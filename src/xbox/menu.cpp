// ToyPortal360 - on-console menu. Hold the combo (default LB + RB + Back)
// while a toy game is running. Uses the system's own message box and
// keyboard, so it draws over any game without hooking its renderer.
#include "plugin.h"
#include "../core/figure_db.h"
#include <stdio.h>
#include <ppcintrinsics.h>

volatile bool g_menu_open = false; // the menu or the grid is showing
DWORD g_menu_user = 0;

// ---- small UI helpers -------------------------------------------------------

typedef VOID (*XNotifyQueueUI_t)(DWORD type, DWORD user, ULONGLONG areas, PWCHAR text, PVOID context);

void notify(const wchar_t* text)
{
	static XNotifyQueueUI_t fn = NULL;
	if (!fn)
	{
		HANDLE h = NULL;
		if (XexGetModuleHandle((PSZ)"xam.xex", &h) == 0 && h)
			XexGetProcedureAddress(h, kOrdXNotifyQueueUI, (PVOID)&fn);
	}
	if (fn)
	{
#ifdef TOYPORTAL_FORGE
		// The 64-bit "areas" argument takes one register on the console.
		tp_ccall((PVOID)fn, 14, XUSER_INDEX_ANY, 2, (DWORD)text, 0, 0, 0);
#else
		fn(14, XUSER_INDEX_ANY, 2, (PWCHAR)text, NULL);
#endif
	}
}

static void widen(const char* in, wchar_t* out, int n)
{
	int i = 0;
	for (; in[i] && i < n - 1; i++)
		out[i] = (unsigned char)in[i];
	out[i] = 0;
}

static void narrow(const wchar_t* in, char* out, int n)
{
	int i = 0;
	for (; in[i] && i < n - 1; i++)
		out[i] = in[i] < 128 ? (char)in[i] : '?';
	out[i] = 0;
}

static const char* base_name(const char* path)
{
	const char* s = strrchr(path, '\\');
	return s ? s + 1 : path;
}

// Shows a message box. Returns the button pressed, or -1 for B / cancel.
static int ask(const wchar_t* title, const wchar_t* text, int count,
	const wchar_t* b0, const wchar_t* b1, const wchar_t* b2, int focus)
{
	LPCWSTR buttons[3] = {b0, b1, b2};
	for (int attempt = 0; attempt < 40; attempt++)
	{
		MESSAGEBOX_RESULT result;
		XOVERLAPPED ov;
		memset(&result, 0, sizeof(result));
		memset(&ov, 0, sizeof(ov));
		const DWORD r = XShowMessageBoxUI(g_menu_user, title, text, count, buttons, focus, XMB_NOICON, &result, &ov);
		if (r == ERROR_ACCESS_DENIED) // another system UI is still closing
		{
			Sleep(100);
			continue;
		}
		if (r != ERROR_IO_PENDING)
			return -1;
		while (!XHasOverlappedIoCompleted(&ov))
			Sleep(30);
		if (XGetOverlappedResult(&ov, NULL, TRUE) != ERROR_SUCCESS)
			return -1;
		return (int)result.dwButtonPressed;
	}
	return -1;
}

static void say(const wchar_t* title, const wchar_t* text)
{
	ask(title, text, 1, L"OK", NULL, NULL, 0);
}

void menu_say(const wchar_t* title, const wchar_t* text)
{
	say(title, text);
}

