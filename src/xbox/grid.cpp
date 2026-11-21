// ToyPortal360 - the on-screen character grid. Hold LB+RB+Y (toyportal.ini:
// grid_combo) in a toy game: every figure of that game shows in a 4x4 grid
// drawn over the game's picture. A puts the figure under the cursor on the
// portal (making its file first if there is none), A again takes it off;
// LB/RB turn the page and B closes the grid.
//
// How it draws: the game's import of the kernel's VdSwap (ordinal 0x25B,
// arguments as in Xenia's xboxkrnl_video.cc) is hooked, so each frame we
// learn where the picture about to be shown is and how it is laid out
// (core/overlay.h). A thread copies the grid image onto the latest two
// front buffers every few milliseconds while the grid is open; the game's
// next frame covers it again once it closes. While it is open the game's
// controller reads (XamInputGetState and the keystroke functions) return
// nothing, so the game does not react to the buttons used in the grid.
//
// Pictures: Toy Portal 360 ships none. A player can put their own (for
// example photos of their figures, converted by tools/make_pictures.py) in
// TPHDD:\ToyPortal\Pictures\<game>\<name>.tpi; the square then shows
// the picture with the name under it.
#include "plugin.h"
#include "../core/figure_db.h"
#include "../core/grid_menu.h"
#include "../core/overlay.h"
#include <stdio.h>
#include <ppcintrinsics.h>

enum
{
	kOrdXamInputGetState = 0x191,
	kOrdXamInputGetKeystroke = 0x193,
	kOrdXamInputGetKeystrokeEx = 0x198,
	kOrdVdSwap = 0x25B // xboxkrnl.exe
};
static const DWORD kErrorEmpty = 0x10D2; // ERROR_EMPTY: no keystroke waiting

#ifdef TOYPORTAL_FORGE
#define CALL_REAL3(fn, a, b, c) tp_ccall((PVOID)(fn), (DWORD)(a), (DWORD)(b), (DWORD)(c), 0, 0, 0, 0)
extern "C" DWORD tp_entry_input_state(DWORD, DWORD, PVOID);
extern "C" DWORD tp_entry_input_key(DWORD, DWORD, PVOID);
extern "C" DWORD tp_entry_input_key_ex(PVOID, DWORD, PVOID);
extern "C" void tp_entry_vdswap();
#define HOOK_ENTRY(forge_entry, hook) ((PVOID)forge_entry)
#else
typedef DWORD (*Call3_t)(DWORD, DWORD, DWORD);
#define CALL_REAL3(fn, a, b, c) ((Call3_t)(fn))((DWORD)(a), (DWORD)(b), (DWORD)(c))
#define HOOK_ENTRY(forge_entry, hook) ((PVOID)hook)
#endif

static PVOID s_real_state = NULL;
static PVOID s_real_key = NULL;
static PVOID s_real_key_ex = NULL;
extern "C"
{
	// The real VdSwap; src/forge/forge.S jumps there after the hook.
	PVOID tp_vdswap_real = NULL;
}

static volatile bool s_hold_input = false;  // the game sees no buttons
static volatile bool s_watch_swaps = false; // record the frames the game shows
static volatile bool s_open = false;

// ---- hooks ------------------------------------------------------------------

extern "C" DWORD HookInputGetState(DWORD user, DWORD flags, XINPUT_STATE* state)
{
	const DWORD r = CALL_REAL3(s_real_state, user, flags, state);
	if (s_hold_input && r == ERROR_SUCCESS && state)
		memset(&state->Gamepad, 0, sizeof(state->Gamepad));
	return r;
}

// Keystrokes are still taken from the queue (so they don't all arrive when
// the grid closes), but the game is told there are none.
extern "C" DWORD HookInputGetKeystroke(DWORD user, DWORD flags, BYTE* keystroke)
{
	const DWORD r = CALL_REAL3(s_real_key, user, flags, keystroke);
	if (s_hold_input && r == ERROR_SUCCESS)
	{
		if (keystroke)
			memset(keystroke, 0, 8);
		return kErrorEmpty;
	}
	return r;
}

extern "C" DWORD HookInputGetKeystrokeEx(DWORD* user, DWORD flags, BYTE* keystroke)
{
	const DWORD r = CALL_REAL3(s_real_key_ex, user, flags, keystroke);
	if (s_hold_input && r == ERROR_SUCCESS)
	{
		if (keystroke)
			memset(keystroke, 0, 8);
		return kErrorEmpty;
	}
	return r;
}

struct SwapRecord
{
	u32 fetch[6]; // the front buffer's texture header, as VdSwap gets it
	u32 address;  // *frontbuffer_ptr
};
static SwapRecord s_swaps[2];
static volatile LONG s_swap_latest = 0;
static volatile DWORD s_swap_count = 0;

// VdSwap(buffer_ptr, fetch_ptr, unk2, unk3, unk4, frontbuffer_ptr,
//        texture_format_ptr, color_space_ptr, width_ptr, height_ptr).
// Runs on the game's render thread before the real VdSwap; only copies.
extern "C" void HookVdSwapPre(DWORD, DWORD fetch_ptr, DWORD, DWORD, DWORD, DWORD frontbuffer_ptr)
{
	if (!s_watch_swaps || !fetch_ptr || !MmIsAddressValid((PVOID)fetch_ptr))
		return;
	const LONG slot = s_swap_latest ^ 1; // the one the grid thread isn't reading
	memcpy(s_swaps[slot].fetch, (const void*)fetch_ptr, sizeof(s_swaps[slot].fetch));
	s_swaps[slot].address = frontbuffer_ptr && MmIsAddressValid((PVOID)frontbuffer_ptr) ? *(const u32*)frontbuffer_ptr : 0;
	s_swap_latest = slot;
	s_swap_count++;
}

