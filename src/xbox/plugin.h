// ToyPortal360 - shared state between the hook, watcher and menu code.
#ifndef TOYPORTAL_PLUGIN_H
#define TOYPORTAL_PLUGIN_H

#include "kernel.h"
#include "../core/toy_router.h"

struct Config
{
	bool emulate;          // false = pass calls through to a real portal
	bool log_traffic;      // write every packet to log.txt
	ToyKind device;        // kToyUnknown = auto-detect
	WORD combo;            // XINPUT_GAMEPAD_* buttons held together to open the menu
	DWORD status_interval_ms;
	DWORD idle_result;     // XAM result returned when there is nothing to read
	DWORD sky_wrap_len;    // length byte for wrapped Skylanders replies (0 = auto)
};

extern Config g_config;
extern ToyRouter g_router;
extern CRITICAL_SECTION g_lock;
extern volatile bool g_title_hooked;
extern volatile bool g_toy_lookup_seen; // hooks.cpp: the game looked up a toy-pad function
extern volatile DWORD g_title_id;
// Set when the game speaks a protocol we don't emulate (e.g. Disney Infinity):
// its calls then go straight to the real XAM so a real base keeps working.
extern volatile bool g_title_unsupported;
extern char g_root[64];    // e.g. "TPHDD:\\ToyPortal"

// log.cpp - buffered so hooks never touch the disk on the game's thread.
void log_line(const char* fmt, ...);
void log_packet(const char* tag, DWORD device, const BYTE* buf, DWORD len, DWORD result);
void log_flush();

// storage.cpp
void storage_init();
void config_defaults();
void config_load();
int list_figure_files(ToyKind kind, const char* filter, char (*names)[48], int max);
bool read_figure(const char* path, BYTE* out, DWORD size);
bool write_figure(const char* path, const BYTE* data, DWORD size);
bool create_unique_file(const char* folder, const char* base, const char* ext, const BYTE* data, DWORD size, char* out_path, int out_size);
void figure_folder(ToyKind kind, char* out, int out_size);
DWORD figure_size(ToyKind kind);
// TPHDD:\x -> Hdd:\x etc., for showing paths to the player.
void display_path(const char* in, char* out, int out_size);
void flush_dirty_figures();

// hooks.cpp
bool hooks_resolve();
int hooks_patch_title(PLDR_DATA_TABLE_ENTRY title);
void ppc_write_jump(DWORD* at, DWORD destination); // exposed for tests

// menu.cpp
void menu_poll();
void notify(const wchar_t* text);

#endif
