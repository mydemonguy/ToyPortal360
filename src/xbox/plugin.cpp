// ToyPortal360 - plugin entry point.
//
// When the running game imports XAM's non-controller raw input functions
// (which 360 toy games use to reach their portal), its imports are
// redirected to the emulated Skylanders portal / LEGO Dimensions toy pad /
// Disney Infinity base in src/core.
//
// Two ways to load it:
// - DashLaunch plugin (default): a system DLL loaded at boot. A system
//   thread watches for game launches and hooks each toy game.
// - Peer Pressure sideload (TOYPORTAL_SIDELOAD): a title DLL that Peer
//   Pressure loads into one game, after the game is in memory and before
//   its code runs. It hooks that game straight away from DllMain, runs
//   ordinary game threads, and is unloaded when the game exits.
#include "plugin.h"
#include <stdio.h>

Config g_config;
ToyRouter g_router;
CRITICAL_SECTION g_lock;
volatile bool g_title_hooked = false;
volatile DWORD g_title_id = 0;
volatile bool g_title_unsupported = false;

static volatile bool s_quit = false;

// XamGetCurrentTitleId lives in xam.xex, which the XDK import libraries
// don't always expose, so look it up by ordinal.
#ifdef TOYPORTAL_FORGE
extern "C" DWORD XamGetCurrentTitleId(VOID); // Xenon Forge links it by ordinal
static DWORD current_title_id() { return XamGetCurrentTitleId(); }
#else
static DWORD current_title_id()
{
	typedef DWORD (*fn_t)(VOID);
	static fn_t fn = NULL;
	if (!fn)
	{
		HANDLE h = NULL;
		if (XexGetModuleHandle((PSZ)"xam.xex", &h) == 0 && h)
			XexGetProcedureAddress(h, kOrdXamGetCurrentTitleId, (PVOID)&fn);
	}
	return fn ? fn() : 0;
}

extern "C" bool tp_start_thread(LPTHREAD_START_ROUTINE fn, LPVOID arg, HANDLE* out_handle, bool system_thread)
{
	HANDLE h = NULL;
	DWORD id = 0;
	// 0x2 = system thread, which survives title changes.
	ExCreateThread(&h, 0, &id, (PVOID)XapiThreadStartup, fn, arg, system_thread ? 0x2 : 0);
	if (!h)
		return false;
	if (out_handle)
		*out_handle = h;
	else
		CloseHandle(h);
	return true;
}
#endif
static HANDLE s_thread = NULL;

// Start-up markers (see src/forge/breadcrumb.c); compiled out unless enabled.
#ifdef TOYPORTAL_BREADCRUMBS
extern "C" void tp_crumb(const char* tag, unsigned value);
extern "C" void tp_dump_imports(void* exe, const unsigned* wanted);
extern "C" void hooks_wanted(unsigned out[4]);
#define CRUMB(tag, value) tp_crumb(tag, (unsigned)(value))
#else
#define CRUMB(tag, value) ((void)0)
#endif

static void apply_config_to_router()
{
	g_router.status_interval_ms = g_config.status_interval_ms;
	g_router.sky_wrap_len = g_config.sky_wrap_len;
}

static bool hook_title(PLDR_DATA_TABLE_ENTRY title, DWORD title_id)
{
	if (!title)
		return false;
	const int patched = hooks_patch_title(title);
	CRUMB("04-PATCHED", patched);
	if (patched <= 0 && !g_toy_lookup_seen)
		return false;
	g_title_id = title_id;
	g_title_hooked = true;
	if (patched > 0)
		log_line("Title %08X: hooked %d portal import(s), mode=%s", title_id, patched,
			g_config.emulate ? "emulate" : "passthrough");
	else
		log_line("Title %08X: hooked the portal functions it looked up, mode=%s", title_id,
			g_config.emulate ? "emulate" : "passthrough");
	CRUMB("04B-HOOKED", patched);
	return true;
}

static void notify_ready()
{
	CRUMB("09-NOTIFY", g_config.emulate);
	if (g_config.emulate)
		notify(g_config.combo == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK) ?
			L"Toy Portal 360 ready - hold LB+RB+Back for the menu" :
			L"Toy Portal 360 ready - use your menu buttons");
}

static PLDR_DATA_TABLE_ENTRY executable_module()
{
	return XexExecutableModuleHandle ? *XexExecutableModuleHandle : NULL;
}

// Shared tail of both loops: the menu, and every ~500 ms saving figures the
// game changed and the log.
static void service_tick(DWORD* ticks)
{
	menu_poll();
	if (++*ticks % 10 == 0)
	{
		flush_dirty_figures();
		log_flush();
	}
	Sleep(50);
}

#ifndef TOYPORTAL_SIDELOAD