#ifndef TOYPORTAL_FORGE
// Without the assembly entry (tests on a PC): call the real one from C.
typedef void (*VdSwap_t)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
extern "C" void HookVdSwap(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g, DWORD h, DWORD i, DWORD j)
{
	HookVdSwapPre(a, b, c, d, e, f);
	((VdSwap_t)tp_vdswap_real)(a, b, c, d, e, f, g, h, i, j);
}
#endif

void grid_hooks_resolve()
{
	s_real_state = hooks_resolve_export("xam.xex", kOrdXamInputGetState);
	s_real_key = hooks_resolve_export("xam.xex", kOrdXamInputGetKeystroke);
	s_real_key_ex = hooks_resolve_export("xam.xex", kOrdXamInputGetKeystrokeEx);
	tp_vdswap_real = hooks_resolve_export("xboxkrnl.exe", kOrdVdSwap);
	log_line("Grid: VdSwap=%p XamInputGetState=%p GetKeystroke=%p GetKeystrokeEx=%p",
		tp_vdswap_real, s_real_state, s_real_key, s_real_key_ex);
}

int grid_patch_module(PLDR_DATA_TABLE_ENTRY m)
{
	const int swap = hooks_patch_import(m, tp_vdswap_real, HOOK_ENTRY(tp_entry_vdswap, HookVdSwap));
	int input = hooks_patch_import(m, s_real_state, HOOK_ENTRY(tp_entry_input_state, HookInputGetState));
	input += hooks_patch_import(m, s_real_key, HOOK_ENTRY(tp_entry_input_key, HookInputGetKeystroke));
	input += hooks_patch_import(m, s_real_key_ex, HOOK_ENTRY(tp_entry_input_key_ex, HookInputGetKeystrokeEx));
	if (swap || input)
		log_line("Grid: hooked %d VdSwap and %d controller import(s) in the module at %p", swap, input, m->ImageBase);
	return swap + input;
}

// A game that looks these functions up while running gets the hooks too.
PVOID grid_swap_lookup(PVOID address)
{
	if (!address)
		return address;
	if (address == tp_vdswap_real) return HOOK_ENTRY(tp_entry_vdswap, HookVdSwap);
	if (address == s_real_state) return HOOK_ENTRY(tp_entry_input_state, HookInputGetState);
	if (address == s_real_key) return HOOK_ENTRY(tp_entry_input_key, HookInputGetKeystroke);
	if (address == s_real_key_ex) return HOOK_ENTRY(tp_entry_input_key_ex, HookInputGetKeystrokeEx);
	return address;
}

// ---- the figures of the game ---------------------------------------------------

struct GridItem
{
	const char* name;
	u32 key;   // what identifies the figure in its file (see key_of)
	int table; // index into the name table (as the *_search functions return)
	u8 series; // Disney Infinity only
};

enum { kMaxItems = 900, kMaxFiles = 256 };
static GridItem s_items[kMaxItems];
static int s_item_count = 0;
static ToyKind s_kind = kToyUnknown;
static char s_file_names[kMaxFiles][48];
static u32 s_file_keys[kMaxFiles];
static int s_file_count = 0;

static u32 sky_key(u16 id, u16 variant) { return ((u32)id << 16) | variant; }

static u32 key_of(ToyKind kind, const BYTE* data)
{
	if (kind == kToySkylanders)
		return sky_key(rd_le16(data + 0x10), rd_le16(data + 0x1C));
	if (kind == kToyDimensions)
		return DimensionsToypad::figure_id_of(data);
	return InfinityBase::figure_number_of(data);
}

static void build_items(ToyKind kind)
{
	s_item_count = 0;
	int n = 0;
	if (kind == kToySkylanders)
	{
		const SkyName* t = sky_names(&n);
		for (int i = 0; i < n && s_item_count < kMaxItems; i++)
		{
			GridItem& it = s_items[s_item_count++];
			it.name = t[i].name; it.key = sky_key(t[i].id, t[i].variant); it.table = i; it.series = 0;
		}
	}
	else if (kind == kToyDimensions)
	{
		const DimName* t = dim_names(&n);
		for (int i = 0; i < n && s_item_count < kMaxItems; i++)
		{
			if (t[i].id == 0) // "Blank Tag" is not a character
				continue;
			GridItem& it = s_items[s_item_count++];
			it.name = t[i].name; it.key = t[i].id; it.table = i; it.series = 0;
		}
		const DimName* v = dim_token_names(&n);
		for (int i = 0; i < n && s_item_count < kMaxItems; i++)
		{
			GridItem& it = s_items[s_item_count++];
			it.name = v[i].name; it.key = v[i].id; it.table = -(i + 1); it.series = 0;
		}
	}
	else if (kind == kToyInfinity)
	{
		const InfName* t = inf_names(&n);
		for (int i = 0; i < n && s_item_count < kMaxItems; i++)
		{
			GridItem& it = s_items[s_item_count++];
			it.name = t[i].name; it.key = t[i].number; it.table = i; it.series = t[i].series;
		}
	}
}

// Reads every figure file of this game once, to know which figure each holds.
static void scan_files(ToyKind kind)
{
	static BYTE data[kSkyFigureSize];
	char folder[96], path[200];
	figure_folder(kind, folder, sizeof(folder));
	s_file_count = list_figure_files(kind, "", s_file_names, kMaxFiles);
	for (int i = 0; i < s_file_count; i++)
	{
		_snprintf(path, sizeof(path), "%s\\%s", folder, s_file_names[i]);
		path[sizeof(path) - 1] = 0;
		s_file_keys[i] = read_figure(path, data, figure_size(kind)) ? key_of(kind, data) : 0xFFFFFFFF;
	}
	log_line("Grid: %d %s figure file(s) found", s_file_count, toy_kind_name(kind));
}