// On-screen keyboard. Returns false if cancelled. Like the message boxes, it
// is refused while the previous system window is still closing (on a console:
// choosing Create opened nothing), so it retries; any other failure is shown.
static bool keyboard(const wchar_t* title, const wchar_t* description, char* out, int n)
{
	wchar_t result[64];
	XOVERLAPPED ov;
	DWORD r = 0;
	for (int attempt = 0; attempt < 40; attempt++)
	{
		memset(result, 0, sizeof(result));
		memset(&ov, 0, sizeof(ov));
		r = XShowKeyboardUI(g_menu_user, VKBD_DEFAULT, L"", title, description, result, 63, &ov);
		if (r != ERROR_ACCESS_DENIED)
			break;
		Sleep(100);
	}
	if (r != ERROR_IO_PENDING)
	{
		wchar_t text[160];
		_snwprintf(text, 159, L"The on-screen keyboard did not open (error %08X).", (unsigned)r);
		text[159] = 0;
		log_line("keyboard: XShowKeyboardUI returned %08X", (unsigned)r);
		say(L"Toy Portal 360", text);
		return false;
	}
	while (!XHasOverlappedIoCompleted(&ov))
		Sleep(30);
	if (XGetOverlappedResult(&ov, NULL, TRUE) != ERROR_SUCCESS)
		return false;
	narrow(result, out, n);
	return true;
}

typedef void (*label_fn)(int i, wchar_t* out, int n, void* ctx);

// Pages through `count` items one at a time. Returns the chosen index or -1.
static int browse(const wchar_t* title, const wchar_t* action, int count, label_fn label, void* ctx)
{
	if (count <= 0)
		return -1;
	int i = 0;
	for (;;)
	{
		wchar_t item[96], text[256];
		label(i, item, 96, ctx);
		_snwprintf(text, 255, L"%d of %d\n\n%s\n\nPress B to go back.", i + 1, count, item);
		text[255] = 0;
		const int b = ask(title, text, 3, L"Previous", L"Next", action, count > 1 ? 1 : 2);
		if (b < 0)
			return -1;
		if (b == 0)
			i = (i + count - 1) % count;
		else if (b == 1)
			i = (i + 1) % count;
		else
			return i;
	}
}

// ---- figure naming ----------------------------------------------------------

static void figure_label(ToyKind kind, int slot, wchar_t* out, int n)
{
	const char* name = NULL;
	char fallback[64];
	EnterCriticalSection(&g_lock);
	if (kind == kToySkylanders)
	{
		name = sky_name_for(g_router.sky.figure_id(slot), g_router.sky.figure_variant(slot));
		strncpy(fallback, base_name(g_router.sky.figure(slot).path), 63);
	}
	else if (kind == kToyDimensions)
	{
		name = dim_name_for(g_router.dim.figure(slot).id);
		strncpy(fallback, base_name(g_router.dim.figure(slot).path), 63);
	}
	else
	{
		name = inf_name_for(InfinityBase::figure_number_of(g_router.inf.figure(slot).data));
		strncpy(fallback, base_name(g_router.inf.figure(slot).path), 63);
	}
	LeaveCriticalSection(&g_lock);
	fallback[63] = 0;
	widen(name ? name : fallback, out, n);
}

static const wchar_t* pad_name(u8 pad)
{
	return pad == kPadLeft ? L"Left" : pad == kPadCenter ? L"Centre" : L"Right";
}

static const wchar_t* kind_title(ToyKind kind)
{
	return kind == kToySkylanders ? L"Skylanders" : kind == kToyDimensions ? L"Dimensions" : L"Disney Infinity";
}

static const wchar_t* device_noun(ToyKind kind)
{
	return kind == kToyDimensions ? L"toy pad" : kind == kToyInfinity ? L"base" : L"portal";
}

static const wchar_t* inf_group_name(u8 group)
{
	return group == 1 ? L"Play set & discs" : group == 2 ? L"Player 1" : L"Player 2";
}

