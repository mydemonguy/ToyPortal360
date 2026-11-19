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

int hooks_patch_title(PLDR_DATA_TABLE_ENTRY title)
{
	int n = 0;
	n += patch_import(title, (PVOID)s_real_get, HOOK_ENTRY(tp_entry_get_raw, HookGetRaw));
	n += patch_import(title, (PVOID)s_real_set, HOOK_ENTRY(tp_entry_set_raw, HookSetRaw));
	n += patch_import(title, (PVOID)s_real_get_ex, HOOK_ENTRY(tp_entry_get_raw_ex, HookGetRawEx));
	n += patch_import(title, (PVOID)s_real_set_ex, HOOK_ENTRY(tp_entry_set_raw_ex, HookSetRawEx));
	// Not counted: the title is only a toy game once it looks one up.
	patch_import(title, (PVOID)s_real_get_proc, HOOK_ENTRY(tp_entry_get_proc, HookGetProcAddress));
	return n;
}