// The portal spot holding the figure with this key, or -1.
static int slot_with(ToyKind kind, u32 key)
{
	int found = -1;
	EnterCriticalSection(&g_lock);
	if (kind == kToySkylanders)
	{
		for (int i = 0; i < kSkySlots && found < 0; i++)
			if (g_router.sky.occupied(i) && sky_key(g_router.sky.figure_id(i), g_router.sky.figure_variant(i)) == key)
				found = i;
	}
	else if (kind == kToyDimensions)
	{
		for (int i = 0; i < kDimSlots && found < 0; i++)
			if (g_router.dim.occupied(i) && g_router.dim.figure(i).id == key)
				found = i;
	}
	else
	{
		for (int i = 0; i < kInfSlots && found < 0; i++)
			if (g_router.inf.occupied(i) && InfinityBase::figure_number_of(g_router.inf.figure(i).data) == key)
				found = i;
	}
	LeaveCriticalSection(&g_lock);
	return found;
}

static void item_label(const GridItem& it, char* out, int n)
{
	if (s_kind == kToyInfinity)
		_snprintf(out, n, "%s (%d.0)", it.name, it.series);
	else
		_snprintf(out, n, "%s", it.name);
	out[n - 1] = 0;
}

// ---- the player's pictures ----------------------------------------------------

// Only the shown page's pictures are kept (in a fixed buffer); what has no
// picture, or a bad one, is remembered so the disk is asked once per opening.
enum { kPicUnknown, kPicMissing, kPicBad, kPicFound };
static u8 s_pic_state[kMaxItems];
// Each shown picture fills its square's whole picture area: centred, with
// see-through pixels round it (drawn in the square's colour).
static u8 s_pics[kGridPerPage][kGridPicW * kGridPicH * 4];
static u8 s_pic_file[kTpiMaxBytes];
static bool s_pic_loaded[kGridPerPage];
static bool s_pic_placed[kGridPerPage]; // as last drawn into s_image
static int s_pic_page = -1;

// The file names a figure's picture may have: its name as figure files
// are named, then its number (Skylanders: "id-variant", then "id").
static int picture_names(const GridItem& it, char (*out)[48])
{
	int n = 0;
	char safe[48];
	safe_file_name(it.name, safe, sizeof(safe));
	_snprintf(out[n++], 48, "%s.tpi", safe);
	if (s_kind == kToySkylanders)
	{
		const unsigned id = it.key >> 16, variant = it.key & 0xFFFF;
		if (variant)
			_snprintf(out[n++], 48, "%u-%u.tpi", id, variant);
		_snprintf(out[n++], 48, "%u.tpi", id);
	}
	else
		_snprintf(out[n++], 48, "%u.tpi", (unsigned)it.key);
	for (int i = 0; i < n; i++)
		out[i][47] = 0;
	return n;
}

static void load_picture(int slot, int item)
{
	s_pic_loaded[slot] = false;
	if (item >= s_item_count || s_pic_state[item] == kPicMissing || s_pic_state[item] == kPicBad)
		return;
	char folder[96], names[3][48], path[200];
	picture_folder(s_kind, folder, sizeof(folder));
	const int n = picture_names(s_items[item], names);
	for (int i = 0; i < n; i++)
	{
		_snprintf(path, sizeof(path), "%s\\%s", folder, names[i]);
		path[sizeof(path) - 1] = 0;
		const DWORD got = read_small_file(path, s_pic_file, kTpiMaxBytes);
		if (got == kNoFile)
			continue;
		u32 w, h;
		const char* why = got > (DWORD)kTpiMaxBytes ? "too big (largest is 128x38)" : tpi_check(s_pic_file, got, &w, &h);
		if (why)
		{
			s_pic_state[item] = kPicBad;
			log_line("Grid: picture %s skipped: %s", names[i], why);
			return;
		}
		if (s_pic_state[item] != kPicFound)
			log_line("Grid: picture %s shown (%ux%u)", names[i], (unsigned)w, (unsigned)h);
		s_pic_state[item] = kPicFound;
		memset(s_pics[slot], 0, sizeof(s_pics[slot])); // all see-through
		int px, py, ax, ay;
		grid_picture_origin(0, w, h, &px, &py);
		grid_picture_origin(0, kGridPicW, kGridPicH, &ax, &ay);
		for (u32 row = 0; row < h; row++)
			memcpy(s_pics[slot] + (((py - ay) + row) * kGridPicW + (px - ax)) * 4, s_pic_file + kTpiHeader + row * w * 4, w * 4);
		s_pic_loaded[slot] = true;
		return;
	}
	s_pic_state[item] = kPicMissing;
}

static void load_page_pictures(int page)
{
	if (page == s_pic_page)
		return;
	s_pic_page = page;
	for (int i = 0; i < kGridPerPage; i++)
		load_picture(i, page * kGridPerPage + i);
}

// ---- place / remove ---------------------------------------------------------

static char s_footer[72];
static DWORD s_footer_until = 0;

static void footer(const char* fmt, const char* name)
{
	char shown[40];
	strncpy(shown, name, sizeof(shown) - 1);
	shown[sizeof(shown) - 1] = 0;
	_snprintf(s_footer, sizeof(s_footer), fmt, shown);
	s_footer[sizeof(s_footer) - 1] = 0;
	s_footer_until = GetTickCount() + 3000;
	log_line("Grid: %s", s_footer);
}

static void footer2(const char* fmt, const char* a, const char* b)
{
	char sa[32], sb[32];
	strncpy(sa, a, sizeof(sa) - 1);
	sa[sizeof(sa) - 1] = 0;
	strncpy(sb, b, sizeof(sb) - 1);
	sb[sizeof(sb) - 1] = 0;
	_snprintf(s_footer, sizeof(s_footer), fmt, sa, sb);
	s_footer[sizeof(s_footer) - 1] = 0;
	s_footer_until = GetTickCount() + 3000;
	log_line("Grid: %s", s_footer);
}