static void portal_summary(ToyKind kind, wchar_t* out, int n)
{
	int len = 0;
	out[0] = 0;
	if (kind == kToySkylanders)
	{
		len += _snwprintf(out + len, n - len, L"On the portal:\n");
		int shown = 0;
		for (int i = 0; i < kSkySlots; i++)
		{
			EnterCriticalSection(&g_lock);
			const bool on = g_router.sky.occupied(i);
			LeaveCriticalSection(&g_lock);
			if (!on)
				continue;
			wchar_t name[64];
			figure_label(kind, i, name, 64);
			len += _snwprintf(out + len, n - len, L"  %s\n", name);
			shown++;
		}
		if (!shown)
			_snwprintf(out + len, n - len, L"  (nothing)\n");
	}
	else if (kind == kToyInfinity)
	{
		for (u8 g = 1; g <= 3; g++)
		{
			len += _snwprintf(out + len, n - len, L"%s: ", inf_group_name(g));
			int shown = 0;
			for (int i = 0; i < kInfSlots; i++)
			{
				EnterCriticalSection(&g_lock);
				const bool on = g_router.inf.occupied(i) && infinity_group_for_slot(i) == g;
				LeaveCriticalSection(&g_lock);
				if (!on)
					continue;
				wchar_t name[48];
				figure_label(kind, i, name, 48);
				len += _snwprintf(out + len, n - len, L"%s%s", shown ? L", " : L"", name);
				shown++;
			}
			len += _snwprintf(out + len, n - len, L"%s\n", shown ? L"" : L"(empty)");
		}
	}
	else
	{
		static const u8 pads[3] = {kPadLeft, kPadCenter, kPadRight};
		for (int p = 0; p < 3; p++)
		{
			len += _snwprintf(out + len, n - len, L"%s: ", pad_name(pads[p]));
			int shown = 0;
			for (int i = 0; i < kDimSlots; i++)
			{
				EnterCriticalSection(&g_lock);
				const bool on = g_router.dim.occupied(i) && dimensions_pad_for_slot(i) == pads[p];
				LeaveCriticalSection(&g_lock);
				if (!on)
					continue;
				wchar_t name[48];
				figure_label(kind, i, name, 48);
				len += _snwprintf(out + len, n - len, L"%s%s", shown ? L", " : L"", name);
				shown++;
			}
			len += _snwprintf(out + len, n - len, L"%s\n", shown ? L"" : L"(empty)");
		}
	}
	out[n - 1] = 0;
}

// ---- actions ----------------------------------------------------------------

struct FileList
{
	char names[256][48];
	int count;
};
static FileList s_files;

static void file_label(int i, wchar_t* out, int n, void*)
{
	widen(s_files.names[i], out, n);
}

static bool path_on_portal(const char* path)
{
	bool found = false;
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < kSkySlots && !found; i++)
		found = g_router.sky.occupied(i) && _stricmp(g_router.sky.figure(i).path, path) == 0;
	for (int i = 0; i < kDimSlots && !found; i++)
		found = g_router.dim.occupied(i) && _stricmp(g_router.dim.figure(i).path, path) == 0;
	for (int i = 0; i < kInfSlots && !found; i++)
		found = g_router.inf.occupied(i) && _stricmp(g_router.inf.figure(i).path, path) == 0;
	LeaveCriticalSection(&g_lock);
	return found;
}

static int choose_pad(const wchar_t* title)
{
	const int b = ask(title, L"Which pad?", 3, L"Left", L"Centre", L"Right", 1);
	if (b < 0)
		return -1;
	return b == 0 ? kPadLeft : b == 1 ? kPadCenter : kPadRight;
}

