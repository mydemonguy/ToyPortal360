// ToyPortal360 - replaces the game's imports of XAM's "non-controller raw"
// input functions. Xbox 360 toy games talk to their portal through these:
// Xenia implements exactly these four exports to support the 360 Skylanders
// portal. Parameter order follows Xenia's xam_input.cc.
#include "plugin.h"
#include <ppcintrinsics.h>

typedef DWORD (*GetRaw_t)(WORD* state, DWORD* length, BYTE* buffer);
typedef DWORD (*SetRaw_t)(DWORD length, BYTE* buffer);
typedef DWORD (*GetRawEx_t)(DWORD device, BYTE* buffer, DWORD* length, WORD* state);
typedef DWORD (*SetRawEx_t)(DWORD device, BYTE* buffer, DWORD length);

// Calling the real XAM functions. Under Xenon Forge (SysV code calling
// console code by address) the call goes through the tp_ccall trampoline.
#ifdef TOYPORTAL_FORGE
#define CALL_REAL4(fn, a, b, c, d) tp_ccall((PVOID)(fn), (DWORD)(a), (DWORD)(b), (DWORD)(c), (DWORD)(d), 0, 0, 0)
#define CALL_REAL3(fn, a, b, c) tp_ccall((PVOID)(fn), (DWORD)(a), (DWORD)(b), (DWORD)(c), 0, 0, 0, 0)
#define CALL_REAL2(fn, a, b) tp_ccall((PVOID)(fn), (DWORD)(a), (DWORD)(b), 0, 0, 0, 0, 0)
// The game reaches the hooks through assembly entries that keep the
// console's 64-bit registers (src/forge/forge.S).
extern "C" DWORD tp_entry_get_raw(WORD*, DWORD*, BYTE*);
extern "C" DWORD tp_entry_set_raw(DWORD, BYTE*);
extern "C" DWORD tp_entry_get_raw_ex(DWORD, BYTE*, DWORD*, WORD*);
extern "C" DWORD tp_entry_set_raw_ex(DWORD, BYTE*, DWORD);
extern "C" DWORD tp_entry_get_proc(HANDLE, DWORD, PVOID*);
#define HOOK_ENTRY(forge_entry, hook) ((PVOID)forge_entry)
#else
#define CALL_REAL4(fn, a, b, c, d) fn(a, b, c, d)
#define CALL_REAL3(fn, a, b, c) fn(a, b, c)
#define CALL_REAL2(fn, a, b) fn(a, b)
#define HOOK_ENTRY(forge_entry, hook) ((PVOID)hook)
#endif

static GetRaw_t s_real_get = NULL;
static SetRaw_t s_real_set = NULL;
static GetRawEx_t s_real_get_ex = NULL;
static SetRawEx_t s_real_set_ex = NULL;

// LEGO Dimensions does not import the four functions: it looks them up while
// running with XexGetProcedureAddress (its import list, dumped on a console,
// has none of them). So the game's import of XexGetProcedureAddress is hooked
// too, and a lookup that returns one of the four gets the hook instead.
typedef DWORD (*GetProc_t)(HANDLE module, DWORD ordinal, PVOID* address);
static GetProc_t s_real_get_proc = NULL;
volatile bool g_toy_lookup_seen = false; // the game looked up a toy-pad function

static const DWORD kDefaultDevice = 5; // what the non-Ex functions use
static const DWORD kMaxPacket = 0x20;

static bool emulating()
{
	return g_config.emulate && g_title_hooked && !g_title_unsupported;
}

static const DWORD kNotOurs = 0xFFFFFFFF; // emu_write: forward to the real XAM

// ---- shared implementation --------------------------------------------------

static DWORD emu_read(DWORD device, BYTE* buffer, DWORD* length, WORD* state)
{
	if (!buffer || !length || !state || *length == 0 || *length > kMaxPacket)
		return ERROR_INVALID_PARAMETER;

	EnterCriticalSection(&g_lock);
	const u32 n = g_router.on_read(buffer, *length, GetTickCount());
	LeaveCriticalSection(&g_lock);

	if (n == 0)
	{
		*length = 0;
		*state = 1;
		return g_config.idle_result;
	}
	*length = n;
	*state = 1; // Xenia reports 1 while the device stays connected
	if (g_config.log_traffic)
		log_packet("emu<", device, buffer, n, ERROR_SUCCESS);
	return ERROR_SUCCESS;
}