// ---- swapping when every usable spot is taken --------------------------------
//
// A on a new piece when its spots are all full takes off the piece that has
// been on longest and puts the new one in its place, like lifting a toy off
// and setting another down. The new one goes on a moment later, so the game
// sees the old one leave first.

enum { kMaxSpots = 16, kSwapDelayMs = 800 };
static u32 s_spot_age[kMaxSpots]; // when the grid last put something there (0: before)
static u32 s_age_clock = 0;
static ToyKind s_age_kind = kToyUnknown;

struct PendingPlace
{
	bool active;
	DWORD due;
	int slot;
	char path[200];
	char label[64];
	bool created;
	BYTE data[kSkyFigureSize];
};
static PendingPlace s_pending;
static int s_force_pad = -1; // LEGO Dimensions: the pad the player picked

// The spots this piece can use, in the order they're tried.
static int spots_for(const BYTE* data, int* out)
{
	int n = 0;
	if (s_kind == kToySkylanders)
	{
		for (int i = 0; i < kSkySlots; i++)
			out[n++] = i;
	}
	else if (s_kind == kToyDimensions)
	{
		static const u8 pads[3] = {kPadCenter, kPadLeft, kPadRight};
		for (int p = 0; p < 3; p++)
			for (int i = 0; i < kDimSlots; i++)
				if (dimensions_pad_for_slot(i) == pads[p] && (s_force_pad < 0 || pads[p] == s_force_pad))
					out[n++] = i;
	}
	else
	{
		InfPieceKind piece = infinity_piece_kind(InfinityBase::figure_number_of(data));
		if (piece == kInfUnknown)
			piece = kInfCharacter;
		switch (piece)
		{
		case kInfCharacter: out[n++] = 3; out[n++] = 6; break;
		case kInfAbilityDisc: out[n++] = 4; out[n++] = 5; out[n++] = 7; out[n++] = 8; break;
		case kInfPlaySet: out[n++] = 0; break;
		case kInfHexDisc: out[n++] = 0; out[n++] = 1; out[n++] = 2; break;
		default: break;
		}
	}
	return n;
}

static bool spot_taken(int slot)
{
	if (s_kind == kToySkylanders)
		return g_router.sky.occupied(slot);
	if (s_kind == kToyDimensions)
		return g_router.dim.occupied(slot);
	return g_router.inf.occupied(slot);
}

static u32 key_at(int slot)
{
	if (s_kind == kToySkylanders)
		return sky_key(g_router.sky.figure_id(slot), g_router.sky.figure_variant(slot));
	if (s_kind == kToyDimensions)
		return g_router.dim.figure(slot).id;
	return InfinityBase::figure_number_of(g_router.inf.figure(slot).data);
}

static void label_for_key(u32 key, char* out, int n)
{
	for (int i = 0; i < s_item_count; i++)
		if (s_items[i].key == key)
		{
			item_label(s_items[i], out, n);
			return;
		}
	_snprintf(out, n, "a figure");
	out[n - 1] = 0;
}

static void note_placed(int slot)
{
	if (slot >= 0 && slot < kMaxSpots)
		s_spot_age[slot] = ++s_age_clock;
}

static void load_at(const BYTE* data, const char* path, int slot)
{
	if (s_kind == kToySkylanders)
		g_router.sky.load(data, path); // the freed spot is the only free one
	else if (s_kind == kToyDimensions)
		g_router.dim.load(data, path, slot);
	else
		g_router.inf.load(data, path, slot);
}

// Puts a waiting piece on once its time has come (or straight away if `now`).
static bool finish_pending(bool now)
{
	if (!s_pending.active)
		return false;
	if (!now && (int)(GetTickCount() - s_pending.due) < 0)
		return false;
	EnterCriticalSection(&g_lock);
	const bool free_spot = !spot_taken(s_pending.slot);
	if (free_spot)
		load_at(s_pending.data, s_pending.path, s_pending.slot);
	LeaveCriticalSection(&g_lock);
	s_pending.active = false;
	if (free_spot)
	{
		note_placed(s_pending.slot);
		footer(s_pending.created ? "Made and placed %s" : "Placed %s", s_pending.label);
	}
	else
		footer("%s not placed: its spot was filled meanwhile", s_pending.label);
	return true;
}

// Puts the figure in `data` on the first free spot it can use; if they're
// all taken, takes the longest-placed one off and queues the new one for
// that spot. Returns 1 placed, 2 swapping, 0 not possible.
static int place_data(const BYTE* data, const char* path, const char* label, bool created)
{
	int spots[kMaxSpots];
	const int n = spots_for(data, spots);
	if (n == 0)
		return 0;
	EnterCriticalSection(&g_lock);
	int chosen = -1;
	for (int i = 0; i < n && chosen < 0; i++)
		if (!spot_taken(spots[i]))
			chosen = spots[i];
	if (chosen >= 0)
	{
		load_at(data, path, chosen);
		LeaveCriticalSection(&g_lock);
		note_placed(chosen);
		return 1;
	}
	int oldest = spots[0];
	for (int i = 1; i < n; i++)
		if (s_spot_age[spots[i]] < s_spot_age[oldest])
			oldest = spots[i];
	const u32 old_key = key_at(oldest);
	LeaveCriticalSection(&g_lock);

	flush_dirty_figures(); // save its progress first
	EnterCriticalSection(&g_lock);
	if (s_kind == kToySkylanders)
		g_router.sky.remove(oldest);
	else if (s_kind == kToyDimensions)
		g_router.dim.remove(oldest);
	else
		g_router.inf.remove(oldest);
	LeaveCriticalSection(&g_lock);

	s_pending.active = true;
	s_pending.due = GetTickCount() + kSwapDelayMs;
	s_pending.slot = oldest;
	s_pending.created = created;
	strncpy(s_pending.path, path, sizeof(s_pending.path) - 1);
	s_pending.path[sizeof(s_pending.path) - 1] = 0;
	strncpy(s_pending.label, label, sizeof(s_pending.label) - 1);
	s_pending.label[sizeof(s_pending.label) - 1] = 0;
	memcpy(s_pending.data, data, figure_size(s_kind));

	char old_label[64];
	label_for_key(old_key, old_label, sizeof(old_label));
	footer2("Swapping: %s off, %s on", old_label, label);
	return 2;
}

