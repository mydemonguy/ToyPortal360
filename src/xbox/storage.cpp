// ToyPortal360 - figure files and settings on the console's HDD or USB.
//
// Layout (first one found wins: HDD, then USB 0, then USB 1):
//   ToyPortal\toyportal.ini
//   ToyPortal\Skylanders\*.sky   (1024-byte dumps; .bin/.dmp also accepted)
//   ToyPortal\Dimensions\*.bin   (180-byte NTAG213 dumps)
//   ToyPortal\Infinity\*.bin     (320-byte Disney Infinity dumps)
//   ToyPortal\log.txt
#include "plugin.h"
#include <stdio.h>

char g_root[64] = "";
static CRITICAL_SECTION s_flush_lock;

// Creates the drive name in both namespaces: \System??\ (where a DashLaunch
// system plugin's links live) and \??\ (what file calls made from inside a
// game look up: RB3Enhanced, a working Peer Pressure sideload, uses it, and a
// console run of the sideload build could not write files with only the
// first). Either call fails harmlessly if the link already exists.
static void make_link_in(const char* ns, const char* drive, const char* device)
{
	char link[64];
	_snprintf(link, sizeof(link), "%s%s", ns, drive);
	TP_ANSI_STRING l, d;
	l.Buffer = link;
	l.Length = (USHORT)strlen(link);
	l.MaximumLength = l.Length + 1;
	d.Buffer = (PCHAR)device;
	d.Length = (USHORT)strlen(device);
	d.MaximumLength = d.Length + 1;
	ObCreateSymbolicLink(&l, &d);
}

static void make_link(const char* drive, const char* device)
{
	make_link_in("\\??\\", drive, device);
	make_link_in("\\System??\\", drive, device);
}

