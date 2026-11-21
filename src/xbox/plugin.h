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
	WORD grid_combo;       // ... and the character grid (0 = off)
	bool grid_small;       // draw the grid at 1x (faster, smaller)
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
bool create_unique_file(const char* folder, const char* base, const char* ext, const BYTE* data, DWORD size, char* out_path, int out_size, DWORD* error);
void figure_folder(ToyKind kind, char* out, int out_size);
// TPHDD:\ToyPortal\Pictures\<game>: the player's own pictures for the grid.
void picture_folder(ToyKind kind, char* out, int out_size);
// Reads up to `max` bytes. Returns how many, max + 1 if the file is longer,
// or kNoFile if it can't be opened or read.
static const DWORD kNoFile = 0xFFFFFFFF;
DWORD read_small_file(const char* path, BYTE* out, DWORD max);
DWORD figure_size(ToyKind kind);
// TPHDD:\x -> Hdd:\x etc., for showing paths to the player.
void display_path(const char* in, char* out, int out_size);
void flush_dirty_figures();

// hooks.cpp
bool hooks_resolve();
int hooks_patch_title(PLDR_DATA_TABLE_ENTRY title);
void ppc_write_jump(DWORD* at, DWORD destination); // exposed for tests
PVOID hooks_resolve_export(const char* module, DWORD ordinal);
// Points every import of `real` in the module at `hook`; returns how many.
int hooks_patch_import(PLDR_DATA_TABLE_ENTRY module, PVOID real, PVOID hook);

// menu.cpp
void menu_poll();
void notify(const wchar_t* text);
extern volatile bool g_menu_open; // the menu or the grid is showing
extern DWORD g_menu_user;         // the controller that opened it
void menu_say(const wchar_t* title, const wchar_t* text);
ToyKind menu_pick_kind();
enum { kCreateNotPossible = 0xFFFFFFFE };
bool create_figure_file(ToyKind kind, int table_index, char* path, int path_size, DWORD* error);

// grid.cpp - the on-screen character grid
void grid_hooks_resolve();
int grid_patch_module(PLDR_DATA_TABLE_ENTRY m);
PVOID grid_swap_lookup(PVOID address);
bool grid_start();

#endif