// LEGO Dimensions: A on a figure that isn't on the pad asks which pad.
static bool s_choosing = false;
static int s_choose_item = 0;
static int s_choose_pos = 1; // 0 left, 1 centre, 2 right
static const u8 kChoosePads[3] = {kPadLeft, kPadCenter, kPadRight};
static const char* const kPadNames[3] = {"left", "centre", "right"};

static void pad_use(u8 pad, int* used, int* total)
{
	*used = *total = 0;
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < kDimSlots; i++)
		if (dimensions_pad_for_slot(i) == pad)
		{
			(*total)++;
			if (g_router.dim.occupied(i))
				(*used)++;
		}
	LeaveCriticalSection(&g_lock);
}

static void place_item(const GridItem& it, int pad);

static int s_choose_count = 3;  // 4 when the figure is on: the last box is "Off"
static int s_choose_from = -1;  // its slot when it's on the pad

static int pad_index(u8 pad)
{
	return pad == kPadLeft ? 0 : pad == kPadCenter ? 1 : 2;
}

static void toggle(const GridItem& it)
{
	finish_pending(true); // one swap at a time
	if (s_kind == kToyDimensions)
	{
		s_choose_item = (int)(&it - s_items);
		s_choose_from = slot_with(s_kind, it.key);
		if (s_choose_from >= 0)
		{
			// On the pad already: start on its pad; another pad moves it,
			// "Off" takes it off.
			s_choose_count = 4;
			s_choose_pos = pad_index(dimensions_pad_for_slot(s_choose_from));
		}
		else
		{
			// Start on the first pad with room, centre first.
			static const int order[3] = {1, 0, 2};
			s_choose_count = 3;
			s_choose_pos = 1;
			for (int i = 0; i < 3; i++)
			{
				int used, total;
				pad_use(kChoosePads[order[i]], &used, &total);
				if (used < total)
				{
					s_choose_pos = order[i];
					break;
				}
			}
		}
		s_choosing = true;
		return;
	}
	place_item(it, -1);
}

// LEGO Dimensions: moves a figure that's on the pad to another pad, like
// lifting it and setting it down there. A full pad gives up the figure
// that has been on it longest.
static void move_to_pad(const GridItem& it, int from, u8 pad)
{
	char label[64];
	item_label(it, label, sizeof(label));
	static const char* const names[3] = {"left", "centre", "right"};
	const char* pad_name = names[pad_index(pad)];
	if (dimensions_pad_for_slot(from) == pad)
	{
		footer2("%s is on the %s pad already", label, pad_name);
		return;
	}
	flush_dirty_figures(); // save progress before anything leaves
	EnterCriticalSection(&g_lock);
	int to = g_router.dim.first_free_on_pad(pad);
	char old_label[64];
	old_label[0] = 0;
	if (to < 0)
	{
		for (int i = 0; i < kDimSlots; i++)
			if (dimensions_pad_for_slot(i) == pad && (to < 0 || s_spot_age[i] < s_spot_age[to]))
				to = i;
		if (to >= 0)
		{
			const u32 old_key = key_at(to);
			LeaveCriticalSection(&g_lock);
			label_for_key(old_key, old_label, sizeof(old_label));
			EnterCriticalSection(&g_lock);
		}
	}
	const bool ok = to >= 0 && g_router.dim.move(from, to);
	LeaveCriticalSection(&g_lock);
	if (!ok)
	{
		footer2("Couldn't move %s to the %s pad", label, pad_name);
		return;
	}
	note_placed(to);
	if (old_label[0])
	{
		char moved[64];
		_snprintf(moved, sizeof(moved), "%s to %s", label, pad_name);
		moved[sizeof(moved) - 1] = 0;
		footer2("Moved %s (%s came off)", moved, old_label);
	}
	else
		footer2("Moved %s to the %s pad", label, pad_name);
}

// The picker's A: put on, move, or take off.
static void choose_done()
{
	s_choosing = false;
	const GridItem& it = s_items[s_choose_item];
	const int from = slot_with(s_kind, it.key); // it may have changed meanwhile
	if (s_choose_pos == 3)
	{
		if (from >= 0)
			place_item(it, -1); // takes it off
		return;
	}
	if (from >= 0)
		move_to_pad(it, from, kChoosePads[s_choose_pos]);
	else
		place_item(it, kChoosePads[s_choose_pos]);
}