static void on_title_change(PLDR_DATA_TABLE_ENTRY title, DWORD title_id)
{
	// Save anything the previous game wrote before forgetting it.
	flush_dirty_figures();
	config_load(); // pick up toyportal.ini edits without a reboot

	EnterCriticalSection(&g_lock);
	g_title_hooked = false;
	g_title_unsupported = false;
	g_toy_lookup_seen = false;
	g_router.reset(g_config.device);
	apply_config_to_router();
	LeaveCriticalSection(&g_lock);

	g_title_id = title_id;
	if (hook_title(title, title_id))
		notify_ready();
}

static DWORD WINAPI watcher_thread(LPVOID)
{
	// Give the dashboard and storage devices a moment on boot.
	Sleep(3000);
	storage_init();
	config_load();
	log_line("ToyPortal360 started (DashLaunch plugin), files in %s", g_root);
	if (!hooks_resolve())
		log_line("XAM portal functions not found - this dashboard version is not supported");

	PLDR_DATA_TABLE_ENTRY last = NULL;
	DWORD last_id = 0, last_stamp = 0;
	DWORD ticks = 0;
	while (!s_quit)
	{
		// The loader can reuse the same entry address for the next game, so
		// compare the title ID and image timestamp as well.
		PLDR_DATA_TABLE_ENTRY now = executable_module();
		const DWORD id = current_title_id();
		const DWORD stamp = now ? now->TimeDateStamp : 0;
		if (now != last || id != last_id || stamp != last_stamp)
		{
			last = now;
			last_id = id;
			last_stamp = stamp;
			on_title_change(now, id);
		}
		// A game that looks the portal functions up while running counts
		// as hooked once it has (only lookups made after the hook was in place).
		else if (!g_title_hooked && g_toy_lookup_seen && hook_title(now, id))
			notify_ready();
		service_tick(&ticks);
	}

	flush_dirty_figures();
	log_flush();
	return 0;
}

extern "C" BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID)
{
	(void)module;
	if (reason == DLL_PROCESS_ATTACH)
	{
		InitializeCriticalSection(&g_lock);
		tp_start_thread(watcher_thread, NULL, &s_thread, true);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		s_quit = true;
		if (s_thread)
		{
			WaitForSingleObject(s_thread, 2000);
			CloseHandle(s_thread);
		}
	}
	return TRUE;
}

#else // TOYPORTAL_SIDELOAD

static DWORD WINAPI sideload_thread(LPVOID)
{
	// The game is already hooked; now the slower set-up that touches disk.
	CRUMB("06-THREAD", 0);
	storage_init();
	CRUMB("07-STORAGE", 0);
#ifdef TOYPORTAL_BREADCRUMBS
	{
		unsigned wanted[4];
		hooks_wanted(wanted);
		tp_dump_imports(executable_module(), wanted);
		CRUMB("07B-DUMPED", 0);
	}
#endif
	config_load();
	CRUMB("08-CONFIG", g_config.emulate | (g_config.device << 4));
	EnterCriticalSection(&g_lock);
	g_router.set_kind_if_unknown(g_config.device); // never reset: the game may be talking already
	apply_config_to_router();
	LeaveCriticalSection(&g_lock);
	log_line("ToyPortal360 started (Peer Pressure sideload) in title %08X, files in %s", current_title_id(), g_root);

	if (g_title_hooked)
		notify_ready();
	CRUMB("10-RUNNING", g_title_hooked);

	DWORD ticks = 0;
	while (!s_quit)
	{
		if (ticks == 200) // about 10 s in
			CRUMB("11-ALIVE", g_title_hooked);
		// In case the executable's module handle was not set yet in DllMain.
		if (!g_title_hooked && hook_title(executable_module(), current_title_id()))
			notify_ready();
		service_tick(&ticks);
	}

	flush_dirty_figures();
	log_flush();
	return 0;
}

extern "C" BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID)
{
	(void)module;
	if (reason == DLL_PROCESS_ATTACH)
	{
		CRUMB("01-DLLMAIN", 0);
		InitializeCriticalSection(&g_lock);
		config_defaults();
		// Memory only, so it is safe here: hook the game before its code runs.
		const bool resolved = hooks_resolve();
		CRUMB("02-RESOLVED", resolved);
		CRUMB("03-EXE", (DWORD)executable_module());
		if (resolved)
			hook_title(executable_module(), current_title_id());
		const bool started = tp_start_thread(sideload_thread, NULL, &s_thread, false);
		CRUMB("05-STARTED", started);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		// The game is exiting and this DLL is about to be unloaded: stop the
		// thread (it saves figures on the way out) before the code goes away.
		s_quit = true;
		if (s_thread)
		{
			WaitForSingleObject(s_thread, 5000);
			CloseHandle(s_thread);
		}
	}
	return TRUE;
}

#endif
