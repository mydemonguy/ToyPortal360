// ToyPortal360 - in-memory log ring, written to <root>\log.txt by the
// watcher thread so the game's own thread never waits on the disk.
#include "plugin.h"
#include <stdio.h>
#include <stdarg.h>

enum { kLogLines = 256, kLogLineLen = 200 };

static char s_lines[kLogLines][kLogLineLen];
static int s_head = 0, s_count = 0;
static CRITICAL_SECTION s_log_lock;
static bool s_init = false;

static void ensure_init()
{
	if (!s_init)
	{
		InitializeCriticalSection(&s_log_lock);
		s_init = true;
	}
}

void log_line(const char* fmt, ...)
{
	ensure_init();
	char line[kLogLineLen];
	const int t = _snprintf(line, sizeof(line), "[%08u] ", GetTickCount());
	va_list args;
	va_start(args, fmt);
	_vsnprintf(line + t, sizeof(line) - t - 1, fmt, args);
	va_end(args);
	line[kLogLineLen - 1] = 0;

	EnterCriticalSection(&s_log_lock);
	if (s_count == kLogLines) // full: drop the oldest line
	{
		s_head = (s_head + 1) % kLogLines;
		s_count--;
	}
	strcpy(s_lines[(s_head + s_count) % kLogLines], line);
	s_count++;
	LeaveCriticalSection(&s_log_lock);
}

void log_packet(const char* tag, DWORD device, const BYTE* buf, DWORD len, DWORD result)
{
	char hex[0x20 * 3 + 1];
	int p = 0;
	for (DWORD i = 0; i < len && i < 0x20; i++)
		p += _snprintf(hex + p, sizeof(hex) - p, "%02X ", buf[i]);
	hex[p] = 0;
	log_line("%-5s dev=%u len=%u res=%08X %s", tag, device, len, result, hex);
}

void log_flush()
{
	ensure_init();
	if (!g_root[0])
		return;

	static char pending[kLogLines][kLogLineLen];
	int n = 0;
	EnterCriticalSection(&s_log_lock);
	while (s_count)
	{
		strcpy(pending[n++], s_lines[s_head]);
		s_head = (s_head + 1) % kLogLines;
		s_count--;
	}
	LeaveCriticalSection(&s_log_lock);
	if (!n)
		return;

	char path[128];
	_snprintf(path, sizeof(path), "%s\\log.txt", g_root);
	HANDLE f = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return;
	SetFilePointer(f, 0, NULL, FILE_END);
	for (int i = 0; i < n; i++)
	{
		DWORD written;
		WriteFile(f, pending[i], (DWORD)strlen(pending[i]), &written, NULL);
		WriteFile(f, "\r\n", 2, &written, NULL);
	}
	CloseHandle(f);
}