static void place_item(const GridItem& it, int pad)
{
	char label[64];
	item_label(it, label, sizeof(label));
	finish_pending(true);

	const int slot = slot_with(s_kind, it.key);
	if (slot >= 0)
	{
		flush_dirty_figures(); // save its progress first
		EnterCriticalSection(&g_lock);
		if (s_kind == kToySkylanders)
			g_router.sky.remove(slot);
		else if (s_kind == kToyDimensions)
			g_router.dim.remove(slot);
		else
			g_router.inf.remove(slot);
		LeaveCriticalSection(&g_lock);
		footer("Removed %s", label);
		return;
	}

	char folder[96], path[200];
	figure_folder(s_kind, folder, sizeof(folder));
	path[0] = 0;
	for (int i = 0; i < s_file_count && !path[0]; i++)
		if (s_file_keys[i] == it.key)
			_snprintf(path, sizeof(path), "%s\\%s", folder, s_file_names[i]);
	path[sizeof(path) - 1] = 0;

	bool created = false;
	if (!path[0])
	{
		DWORD error = 0;
		if (!create_figure_file(s_kind, it.table, path, sizeof(path), &error))
		{
			if (error == kCreateNotPossible)
				footer("%s can't be created", label);
			else
			{
				char text[64];
				_snprintf(text, sizeof(text), "Could not save %%s (error %u)", (unsigned)error);
				text[sizeof(text) - 1] = 0;
				footer(text, label);
			}
			return;
		}
		created = true;
		if (s_file_count < kMaxFiles)
		{
			const char* base = strrchr(path, '\\');
			strncpy(s_file_names[s_file_count], base ? base + 1 : path, 47);
			s_file_names[s_file_count][47] = 0;
			s_file_keys[s_file_count] = it.key;
			s_file_count++;
		}
	}

	static BYTE data[kSkyFigureSize];
	if (!read_figure(path, data, figure_size(s_kind)))
	{
		footer("Could not read the file for %s", label);
		return;
	}
	flush_dirty_figures();
	if (pad >= 0)
	{
		const int i = pad == kPadLeft ? 0 : pad == kPadCenter ? 1 : 2;
		const size_t n = strlen(label);
		_snprintf(label + n, sizeof(label) - n, " (%s pad)", kPadNames[i]);
		label[sizeof(label) - 1] = 0;
	}
	s_force_pad = pad;
	const int placed = place_data(data, path, label, created);
	s_force_pad = -1;
	if (placed == 0)
		footer("%s has no spot it can go on", label);
	else if (placed == 1)
		footer(created ? "Made and placed %s" : "Placed %s", label);
}

// ---- drawing ----------------------------------------------------------------

struct Target
{
	SwapRecord record;
	OverlaySurface surface;
	bool usable;
	u32 scale, ox, oy;
	u32 packed[kGridColors];
	OverlayPacker colours; // for the pictures
};
static Target s_targets[2]; // the two most recent different front buffers
static u8 s_image[kGridImageW * kGridImageH];
static char s_problem[96];

static void describe(const OverlaySurface& s, char* out, int n)
{
	_snprintf(out, n, "%08X %ux%u pitch %u %s format %u endian %u swizzle %03X",
		(unsigned)s.address, (unsigned)s.width, (unsigned)s.height, (unsigned)s.pitch,
		s.tiled ? "tiled" : "linear", (unsigned)s.gpu_format, (unsigned)s.endian, (unsigned)s.swizzle);
	out[n - 1] = 0;
}

static void prepare(Target& t, const SwapRecord& r)
{
	t.record = r;
	t.usable = false;
	char what[96];
	const bool parsed = overlay_parse_fetch(r.fetch, r.address, &t.surface);
	describe(t.surface, what, sizeof(what));
	if (!parsed)
	{
		_snprintf(s_problem, sizeof(s_problem), "its picture is in a format the grid can't draw on: %u", (unsigned)t.surface.gpu_format);
		log_line("Grid: can't draw on front buffer %s", what);
		return;
	}
	// The whole picture must be mapped memory we can write.
	const u32 span = overlay_span(t.surface);
	for (u32 off = 0; off < span; off += 4096)
		if (!MmIsAddressValid((PVOID)(t.surface.address + off)))
		{
			_snprintf(s_problem, sizeof(s_problem), "its picture at %08X can't be reached", (unsigned)t.surface.address);
			log_line("Grid: front buffer %s: not mapped at +%X", what, (unsigned)off);
			return;
		}
	// 1x at 640x480, 2x at 720p, 3x at 1080p: the largest that fits in 90%.
	const u32 sx = t.surface.width * 9 / 10 / kGridImageW, sy = t.surface.height * 9 / 10 / kGridImageH;
	t.scale = sx < sy ? sx : sy;
	if (g_config.grid_small && t.scale > 1)
		t.scale = 1;
	if (t.scale == 0 && t.surface.width >= (u32)kGridImageW && t.surface.height >= (u32)kGridImageH)
		t.scale = 1;
	if (t.scale == 0)
	{
		_snprintf(s_problem, sizeof(s_problem), "its picture is too small (%ux%u)", (unsigned)t.surface.width, (unsigned)t.surface.height);
		log_line("Grid: front buffer %s is too small", what);
		return;
	}
	t.ox = ((t.surface.width - kGridImageW * t.scale) / 2) & ~3u;
	t.oy = (t.surface.height - kGridImageH * t.scale) / 2;
	for (int c = 0; c < kGridColors; c++)
		t.packed[c] = overlay_pack(t.surface, kGridPalette[c][0], kGridPalette[c][1], kGridPalette[c][2]);
	overlay_packer_init(t.surface, &t.colours);
	t.usable = true;
	log_line("Grid: drawing on front buffer %s at %ux", what, (unsigned)t.scale);
}

// Takes the newest frame the game showed into the target list.
static void note_newest_swap()
{
	const SwapRecord r = s_swaps[s_swap_latest];
	if (memcmp(&r, &s_targets[0].record, sizeof(r)) == 0)
		return;
	if (memcmp(&r, &s_targets[1].record, sizeof(r)) == 0)
	{
		const Target t = s_targets[0];
		s_targets[0] = s_targets[1];
		s_targets[1] = t;
		return;
	}
	s_targets[1] = s_targets[0];
	prepare(s_targets[0], r);
}