static DWORD emu_write(DWORD device, BYTE* buffer, DWORD length)
{
	if (!buffer || length == 0 || length > kMaxPacket)
		return ERROR_INVALID_PARAMETER;

	EnterCriticalSection(&g_lock);
	const ToyKind before = g_router.kind();
	const bool ok = g_router.on_write(buffer, length);
	const ToyKind after = g_router.kind();
	LeaveCriticalSection(&g_lock);

	if (!ok && after == kToyUnknown)
	{
		// Not a Skylanders portal or Dimensions toy pad command.
		g_title_unsupported = true;
		log_line("Unrecognised portal protocol (first byte %02X) - passing this game through to the real hardware", buffer[0]);
		log_packet("emu>?", device, buffer, length, 0);
		return kNotOurs;
	}

	if (g_config.log_traffic)
		log_packet(ok ? "emu>" : "emu>?", device, buffer, length, ERROR_SUCCESS);
	if (before != after)
		log_line("Detected %s (device %u, frames %s, prefix %02X)", toy_kind_name(after), device,
			g_router.wrapped() ? "wrapped" : "bare", g_router.wrap_prefix());
	return ERROR_SUCCESS;
}

// ---- hooks ------------------------------------------------------------------

extern "C" DWORD HookGetRaw(WORD* state, DWORD* length, BYTE* buffer)
{
	if (emulating())
		return emu_read(kDefaultDevice, buffer, length, state);
	const DWORD r = CALL_REAL3(s_real_get, state, length, buffer);
	if (g_config.log_traffic && r == ERROR_SUCCESS && length && *length)
		log_packet("real<", kDefaultDevice, buffer, *length, r);
	return r;
}

extern "C" DWORD HookSetRaw(DWORD length, BYTE* buffer)
{
	if (emulating())
	{
		const DWORD e = emu_write(kDefaultDevice, buffer, length);
		if (e != kNotOurs)
			return e;
	}
	const DWORD r = CALL_REAL2(s_real_set, length, buffer);
	if (g_config.log_traffic)
		log_packet("real>", kDefaultDevice, buffer, length, r);
	return r;
}

extern "C" DWORD HookGetRawEx(DWORD device, BYTE* buffer, DWORD* length, WORD* state)
{
	if (emulating())
		return emu_read(device, buffer, length, state);
	const DWORD r = CALL_REAL4(s_real_get_ex, device, buffer, length, state);
	if (g_config.log_traffic && r == ERROR_SUCCESS && length && *length)
		log_packet("real<", device, buffer, *length, r);
	return r;
}

extern "C" DWORD HookSetRawEx(DWORD device, BYTE* buffer, DWORD length)
{
	if (emulating())
	{
		const DWORD e = emu_write(device, buffer, length);
		if (e != kNotOurs)
			return e;
	}
	const DWORD r = CALL_REAL3(s_real_set_ex, device, buffer, length);
	if (g_config.log_traffic)
		log_packet("real>", device, buffer, length, r);
	return r;
}

// ---- import patching --------------------------------------------------------

static PVOID resolve(const char* module, DWORD ordinal)
{
	HANDLE h = NULL;
	PVOID addr = NULL;
	if (XexGetModuleHandle((PSZ)module, &h) != 0 || !h)
		return NULL;
	if (XexGetProcedureAddress(h, ordinal, &addr) != 0)
		return NULL;
	return addr;
}

extern "C" DWORD HookGetProcAddress(HANDLE module, DWORD ordinal, PVOID* address)
{
	const DWORD st = CALL_REAL3(s_real_get_proc, module, ordinal, address);
	// The game's run-time lookups in XAM (where the portal functions live) go
	// to log.txt: they show how a game reaches the portal.
	static HANDLE s_xam = NULL;
	static int s_logged = 0;
	if (!s_xam)
		XexGetModuleHandle((PSZ)"xam.xex", &s_xam);
	if (module == s_xam && s_logged < 100)
	{
		s_logged++;
		log_line("Game looked up XAM ordinal %03X (status %08X)", (unsigned)ordinal, (unsigned)st);
	}
	if (st != 0 || !address || !*address)
		return st;
	PVOID hook = NULL;
	if (*address == (PVOID)s_real_get) hook = HOOK_ENTRY(tp_entry_get_raw, HookGetRaw);
	else if (*address == (PVOID)s_real_set) hook = HOOK_ENTRY(tp_entry_set_raw, HookSetRaw);
	else if (*address == (PVOID)s_real_get_ex) hook = HOOK_ENTRY(tp_entry_get_raw_ex, HookGetRawEx);
	else if (*address == (PVOID)s_real_set_ex) hook = HOOK_ENTRY(tp_entry_set_raw_ex, HookSetRawEx);
	if (hook)
	{
		*address = hook;
		g_toy_lookup_seen = true;
	}
	return st;
}