// Puts the figure file at `path` on the portal/pad.
static void place_path(ToyKind kind, const char* path, int pad)
{
	wchar_t msg[160], wname[64];
	widen(base_name(path), wname, 64);
	if (path_on_portal(path))
	{
		wchar_t text[96];
		_snwprintf(text, 95, L"That figure is already on the %s.", device_noun(kind));
		text[95] = 0;
		say(L"Place figure", text);
		return;
	}
	static BYTE data[kSkyFigureSize]; // the largest figure size
	if (!read_figure(path, data, figure_size(kind)))
	{
		say(L"Place figure", L"Could not read that file.");
		return;
	}

	// Disney Infinity: the piece decides its spot; characters and ability
	// discs also need a side.
	int inf_slot = -1;
	if (kind == kToyInfinity)
	{
		InfPieceKind piece = infinity_piece_kind(InfinityBase::figure_number_of(data));
		if (piece == kInfUnknown)
			piece = kInfCharacter;
		int side = 1;
		if (piece == kInfCharacter || piece == kInfAbilityDisc)
		{
			const int b = ask(L"Place figure", piece == kInfCharacter ? L"Which player?" : L"Which player's ability spot?",
				2, L"Player 1", L"Player 2", NULL, 0);
			if (b < 0)
				return;
			side = b + 1;
		}
		EnterCriticalSection(&g_lock);
		inf_slot = g_router.inf.free_slot_for(piece, side);
		LeaveCriticalSection(&g_lock);
		if (inf_slot < 0)
		{
			say(L"Place figure", L"That spot on the base is taken. Remove a figure first.");
			return;
		}
	}
	flush_dirty_figures();

	bool ok = false;
	EnterCriticalSection(&g_lock);
	if (kind == kToySkylanders)
	{
		ok = g_router.sky.load(data, path) >= 0;
	}
	else if (kind == kToyInfinity)
	{
		ok = !g_router.inf.occupied(inf_slot);
		if (ok)
			g_router.inf.load(data, path, inf_slot);
	}
	else
	{
		const int slot = g_router.dim.first_free_on_pad((u8)pad);
		if (slot >= 0)
		{
			g_router.dim.load(data, path, slot);
			ok = true;
		}
	}
	LeaveCriticalSection(&g_lock);

	if (!ok)
	{
		say(L"Place figure", kind == kToySkylanders ? L"The portal is full (8 figures). Remove one first." :
			kind == kToyInfinity ? L"That spot on the base is taken. Remove a figure first." :
			L"That pad is full. Remove or move a figure first.");
		return;
	}
	_snwprintf(msg, 159, L"Placed %s", wname);
	msg[159] = 0;
	notify(msg);
}

static void action_place(ToyKind kind)
{
	int pad = 0;
	if (kind == kToyDimensions)
	{
		pad = choose_pad(L"Place figure");
		if (pad < 0)
			return;
		EnterCriticalSection(&g_lock);
		const int free_slot = g_router.dim.first_free_on_pad((u8)pad);
		LeaveCriticalSection(&g_lock);
		if (free_slot < 0)
		{
			say(L"Place figure", L"That pad is full. Remove or move a figure first.");
			return;
		}
	}

	char filter[48] = "";
	if (!keyboard(L"Find a figure", L"Type part of the file name, or leave it blank to see them all.", filter, sizeof(filter)))
		return;

	s_files.count = list_figure_files(kind, filter, s_files.names, 256);
	if (s_files.count == 0)
	{
		char folder[96], shown_folder[96];
		wchar_t text[256], wfolder[96];
		figure_folder(kind, folder, sizeof(folder));
		display_path(folder, shown_folder, sizeof(shown_folder));
		widen(shown_folder, wfolder, 96);
		_snwprintf(text, 255, L"No figure files found in\n%s\n\nCopy your dumps there, or use More > Create new figure.", wfolder);
		text[255] = 0;
		say(L"Place figure", text);
		return;
	}
	const int i = browse(L"Place figure", L"Place", s_files.count, file_label, NULL);
	if (i < 0)
		return;

	char folder[96], path[200];
	figure_folder(kind, folder, sizeof(folder));
	_snprintf(path, sizeof(path), "%s\\%s", folder, s_files.names[i]);
	path[sizeof(path) - 1] = 0;
	place_path(kind, path, pad);
}

struct SlotList
{
	ToyKind kind;
	int slots[kInfSlots];
	int count;
};

static void slot_label(int i, wchar_t* out, int n, void* ctx)
{
	SlotList* l = (SlotList*)ctx;
	wchar_t name[64];
	figure_label(l->kind, l->slots[i], name, 64);
	if (l->kind == kToyDimensions)
		_snwprintf(out, n, L"%s (%s pad)", name, pad_name(dimensions_pad_for_slot(l->slots[i])));
	else if (l->kind == kToyInfinity)
		_snwprintf(out, n, L"%s (%s)", name, inf_group_name(infinity_group_for_slot(l->slots[i])));
	else
		_snwprintf(out, n, L"%s", name);
	out[n - 1] = 0;
}