// A picture pixel for overlay_blit_fast: the image leaves the picture
// areas as kOverlaySkip and asks here for their colour.
static u32 picture_colour(u32 ix, u32 iy, void* ctx)
{
	const Target& t = *(const Target*)ctx;
	const int col = ((int)ix - 7) / (kGridCellW + 6), row = ((int)iy - 30) / (kGridCellH + 6);
	const int slot = row * kGridCols + col;
	if (col < 0 || row < 0 || col >= kGridCols || row >= kGridRows)
		return t.packed[kGridCell];
	const u32 background = t.packed[s_pic_placed[slot] ? kGridCellOn : kGridCell];
	int cx, cy;
	grid_cell_origin(slot, &cx, &cy);
	const int px = (int)ix - cx - kGridPicX, py = (int)iy - cy - kGridPicY;
	if (!s_pic_loaded[slot] || px < 0 || py < 0 || px >= kGridPicW || py >= kGridPicH)
		return background;
	const u8* a = s_pics[slot] + (py * kGridPicW + px) * 4;
	return a[0] < 128 ? background : t.colours.pack(a[1], a[2], a[3]);
}

// Pushes a finished 128-byte line out to RAM, where the display reads it
// (the front buffer may be cached memory).
static void line_done(u8* line, void*)
{
#ifdef TOYPORTAL_FORGE
	__asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
#else
	__dcbst(0, line);
#endif
}

static DWORD s_pass_ms_sum = 0;
static int s_pass_count = 0;

static void blit_all()
{
	const DWORD start = GetTickCount();
	if (s_kind == kToyDimensions)
	{
		u8 rgb[3][3];
		for (int i = 0; i < 3; i++)
			g_router.dim.light(kChoosePads[i], rgb[i]);
		for (int t = 0; t < 2; t++)
			if (s_targets[t].usable)
				for (int i = 0; i < 3; i++)
					s_targets[t].packed[kGridLight1 + i] = s_targets[t].colours.pack(rgb[i][0], rgb[i][1], rgb[i][2]);
	}
	// The newest frame first: it's the one on screen.
	for (int i = 0; i < 2; i++)
	{
		Target& t = s_targets[i];
		if (!t.usable)
			continue;
		overlay_blit_fast(t.surface, (u8*)t.surface.address, s_image, kGridImageW, kGridImageH, t.scale, t.ox, t.oy,
			t.packed, picture_colour, line_done, &t);
	}
	__sync();
	// How long one pass takes, logged once per opening.
	if (s_pass_count >= 0)
	{
		s_pass_ms_sum += GetTickCount() - start;
		if (++s_pass_count == 50)
		{
			log_line("Grid: one drawing pass (both buffers) takes about %u.%u ms",
				(unsigned)(s_pass_ms_sum / 50), (unsigned)(s_pass_ms_sum * 10 / 50 % 10));
			s_pass_count = -1;
		}
	}
}

// ---- the grid -----------------------------------------------------------------

static GridMenu s_menu;
static GridInput s_input;

// Draws the current page into s_image. Returns a value that changes when
// anything on screen would.
static u32 render(bool draw)
{
	GridView v;
	memset(&v, 0, sizeof(v));
	_snprintf(v.title, sizeof(v.title), "Toy Portal 360 - %s", toy_kind_name(s_kind));
	_snprintf(v.page, sizeof(v.page), "Page %d of %d", s_menu.page() + 1, s_menu.pages());
	const bool message = s_footer[0] && (int)(s_footer_until - GetTickCount()) > 0;
	strncpy(v.footer, message ? s_footer : s_kind == kToyDimensions ?
		"A: pick a pad   LB/RB: page   B: close" : "A: place/remove   LB/RB: page   B: close",
		sizeof(v.footer) - 1);
	if (s_choosing)
	{
		v.choosing = true;
		v.choose = s_choose_pos;
		v.choose_count = s_choose_count;
		_snprintf(v.choose_text, sizeof(v.choose_text), "%s:", s_items[s_choose_item].name);
		v.choose_text[sizeof(v.choose_text) - 1] = 0;
		static const char* const titles[3] = {"Left", "Centre", "Right"};
		for (int i = 0; i < 3; i++)
		{
			// A lit pad shows its colour's name, as the game's hints use them.
			u8 rgb[3];
			g_router.dim.light(kChoosePads[i], rgb);
			const char* colour = grid_colour_name(rgb[0], rgb[1], rgb[2]);
			v.choose_lit[i] = colour != 0;
			if (colour)
				_snprintf(v.choose_names[i], sizeof(v.choose_names[i]), "%s %s", titles[i], colour);
			else
			{
				int used, total;
				pad_use(kChoosePads[i], &used, &total);
				_snprintf(v.choose_names[i], sizeof(v.choose_names[i]), "%s %d/%d", titles[i], used, total);
			}
			v.choose_names[i][sizeof(v.choose_names[i]) - 1] = 0;
		}
		strcpy(v.choose_names[3], "Off");
	}
	else if (s_kind == kToyDimensions && !message)
	{
		v.show_lights = true;
		for (int i = 0; i < 3; i++)
		{
			u8 rgb[3];
			g_router.dim.light(kChoosePads[i], rgb);
			v.lit[i] = grid_colour_name(rgb[0], rgb[1], rgb[2]) != 0;
		}
	}
	const int first = s_menu.page() * kGridPerPage;
	load_page_pictures(s_menu.page());
	u32 state = (u32)s_menu.cursor() * 2654435761u ^ (message ? 0x80000000u : 0) ^ (u32)(size_t)s_footer_until ^
		(s_choosing ? 0x40000000u + 0x1000u * (u32)(s_choose_pos + 1) + 0x10000u * (u32)s_choose_count : 0) ^
		(s_kind == kToyDimensions ? g_router.dim.light_changes() * 0x9E3779B1u : 0);
	for (int i = 0; i < kGridPerPage && first + i < s_item_count; i++)
	{
		const GridItem& it = s_items[first + i];
		item_label(it, v.cells[i].name, sizeof(v.cells[i].name));
		v.cells[i].placed = slot_with(s_kind, it.key) >= 0;
		v.cells[i].picture = s_pic_loaded[i];
		if (v.cells[i].placed)
			state ^= 0x9E3779B9u * (u32)(i + 1);
		v.cell_count = i + 1;
	}
	v.cursor = s_menu.cursor() - first;
	if (draw)
	{
		grid_render(s_image, v);
		for (int i = 0; i < kGridPerPage; i++)
			s_pic_placed[i] = v.cells[i].placed;
	}
	return state;
}