bool hooks_resolve()
{
	s_real_get = (GetRaw_t)resolve("xam.xex", kOrdXamInputNonControllerGetRaw);
	s_real_set = (SetRaw_t)resolve("xam.xex", kOrdXamInputNonControllerSetRaw);
	s_real_get_ex = (GetRawEx_t)resolve("xam.xex", kOrdXamInputNonControllerGetRawEx);
	s_real_set_ex = (SetRawEx_t)resolve("xam.xex", kOrdXamInputNonControllerSetRawEx);
	s_real_get_proc = (GetProc_t)resolve("xboxkrnl.exe", kOrdXexGetProcedureAddress);
	log_line("XAM: GetRaw=%p SetRaw=%p GetRawEx=%p SetRawEx=%p", s_real_get, s_real_set, s_real_get_ex, s_real_set_ex);
	return s_real_get && s_real_set;
}

static void flush_code(void* p, DWORD size)
{
	for (DWORD i = 0; i < size; i += 4)
		__dcbst(i, p);
	__sync();
	for (DWORD i = 0; i < size; i += 4)
		__icbi(i, p);
	__isync();
}

// lis r11, hi ; addi r11, r11, lo ; mtctr r11 ; bctr
void ppc_write_jump(DWORD* at, DWORD destination)
{
	DWORD hi = (destination >> 16) & 0xFFFF;
	if (destination & 0x8000)
		hi += 1;
	at[0] = 0x3D600000 | hi;
	at[1] = 0x396B0000 | (destination & 0xFFFF);
	at[2] = 0x7D6903A6;
	at[3] = 0x4E800420;
	flush_code(at, 16);
}

// The address a call stub jumps to, if it holds the loader's import stub:
// lis r11, hi ; ori r11, r11, lo (or addi) ; mtctr r11 ; bctr. Otherwise 0.
static DWORD thunk_target(const DWORD* w)
{
	if ((w[0] & 0xFFFF0000) != 0x3D600000 || w[2] != 0x7D6903A6 || w[3] != 0x4E800420)
		return 0;
	const DWORD hi = (w[0] & 0xFFFF) << 16;
	if ((w[1] & 0xFFFF0000) == 0x616B0000) // ori r11, r11, lo
		return hi | (w[1] & 0xFFFF);
	if ((w[1] & 0xFFFF0000) == 0x396B0000) // addi r11, r11, lo
		return hi + (DWORD)(int)(short)(w[1] & 0xFFFF);
	return 0;
}

// Redirects every import of `real` in the title to `hook`. A function import
// is usually a pointer the loader fills in followed by the call stub the
// game branches to; some titles have only the stub, which the loader fills
// with a jump to the function. Both are recognised.
static int patch_import(PLDR_DATA_TABLE_ENTRY module, PVOID real, PVOID hook)
{
	if (!real || !module || !module->XexHeaderBase)
		return 0;
	PXEX_IMPORT_DESCRIPTOR desc = (PXEX_IMPORT_DESCRIPTOR)RtlImageXexHeaderField(module->XexHeaderBase, XEX_HEADER_IMPORTS);
	if (!desc)
		return 0;

	int patched = 0;
	char* names = (char*)(desc + 1);
	PXEX_IMPORT_TABLE table = (PXEX_IMPORT_TABLE)(names + desc->NameTableSize);
	for (DWORD m = 0; m < desc->ModuleCount; m++)
	{
		DWORD* records = (DWORD*)(table + 1);
		const DWORD count = table->ImportTable.ImportCount;
		for (DWORD i = 0; i < count; i++)
		{
			DWORD* rec = (DWORD*)records[i];
			if (i + 1 < count && *rec == (DWORD)real)
			{
				*rec = (DWORD)hook;
				flush_code(rec, 4);
				// The next record is the call stub the game branches to.
				ppc_write_jump((DWORD*)records[i + 1], (DWORD)hook);
				patched++;
				i++;
			}
			else if ((*rec >> 16) == 0x3D60 && thunk_target(rec) == (DWORD)real)
			{
				ppc_write_jump(rec, (DWORD)hook);
				patched++;
			}
		}
		table = (PXEX_IMPORT_TABLE)((BYTE*)table + table->TableSize);
	}
	return patched;
}