static bool dir_exists(const char* path)
{
	const DWORD a = GetFileAttributes(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void storage_init()
{
	InitializeCriticalSection(&s_flush_lock);
	// Private drive names so we never clash with links other plugins make.
	make_link("TPHDD:", "\\Device\\Harddisk0\\Partition1");
	make_link("TPUSB0:", "\\Device\\Mass0");
	make_link("TPUSB1:", "\\Device\\Mass1");

	static const char* roots[] = {"TPHDD:\\ToyPortal", "TPUSB0:\\ToyPortal", "TPUSB1:\\ToyPortal"};
	for (int i = 0; i < 3; i++)
	{
		if (dir_exists(roots[i]))
		{
			strcpy(g_root, roots[i]);
			break;
		}
	}
	if (!g_root[0])
	{
		// First run: create the folders on the HDD.
		strcpy(g_root, roots[0]);
		CreateDirectory(g_root, NULL);
	}
	char sub[96];
	_snprintf(sub, sizeof(sub), "%s\\Skylanders", g_root);
	CreateDirectory(sub, NULL);
	_snprintf(sub, sizeof(sub), "%s\\Dimensions", g_root);
	CreateDirectory(sub, NULL);
	_snprintf(sub, sizeof(sub), "%s\\Infinity", g_root);
	CreateDirectory(sub, NULL);
	// Empty folders for the player's own pictures (see grid.cpp), so it's
	// clear where they go. Toy Portal 360 ships no pictures.
	_snprintf(sub, sizeof(sub), "%s\\Pictures", g_root);
	CreateDirectory(sub, NULL);
	static const char* const kGames[3] = {"Skylanders", "Dimensions", "Infinity"};
	for (int i = 0; i < 3; i++)
	{
		_snprintf(sub, sizeof(sub), "%s\\Pictures\\%s", g_root, kGames[i]);
		CreateDirectory(sub, NULL);
	}
}

// ---- settings ---------------------------------------------------------------

static void trim(char* s)
{
	char* p = s;
	while (*p == ' ' || *p == '\t')
		p++;
	memmove(s, p, strlen(p) + 1);
	int n = (int)strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = 0;
}

static WORD parse_combo(const char* v)
{
	static const struct { const char* name; WORD bit; } map[] = {
		{"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
		{"BACK", XINPUT_GAMEPAD_BACK}, {"START", XINPUT_GAMEPAD_START},
		{"UP", XINPUT_GAMEPAD_DPAD_UP}, {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
		{"LEFT", XINPUT_GAMEPAD_DPAD_LEFT}, {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
		{"LS", XINPUT_GAMEPAD_LEFT_THUMB}, {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
		{"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y}};
	WORD combo = 0;
	char buf[64];
	strncpy(buf, v, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	for (char* tok = strtok(buf, "+ "); tok; tok = strtok(NULL, "+ "))
	{
		_strupr(tok);
		for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
			if (strcmp(tok, map[i].name) == 0)
				combo |= map[i].bit;
	}
	return combo;
}

static const char kDefaultIni[] =
	"; ToyPortal360 settings. Restart the game after changing them.\r\n"
	"\r\n"
	"; emulate = fake portal/toy pad. passthrough = leave a real one alone.\r\n"
	"mode = emulate\r\n"
	"\r\n"
	"; auto, skylanders, dimensions or infinity. auto works it out from the game.\r\n"
	"device = auto\r\n"
	"\r\n"
	"; Buttons held together to open the menu.\r\n"
	"combo = LB+RB+BACK\r\n"
	"\r\n"
	"; Buttons held together to open the character grid (off = no grid).\r\n"
	"grid_combo = LB+RB+Y\r\n"
	"\r\n"
	"; big or small. small draws the grid at half the size, which is faster, so\r\n"
	"; less of the game shows through it.\r\n"
	"grid_size = big\r\n"
	"\r\n"
	"; LEGO Dimensions: with the grid closed, LB+RB + d-pad left / up / right\r\n"
	"; moves the figure the grid last put on to the left / centre / right pad\r\n"
	"; (for Master Builds). off turns it off.\r\n"
	"quick_move = on\r\n"
	"\r\n"
	"; 1 = write every portal packet to log.txt (use when something does not work).\r\n"
	"log = 0\r\n"
	"\r\n"
	"; Advanced: ms between idle Skylanders status packets, and the result code\r\n"
	"; returned when the game reads and nothing is waiting (0 = success, empty).\r\n"
	"status_interval_ms = 20\r\n"
	"idle_result = 0\r\n"
	"\r\n"
	"; Advanced: length byte in wrapped Skylanders replies (auto, or a number).\r\n"
	"sky_wrap_len = auto\r\n";

void config_defaults()
{
	g_config.emulate = true;
	g_config.log_traffic = false;
	g_config.device = kToyUnknown;
	g_config.combo = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
	g_config.grid_combo = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_Y;
	g_config.grid_small = false;
	g_config.quick_move = true;
	g_config.status_interval_ms = 20;
	g_config.idle_result = 0;
	g_config.sky_wrap_len = 0;
}

void config_load()
{
	config_defaults();

	char path[96];
	_snprintf(path, sizeof(path), "%s\\toyportal.ini", g_root);
	HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
	{
		write_figure(path, (const BYTE*)kDefaultIni, sizeof(kDefaultIni) - 1);
		return;
	}
	char text[2048];
	DWORD got = 0;
	ReadFile(f, text, sizeof(text) - 1, &got, NULL);
	CloseHandle(f);
	text[got] = 0;

	char* ctx = NULL;
	for (char* line = strtok_s(text, "\n", &ctx); line; line = strtok_s(NULL, "\n", &ctx))
	{
		char* eq = strchr(line, '=');
		if (line[0] == ';' || line[0] == '#' || !eq)
			continue;
		*eq = 0;
		char key[32], val[64];
		strncpy(key, line, sizeof(key) - 1); key[sizeof(key) - 1] = 0;
		strncpy(val, eq + 1, sizeof(val) - 1); val[sizeof(val) - 1] = 0;
		trim(key);
		trim(val);
		_strlwr(key);

		if (strcmp(key, "mode") == 0)
			g_config.emulate = _stricmp(val, "passthrough") != 0;
		else if (strcmp(key, "device") == 0)
			g_config.device = _stricmp(val, "skylanders") == 0 ? kToySkylanders :
			                  _stricmp(val, "dimensions") == 0 ? kToyDimensions :
			                  _stricmp(val, "infinity") == 0 ? kToyInfinity : kToyUnknown;
		else if (strcmp(key, "combo") == 0)
		{
			const WORD c = parse_combo(val);
			if (c)
				g_config.combo = c;
		}
		else if (strcmp(key, "grid_combo") == 0)
		{
			const WORD c = parse_combo(val);
			if (c || _stricmp(val, "off") == 0)
				g_config.grid_combo = c;
		}
		else if (strcmp(key, "quick_move") == 0)
			g_config.quick_move = _stricmp(val, "off") != 0;
		else if (strcmp(key, "grid_size") == 0)
			g_config.grid_small = _stricmp(val, "small") == 0;
		else if (strcmp(key, "log") == 0)
			g_config.log_traffic = atoi(val) != 0;
		else if (strcmp(key, "status_interval_ms") == 0)
			g_config.status_interval_ms = (DWORD)atoi(val);
		else if (strcmp(key, "idle_result") == 0)
			g_config.idle_result = strtoul(val, NULL, 0);
		else if (strcmp(key, "sky_wrap_len") == 0)
			g_config.sky_wrap_len = _stricmp(val, "auto") == 0 ? 0 : strtoul(val, NULL, 0);
	}
}

// ---- figure files -----------------------------------------------------------

void display_path(const char* in, char* out, int out_size)
{
	static const char* from[] = {"TPHDD:", "TPUSB0:", "TPUSB1:"};
	static const char* to[] = {"Hdd:", "Usb0:", "Usb1:"};
	for (int i = 0; i < 3; i++)
	{
		const size_t n = strlen(from[i]);
		if (_strnicmp(in, from[i], n) == 0)
		{
			_snprintf(out, out_size, "%s%s", to[i], in + n);
			out[out_size - 1] = 0;
			return;
		}
	}
	strncpy(out, in, out_size - 1);
	out[out_size - 1] = 0;
}

DWORD figure_size(ToyKind kind)
{
	return kind == kToyDimensions ? (DWORD)kDimFigureSize :
	       kind == kToyInfinity ? (DWORD)kInfFigureSize : (DWORD)kSkyFigureSize;
}

void figure_folder(ToyKind kind, char* out, int out_size)
{
	_snprintf(out, out_size, "%s\\%s", g_root,
		kind == kToyDimensions ? "Dimensions" : kind == kToyInfinity ? "Infinity" : "Skylanders");
	out[out_size - 1] = 0;
}

void picture_folder(ToyKind kind, char* out, int out_size)
{
	_snprintf(out, out_size, "%s\\Pictures\\%s", g_root,
		kind == kToyDimensions ? "Dimensions" : kind == kToyInfinity ? "Infinity" : "Skylanders");
	out[out_size - 1] = 0;
}

DWORD read_small_file(const char* path, BYTE* out, DWORD max)
{
	HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return kNoFile;
	// Ask for one byte more than fits, to notice a file that is too big.
	DWORD got = 0, more = 0;
	BYTE extra;
	BOOL ok = ReadFile(f, out, max, &got, NULL);
	if (ok && got == max)
		ok = ReadFile(f, &extra, 1, &more, NULL);
	CloseHandle(f);
	if (!ok)
		return kNoFile;
	return more ? max + 1 : got;
}

static bool name_contains(const char* hay, const char* needle)
{
	if (!needle || !*needle)
		return true;
	char h[64], n[64];
	strncpy(h, hay, 63); h[63] = 0;
	strncpy(n, needle, 63); n[63] = 0;
	_strlwr(h);
	_strlwr(n);
	return strstr(h, n) != NULL;
}

int list_figure_files(ToyKind kind, const char* filter, char (*names)[48], int max)
{
	char pattern[128];
	figure_folder(kind, pattern, sizeof(pattern));
	strcat(pattern, "\\*");
	const DWORD need = figure_size(kind);

	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	int n = 0;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		if (fd.nFileSizeLow < need || !name_contains(fd.cFileName, filter))
			continue;
		strncpy(names[n], fd.cFileName, 47);
		names[n][47] = 0;
		n++;
	} while (n < max && FindNextFile(h, &fd));
	FindClose(h);

	// Alphabetical, so browsing is predictable.
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && _stricmp(names[j - 1], names[j]) > 0; j--)
		{
			char t[48];
			strcpy(t, names[j]);
			strcpy(names[j], names[j - 1]);
			strcpy(names[j - 1], t);
		}
	return n;
}

bool read_figure(const char* path, BYTE* out, DWORD size)
{
	HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	DWORD got = 0;
	const BOOL ok = ReadFile(f, out, size, &got, NULL);
	CloseHandle(f);
	return ok && got == size;
}

bool write_figure(const char* path, const BYTE* data, DWORD size)
{
	HANDLE f = CreateFile(path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	SetFilePointer(f, 0, NULL, FILE_BEGIN);
	const BOOL ok = WriteFile(f, data, size, &written, NULL);
	CloseHandle(f);
	return ok && written == size;
}

// Writes a new file, adding " (2)", " (3)"... if the name is taken. Returns
// false with *error set (a Win32 error code) when it can't: only "file
// exists" moves on to the next number, anything else is reported at once.
bool create_unique_file(const char* folder, const char* base, const char* ext, const BYTE* data, DWORD size, char* out_path, int out_size, DWORD* error)
{
	*error = 0;
	CreateDirectory(folder, NULL); // in case it was removed since start-up
	for (int i = 0; i < 100; i++)
	{
		if (i == 0)
			_snprintf(out_path, out_size, "%s\\%s.%s", folder, base, ext);
		else
			_snprintf(out_path, out_size, "%s\\%s (%d).%s", folder, base, i + 1, ext);
		out_path[out_size - 1] = 0;
		HANDLE f = CreateFile(out_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (f == INVALID_HANDLE_VALUE)
		{
			const DWORD e = GetLastError();
			if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS)
				continue; // taken: try the next number
			*error = e ? e : 0xFFFFFFFF;
			log_line("Could not create %s (error %u)", out_path, (unsigned)e);
			return false;
		}
		DWORD written = 0;
		const BOOL ok = WriteFile(f, data, size, &written, NULL);
		*error = ok ? 0 : GetLastError();
		CloseHandle(f);
		if (!ok || written != size)
		{
			log_line("Could not write %s (error %u, %u of %u bytes)", out_path, (unsigned)*error, (unsigned)written, (unsigned)size);
			if (!*error) *error = 0xFFFFFFFF;
			return false;
		}
		return true;
	}
	*error = ERROR_FILE_EXISTS;
	return false;
}

// Copies changed figures out under the lock, then writes them without it.
void flush_dirty_figures()
{
	static BYTE sky_buf[kSkySlots][kSkyFigureSize];
	static char sky_path[kSkySlots][260];
	static BYTE dim_buf[kDimSlots][kDimFigureSize];
	static char dim_path[kDimSlots][260];
	static BYTE inf_buf[kInfSlots][kInfFigureSize];
	static char inf_path[kInfSlots][260];
	bool inf_dirty[kInfSlots] = {false};
	bool sky_dirty[kSkySlots] = {false};
	bool dim_dirty[kDimSlots] = {false};

	EnterCriticalSection(&s_flush_lock);
	EnterCriticalSection(&g_lock);
	for (int i = 0; i < kSkySlots; i++)
	{
		SkyFigure& f = g_router.sky.figure(i);
		if (f.dirty && f.path[0])
		{
			memcpy(sky_buf[i], f.data, kSkyFigureSize);
			strcpy(sky_path[i], f.path);
			f.dirty = false;
			sky_dirty[i] = true;
		}
	}
	for (int i = 0; i < kDimSlots; i++)
	{
		DimFigure& f = g_router.dim.figure(i);
		if (f.dirty && f.path[0])
		{
			memcpy(dim_buf[i], f.data, kDimFigureSize);
			strcpy(dim_path[i], f.path);
			f.dirty = false;
			dim_dirty[i] = true;
		}
	}
	for (int i = 0; i < kInfSlots; i++)
	{
		InfFigure& f = g_router.inf.figure(i);
		if (f.dirty && f.path[0])
		{
			memcpy(inf_buf[i], f.data, kInfFigureSize);
			strcpy(inf_path[i], f.path);
			f.dirty = false;
			inf_dirty[i] = true;
		}
	}
	LeaveCriticalSection(&g_lock);

	for (int i = 0; i < kSkySlots; i++)
		if (sky_dirty[i] && !write_figure(sky_path[i], sky_buf[i], kSkyFigureSize))
			log_line("Could not save %s", sky_path[i]);
	for (int i = 0; i < kDimSlots; i++)
		if (dim_dirty[i] && !write_figure(dim_path[i], dim_buf[i], kDimFigureSize))
			log_line("Could not save %s", dim_path[i]);
	for (int i = 0; i < kInfSlots; i++)
		if (inf_dirty[i] && !write_figure(inf_path[i], inf_buf[i], kInfFigureSize))
			log_line("Could not save %s", inf_path[i]);
	LeaveCriticalSection(&s_flush_lock);
}