static u16 read_pad(XINPUT_STATE* st)
{
	memset(st, 0, sizeof(*st));
	if (XInputGetState(g_menu_user, st) != ERROR_SUCCESS)
		return 0;
	return st->Gamepad.wButtons;
}

static void close_grid(const char* why)
{
	s_watch_swaps = false;
	// Keep the game's controller blocked until the buttons are let go, so
	// the B that closed the grid doesn't reach it.
	for (int i = 0; i < 100; i++)
	{
		XINPUT_STATE st;
		if (!read_pad(&st))
			break;
		Sleep(15);
	}
	s_hold_input = false;
	log_line("Grid closed (%s)", why);
}

static DWORD WINAPI grid_thread(LPVOID)
{
	ToyKind kind = menu_pick_kind();
	if (kind == kToyUnknown)
	{
		g_menu_open = false;
		s_open = false;
		return 0;
	}
	s_kind = kind;
	if (s_age_kind != kind)
	{
		memset(s_spot_age, 0, sizeof(s_spot_age));
		s_age_kind = kind;
	}
	s_pending.active = false;
	s_choosing = false;
	s_pass_ms_sum = 0;
	s_pass_count = 0;
	build_items(kind);
	s_menu.reset(s_item_count);
	s_footer[0] = 0;
	memset(s_pic_state, kPicUnknown, sizeof(s_pic_state)); // pictures added since are seen
	memset(s_pic_loaded, 0, sizeof(s_pic_loaded));
	s_pic_page = -1;
	memset(s_targets, 0, sizeof(s_targets));
	s_problem[0] = 0;

	XINPUT_STATE st;
	s_input.reset(read_pad(&st));
	s_hold_input = true;
	s_watch_swaps = true;
	log_line("Grid opened: %s, %d figures", toy_kind_name(kind), s_item_count);

	// Wait for a frame we can draw on.
	DWORD seen = s_swap_count;
	const DWORD start = GetTickCount();
	while (!s_targets[0].usable && GetTickCount() - start < 1500)
	{
		if (s_swap_count != seen)
		{
			seen = s_swap_count;
			note_newest_swap();
		}
		Sleep(10);
	}
	if (!s_targets[0].usable)
	{
		const bool no_frames = s_swap_count == 0 && !s_problem[0];
		close_grid(no_frames ? "no frames seen" : s_problem);
		wchar_t text[300];
		if (no_frames)
			_snwprintf(text, 299, L"The character grid can't be shown in this game: it didn't show Toy Portal 360 a picture to draw on.\n\nUse LB+RB+Back for the menu instead.");
		else
			_snwprintf(text, 299, L"The character grid can't be shown in this game: %hs.\n\nUse LB+RB+Back for the menu instead.", s_problem);
		text[299] = 0;
		menu_say(L"Character grid", text);
		g_menu_open = false;
		s_open = false;
		return 0;
	}

	// First picture, then read which figure each file holds.
	_snprintf(s_footer, sizeof(s_footer), "Reading your figure files...");
	s_footer_until = GetTickCount() + 60000;
	render(true);
	blit_all();
	scan_files(kind);
	s_footer[0] = 0;
	u32 shown = render(true) + 1;

	for (;;)
	{
		if (s_swap_count != seen)
		{
			seen = s_swap_count;
			note_newest_swap();
		}
		const u16 buttons = read_pad(&st);
		const u16 pressed = s_input.update(buttons, st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, GetTickCount());
		if (s_choosing)
		{
			// Left / right pick the pad, A puts the figure there, B goes back.
			if ((pressed & XINPUT_GAMEPAD_DPAD_LEFT) && s_choose_pos > 0)
				s_choose_pos--;
			if ((pressed & XINPUT_GAMEPAD_DPAD_RIGHT) && s_choose_pos < s_choose_count - 1)
				s_choose_pos++;
			if (pressed & XINPUT_GAMEPAD_A)
			{
				choose_done();
				shown = render(false) + 1;
			}
			else if (pressed & XINPUT_GAMEPAD_B)
				s_choosing = false;
			if (finish_pending(false))
				shown = render(false) + 1;
			const u32 now = render(false);
			if (now != shown)
			{
				render(true);
				shown = now;
			}
			blit_all();
			Sleep(1);
			continue;
		}
		const GridAction a = s_menu.press(pressed);
		if (a == kGridClose)
			break;
		if (a == kGridToggle && s_menu.cursor() < s_item_count)
		{
			toggle(s_items[s_menu.cursor()]);
			shown = render(false) + 1; // redraw
		}
		if (finish_pending(false))
			shown = render(false) + 1;
		const u32 now = render(false);
		if (now != shown)
		{
			render(true);
			shown = now;
		}
		// Draw again straight away: the game's GPU keeps writing new frames
		// over the buffers, and every pass that lands after it shows.
		blit_all();
		Sleep(1);
	}

	// A swap still waiting goes on before the grid closes.
	if (s_pending.active)
	{
		while ((int)(GetTickCount() - s_pending.due) < 0)
			Sleep(10);
		finish_pending(true);
	}
	close_grid("B");
	g_menu_open = false;
	s_open = false;
	return 0;
}

bool grid_start()
{
	if (s_open)
		return false;
	s_open = true;
#ifdef TOYPORTAL_SIDELOAD
	const bool system_thread = false; // the game owns this DLL's threads
#else
	const bool system_thread = true;
#endif
	if (!tp_start_thread(grid_thread, NULL, NULL, system_thread))
	{
		s_open = false;
		return false;
	}
	return true;
}