static void occupied_slots(ToyKind kind, SlotList* l)
{
	l->kind = kind;
	l->count = 0;
	EnterCriticalSection(&g_lock);
	const int max = kind == kToySkylanders ? (int)kSkySlots : kind == kToyDimensions ? (int)kDimSlots : (int)kInfSlots;
	for (int i = 0; i < max; i++)
	{
		const bool on = kind == kToySkylanders ? g_router.sky.occupied(i) :
		                kind == kToyDimensions ? g_router.dim.occupied(i) : g_router.inf.occupied(i);
		if (on)
			l->slots[l->count++] = i;
	}
	LeaveCriticalSection(&g_lock);
}

static void action_remove(ToyKind kind)
{
	SlotList l;
	occupied_slots(kind, &l);
	if (!l.count)
	{
		wchar_t text[96];
		_snwprintf(text, 95, L"There is nothing on the %s.", device_noun(kind));
		text[95] = 0;
		say(L"Remove figure", text);
		return;
	}
	const int i = browse(L"Remove figure", L"Remove", l.count, slot_label, &l);
	if (i < 0)
		return;
	wchar_t name[64], msg[96];
	figure_label(kind, l.slots[i], name, 64);
	flush_dirty_figures(); // save its progress first
	EnterCriticalSection(&g_lock);
	if (kind == kToySkylanders)
		g_router.sky.remove(l.slots[i]);
	else if (kind == kToyDimensions)
		g_router.dim.remove(l.slots[i]);
	else
		g_router.inf.remove(l.slots[i]);
	LeaveCriticalSection(&g_lock);
	_snwprintf(msg, 95, L"Removed %s", name);
	msg[95] = 0;
	notify(msg);
}

static void action_move()
{
	SlotList l;
	occupied_slots(kToyDimensions, &l);
	if (!l.count)
	{
		say(L"Move figure", L"There is nothing on the toy pad.");
		return;
	}
	const int i = browse(L"Move figure", L"Move", l.count, slot_label, &l);
	if (i < 0)
		return;
	const int pad = choose_pad(L"Move figure to...");
	if (pad < 0)
		return;
	const int from = l.slots[i];

	EnterCriticalSection(&g_lock);
	const int to = dimensions_pad_for_slot(from) == pad ? from : g_router.dim.first_free_on_pad((u8)pad);
	const bool ok = to >= 0 && g_router.dim.move(from, to);
	LeaveCriticalSection(&g_lock);

	if (!ok)
		say(L"Move figure", L"That pad is full. Remove or move a figure first.");
	else
		notify(dimensions_pad_for_slot(from) == pad ? L"Figure lifted and put back" : L"Figure moved");
}

struct MatchList
{
	ToyKind kind;
	int idx[600];
	int count;
};
static MatchList s_matches;

static void match_label(int i, wchar_t* out, int n, void*)
{
	const int k = s_matches.idx[i];
	int count = 0;
	if (s_matches.kind == kToySkylanders)
		widen(sky_names(&count)[k].name, out, n);
	else if (s_matches.kind == kToyInfinity)
	{
		const InfName& f = inf_names(&count)[k];
		_snwprintf(out, n, L"%hs (%d.0)", f.name, f.series); // RPCS3 shows the series too
		out[n - 1] = 0;
	}
	else if (k >= 0)
		widen(dim_names(&count)[k].name, out, n);
	else
		widen(dim_token_names(&count)[-k - 1].name, out, n);
}

static u32 random32()
{
	return (u32)__mftb() * 2654435761u ^ GetTickCount();
}