// The XAM addresses the hooks look for (for the start-up import dump).
extern "C" void hooks_wanted(unsigned out[4])
{
	out[0] = (unsigned)s_real_get;
	out[1] = (unsigned)s_real_set;
	out[2] = (unsigned)s_real_get_ex;
	out[3] = (unsigned)s_real_set_ex;
}

static int patch_module(PLDR_DATA_TABLE_ENTRY m)
{
	int n = 0;
	n += patch_import(m, (PVOID)s_real_get, HOOK_ENTRY(tp_entry_get_raw, HookGetRaw));
	n += patch_import(m, (PVOID)s_real_set, HOOK_ENTRY(tp_entry_set_raw, HookSetRaw));
	n += patch_import(m, (PVOID)s_real_get_ex, HOOK_ENTRY(tp_entry_get_raw_ex, HookGetRawEx));
	n += patch_import(m, (PVOID)s_real_set_ex, HOOK_ENTRY(tp_entry_set_raw_ex, HookSetRawEx));
	// Not counted: the title is only a toy game once it looks one up.
	patch_import(m, (PVOID)s_real_get_proc, HOOK_ENTRY(tp_entry_get_proc, HookGetProcAddress));
	return n;
}

// A loaded module of the running game other than the executable: a title
// image (0x82000000-0x8FFFFFFF) with a real XEX header, and not this plugin.
static bool is_other_game_module(PLDR_DATA_TABLE_ENTRY m, PLDR_DATA_TABLE_ENTRY title)
{
	if (m == title || !MmIsAddressValid(m) || !MmIsAddressValid((BYTE*)m + 0x5C))
		return false;
	const DWORD base = (DWORD)m->ImageBase;
	if (base < 0x82000000 || base >= 0x90000000)
		return false;
	const DWORD self = (DWORD)(void*)&patch_module;
	if (self >= base && self - base < m->SizeOfNtImage)
		return false;
	const DWORD* header = (const DWORD*)m->XexHeaderBase;
	return header && MmIsAddressValid((PVOID)header) && header[0] == 0x58455832; // "XEX2"
}

// Logs each game module the first time it is seen (log.txt shows which of the
// game's modules exist and which one imports the portal functions).
static void note_module(PLDR_DATA_TABLE_ENTRY m, int patched)
{
	static PLDR_DATA_TABLE_ENTRY s_seen[32];
	static int s_count = 0;
	for (int i = 0; i < s_count; i++)
		if (s_seen[i] == m)
			return;
	if (s_count < 32)
		s_seen[s_count++] = m;
	char name[64];
	int len = 0;
	const PWSTR w = m->BaseDllName.Buffer;
	const int chars = m->BaseDllName.Length / 2;
	if (w && MmIsAddressValid((PVOID)w))
		for (; len < chars && len < 63; len++)
			name[len] = (char)(w[len] < 128 ? w[len] : '?');
	name[len] = 0;
	log_line("Game module %s at %p (%u bytes): %d portal import(s) patched", len ? name : "?",
		m->ImageBase, (unsigned)m->SizeOfNtImage, patched);
}

// Patches the game's executable and every other module of the game that is
// loaded: a game can load more of its code after it starts, and the loader
// links those modules to the portal functions itself, past both other hooks.
// Called again until the game is hooked, so modules loaded later are covered.
int hooks_patch_title(PLDR_DATA_TABLE_ENTRY title)
{
	if (!title)
		return 0;
	int n = patch_module(title);
	note_module(title, n);
	LIST_ENTRY* head = &title->InLoadOrderLinks;
	int guard = 0;
	for (LIST_ENTRY* e = head->Flink; e && e != head && guard < 256; e = e->Flink, guard++)
	{
		if (!MmIsAddressValid(e))
			break;
		PLDR_DATA_TABLE_ENTRY m = (PLDR_DATA_TABLE_ENTRY)e; // InLoadOrderLinks is first
		if (is_other_game_module(m, title))
		{
			const int k = patch_module(m);
			note_module(m, k);
			n += k;
		}
	}
	return n;
}