// Makes a new figure file for entry k of the game's name table (an index as
// the *_search functions return it). On failure *error holds the system
// error, or kCreateNotPossible for a piece that can't be made.
bool create_figure_file(ToyKind kind, int k, char* path, int path_size, DWORD* error)
{
	static BYTE data[kSkyFigureSize];
	const char* name;
	const char* ext;
	DWORD size;
	int count = 0;
	path[0] = 0;
	*error = 0;
	if (kind == kToySkylanders)
	{
		const SkyName& s = sky_names(&count)[k];
		const u32 r = random32();
		const u8 nuid[4] = {(u8)r, (u8)(r >> 8), (u8)(r >> 16), (u8)(r >> 24)};
		skylander_create_figure(data, s.id, s.variant, nuid);
		name = s.name;
		ext = "sky";
		size = kSkyFigureSize;
	}
	else if (kind == kToyInfinity)
	{
		const InfName& f = inf_names(&count)[k];
		u8 uid[7];
		const u32 a = random32(), b = random32();
		uid[0] = (u8)a; uid[1] = (u8)(a >> 8); uid[2] = (u8)(a >> 16); uid[3] = (u8)(a >> 24);
		uid[4] = (u8)b; uid[5] = (u8)(b >> 8); uid[6] = (u8)(b >> 16);
		if (!InfinityBase::create_blank(data, f.number, f.series, uid))
		{
			*error = kCreateNotPossible;
			return false;
		}
		name = f.name;
		ext = "bin";
		size = kInfFigureSize;
	}
	else
	{
		const DimName& d = k >= 0 ? dim_names(&count)[k] : dim_token_names(&count)[-k - 1];
		u8 uid[7];
		const u32 a = random32(), b = random32();
		uid[0] = 0x04;
		uid[1] = (u8)a; uid[2] = (u8)(a >> 8); uid[3] = (u8)(a >> 16);
		uid[4] = (u8)(a >> 24); uid[5] = (u8)b; uid[6] = 0x80;
		DimensionsToypad::create_blank(data, (u16)d.id, uid);
		name = d.name;
		ext = "bin";
		size = kDimFigureSize;
	}

	char folder[96], safe[48];
	figure_folder(kind, folder, sizeof(folder));
	safe_file_name(name, safe, sizeof(safe));
	return create_unique_file(folder, safe, ext, data, size, path, path_size, error);
}

static void action_create(ToyKind kind)
{
	char query[48] = "";
	if (!keyboard(L"Create new figure", L"Type part of the figure's name (for example: spyro, batman, gandalf, sulley).", query, sizeof(query)))
		return;
	s_matches.kind = kind;
	s_matches.count = kind == kToySkylanders ? sky_search(query, s_matches.idx, 600) :
	                  kind == kToyDimensions ? dim_search(query, s_matches.idx, 600) : inf_search(query, s_matches.idx, 600);
	if (!s_matches.count)
	{
		say(L"Create new figure", L"No figure matches that name.");
		return;
	}
	const int i = browse(L"Create new figure", L"Create", s_matches.count, match_label, NULL);
	if (i < 0)
		return;

	char path[200];
	DWORD error = 0;
	if (!create_figure_file(kind, s_matches.idx[i], path, sizeof(path), &error))
	{
		if (error == kCreateNotPossible)
		{
			say(L"Create new figure", L"That piece can't be created.");
			return;
		}
		wchar_t text[300], wpath[200];
		char shown[200];
		display_path(path, shown, sizeof(shown));
		widen(shown, wpath, 200);
		_snwprintf(text, 299, L"Could not write the new file:\n%s\n\nError %u. Check that the drive isn't full and that the folder exists.", wpath, (unsigned)error);
		text[299] = 0;
		say(L"Create new figure", text);
		return;
	}
	wchar_t text[200], wname[64];
	widen(base_name(path), wname, 64);
	_snwprintf(text, 199, L"Created %s\n\nPut it on the %s now?", wname, device_noun(kind));
	text[199] = 0;
	if (ask(L"Create new figure", text, 2, L"Place it", L"Not now", NULL, 0) != 0)
		return;
	int pad = 0;
	if (kind == kToyDimensions && (pad = choose_pad(L"Place figure")) < 0)
		return;
	place_path(kind, path, pad);
}

static void action_more(ToyKind kind)
{
	const wchar_t* toggle = g_config.emulate ? L"Turn emulation off" : L"Turn emulation on";
	const int b = ask(L"Toy Portal 360", L"More options", 3, L"Create new figure",
		kind == kToyDimensions ? L"Move figure" : L"Save figures now", toggle, 0);
	if (b == 0)
		action_create(kind);
	else if (b == 1 && kind == kToyDimensions)
		action_move();
	else if (b == 1)
	{
		flush_dirty_figures();
		notify(L"Figures saved");
	}
	else if (b == 2)
	{
		g_config.emulate = !g_config.emulate;
		notify(g_config.emulate ? L"Portal emulation on" : L"Portal emulation off (real portal)");
	}
}

// The game's portal kind; asks the player if the game hasn't said yet.
ToyKind menu_pick_kind()
{
	EnterCriticalSection(&g_lock);
	ToyKind kind = g_router.kind();
	LeaveCriticalSection(&g_lock);

	if (kind == kToyUnknown)
	{
		const int b = ask(L"Toy Portal 360",
			L"The game has not talked to the portal yet. Which kind is it?", 3,
			L"Skylanders portal", L"Dimensions toy pad", L"Infinity base", 0);
		if (b >= 0)
		{
			kind = b == 0 ? kToySkylanders : b == 1 ? kToyDimensions : kToyInfinity;
			EnterCriticalSection(&g_lock);
			g_router.set_kind_if_unknown(kind);
			kind = g_router.kind();
			LeaveCriticalSection(&g_lock);
		}
	}

	return kind;
}

static DWORD WINAPI menu_thread(LPVOID)
{
	flush_dirty_figures();

	ToyKind kind = menu_pick_kind();

	while (kind != kToyUnknown)
	{
		wchar_t text[400], title[64];
		portal_summary(kind, text, 400);
		_snwprintf(title, 63, L"Toy Portal 360 - %s%s", kind_title(kind),
			g_config.emulate ? L"" : L" (OFF)");
		title[63] = 0;
		const int b = ask(title, text, 3, L"Place figure", L"Remove figure", L"More...", 0);
		if (b < 0)
			break;
		if (b == 0)
			action_place(kind);
		else if (b == 1)
			action_remove(kind);
		else
			action_more(kind);
	}

	g_menu_open = false;
	return 0;
}

static bool combo_pressed(WORD now, WORD before, WORD combo)
{
	return combo && (now & combo) == combo && (before & combo) != combo;
}

// Called by the watcher thread every tick: LB+RB+Back opens the menu,
// LB+RB+Y the character grid (toyportal.ini: combo, grid_combo).
void menu_poll()
{
	static WORD s_prev[4] = {0, 0, 0, 0};
	for (DWORD u = 0; u < 4; u++)
	{
		XINPUT_STATE st;
		if (XInputGetState(u, &st) != ERROR_SUCCESS)
		{
			s_prev[u] = 0;
			continue;
		}
		const WORD now = st.Gamepad.wButtons;
		const bool grid = combo_pressed(now, s_prev[u], g_config.grid_combo);
		const bool menu = !grid && combo_pressed(now, s_prev[u], g_config.combo);
		s_prev[u] = now;
		if ((!grid && !menu) || g_menu_open)
			continue;

		if (!g_title_hooked)
		{
			notify(L"Toy Portal 360: no toy game is running");
			continue;
		}
		g_menu_user = u;
		g_menu_open = true;
		if (grid)
		{
			if (!grid_start())
				g_menu_open = false;
			continue;
		}
#ifdef TOYPORTAL_SIDELOAD
		const bool system_thread = false; // the game owns this DLL's threads
#else
		const bool system_thread = true;
#endif
		if (!tp_start_thread(menu_thread, NULL, NULL, system_thread))
			g_menu_open = false;
	}
}
