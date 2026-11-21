/*
 * Runs a built ToyPortal360 .xex - the real console binary - under qemu-ppc.
 *
 *   run_xex <file.xex> <dashlaunch|sideload> <storage dir>
 *
 * It does the Xbox kernel loader's job: unpacks the XEX image at its load
 * address and resolves its imports (address-table slots and call stubs)
 * to fake kernel/XAM functions written here. It then calls the module's
 * entry point and acts as a toy game whose imports the plugin hooks.
 *
 * One change is made to the image: qemu-ppc emulates a 32-bit CPU, so the
 * 64-bit register saves in the entry wrappers (std/ld r14-r31 at r1) are
 * turned into 32-bit ones (stw/lwz of the low word). Nothing else in the
 * image uses 64-bit instructions.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

static int g_pass, g_fail;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const char* g_root;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;

/* ---- scripted UI and records ------------------------------------------ */

static int g_boxes[64], g_box_n, g_box_i;
static char g_keys[16][64];
static int g_key_n, g_key_i;
static char g_shown[256][512];
static int g_shown_n;
static char g_notes[64][160];
static int g_note_n;
static volatile u16 g_buttons;
static volatile int g_real_calls, g_system_threads, g_title_threads, g_threads;

static void narrow(const u16* w, char* out, int n)
{
	int i = 0;
	for (; w && w[i] && i < n - 1; i++)
		out[i] = w[i] < 128 ? (char)w[i] : '?';
	out[i] = 0;
}

static int shown_contains(const char* s)
{
	pthread_mutex_lock(&g_mx);
	int found = 0;
	for (int i = 0; i < g_shown_n && !found; i++)
		found = strstr(g_shown[i], s) != NULL;
	pthread_mutex_unlock(&g_mx);
	return found;
}

static int note_contains(const char* s)
{
	pthread_mutex_lock(&g_mx);
	int found = 0;
	for (int i = 0; i < g_note_n && !found; i++)
		found = strstr(g_notes[i], s) != NULL;
	pthread_mutex_unlock(&g_mx);
	return found;
}

static int script_left(void)
{
	pthread_mutex_lock(&g_mx);
	int n = (g_box_n - g_box_i) + (g_key_n - g_key_i);
	pthread_mutex_unlock(&g_mx);
	return n;
}

/* ---- handles -------------------------------------------------------------- */

enum { H_FILE = 1, H_DIR, H_THREAD };
typedef struct
{
	int kind, fd;
	DIR* dir;
	char dir_path[600];
	pthread_t thread;
} Handle;

static void host_path(const char* p, char* out, size_t n)
{
	const char* colon = strchr(p, ':');
	size_t used = (size_t)snprintf(out, n, "%s/%.*s", g_root, (int)(colon - p), p);
	for (const char* c = colon + 1; *c && used + 1 < n; c++)
		out[used++] = *c == '\\' ? '/' : *c;
	out[used] = 0;
}

/* ---- fake kernel ---------------------------------------------------------- */

static void* g_exec_module; /* what XexExecutableModuleHandle points at */

typedef struct { pthread_mutex_t m; void* key; } CsSlot;
static CsSlot g_cs[16];
static pthread_mutex_t* cs_for(void* cs)
{
	pthread_mutex_lock(&g_mx);
	for (int i = 0; i < 16; i++)
	{
		if (g_cs[i].key == cs)
		{
			pthread_mutex_unlock(&g_mx);
			return &g_cs[i].m;
		}
	}
	for (int i = 0; i < 16; i++)
	{
		if (!g_cs[i].key)
		{
			pthread_mutexattr_t a;
			pthread_mutexattr_init(&a);
			pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
			pthread_mutex_init(&g_cs[i].m, &a);
			g_cs[i].key = cs;
			pthread_mutex_unlock(&g_mx);
			return &g_cs[i].m;
		}
	}
	abort();
}
static void k_RtlInitializeCriticalSection(void* cs) { memset(cs, 0, 0x1C); cs_for(cs); }
static void k_RtlEnterCriticalSection(void* cs) { pthread_mutex_lock(cs_for(cs)); }
static void k_RtlLeaveCriticalSection(void* cs) { pthread_mutex_unlock(cs_for(cs)); }

typedef struct { u32 (*fn)(void*); void* ctx; } ThreadStart;
static void* thread_main(void* p)
{
	ThreadStart t = *(ThreadStart*)p;
	free(p);
	__sync_fetch_and_add(&g_threads, 1);
	t.fn(t.ctx); /* console code; raw threads end in ExTerminateThread */
	__sync_fetch_and_sub(&g_threads, 1);
	return NULL;
}
static u32 k_ExCreateThread(void** handle, u32 stack, u32* tid, void* startup, u32 (*start)(void*), void* ctx, u32 flags)
{
	(void)stack;
	if (startup)
		printf("note: ExCreateThread with an XAPI startup routine\n");
	if (flags & 2)
		__sync_fetch_and_add(&g_system_threads, 1);
	else
		__sync_fetch_and_add(&g_title_threads, 1);
	Handle* h = calloc(1, sizeof(Handle));
	h->kind = H_THREAD;
	ThreadStart* t = malloc(sizeof(ThreadStart));
	t->fn = start;
	t->ctx = ctx;
	pthread_create(&h->thread, NULL, thread_main, t);
	if (tid)
		*tid = 1;
	*handle = h;
	return 0;
}
static u32 k_ExTerminateThread(u32 code)
{
	(void)code;
	__sync_fetch_and_sub(&g_threads, 1);
	pthread_exit(NULL);
}
typedef struct { u16 len, max; char* buf; } AnsiString;
/* Inside a game (the sideload builds), file calls only see drive names made
 * in \??\ - on a console, a \System??\ link left the plugin unable to write. */
static int g_in_title;
static u32 k_ObCreateSymbolicLink(AnsiString* link, AnsiString* dev)
{
	(void)dev;
	char name[64], path[600];
	snprintf(name, sizeof(name), "%.*s", link->len, link->buf);
	if (g_in_title && strncmp(name, "\\??\\", 4) != 0)
		return 0; /* made, but in a namespace the game's file calls never look in */
	char* drive = strrchr(name, '\\') + 1;
	drive[strlen(drive) - 1] = 0; /* drop ':' */
	snprintf(path, sizeof(path), "%s/%s", g_root, drive);
	mkdir(path, 0755);
	return 0;
}
/* Kernel file calls used by the start-up markers: \Device\Harddisk0\Partition1\x
 * becomes <root>/HDD1/x. NtCreateFile's 9th argument (create options) is ignored. */
typedef struct { u16 len, max; char* buf; } ObjString;
typedef struct { void* root; ObjString* name; u32 attributes; } ObjAttributes;
static u32 k_NtCreateFile(u32* handle, u32 access, ObjAttributes* attr, u32* io, void* alloc, u32 fattr, u32 share, u32 disp)
{
	(void)access; (void)io; (void)alloc; (void)fattr; (void)share; (void)disp;
	static const char prefix[] = "\\Device\\Harddisk0\\Partition1\\";
	char name[300], path[700];
	snprintf(name, sizeof(name), "%.*s", attr->name->len, attr->name->buf);
	if (getenv("XEX_DEBUG")) fprintf(stderr, "NtCreateFile %s\n", name);
	if (strncmp(name, prefix, sizeof(prefix) - 1) != 0)
		return 0xC0000034;
	snprintf(path, sizeof(path), "%s/HDD1/%s", g_root, name + sizeof(prefix) - 1);
	for (char* c = path + strlen(g_root) + 1; *c; c++)
		if (*c == '\\' || *c == '/') { *c = 0; mkdir(path, 0755); *c = '/'; }
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return 0xC0000034;
	*handle = (u32)fd;
	return 0;
}
static u32 k_NtWriteFile(u32 h, void* ev, void* apc, void* ctx, u32* io, void* buf, u32 len, void* off)
{
	(void)ev; (void)apc; (void)ctx; (void)io; (void)off;
	return write((int)h, buf, len) == (ssize_t)len ? 0 : 0xC0000001;
}
static u32 k_NtClose(u32 h) { close((int)h); return 0; }
static u32 k_MmIsAddressValid(void* p) { return p != NULL; }

static void* k_RtlImageXexHeaderField(void* base, u32 field)
{
	if (field != 0x000103FF)
		return NULL;
	/* the fake game keeps its imports at XexHeaderBase, or 16 bytes in when the
	 * header starts with a real "XEX2" signature (a game's extra modules) */
	return *(u32*)base == 0x58455832 ? (u8*)base + 16 : base;
}

/* XAM functions the plugin resolves at run time. */
static u32 x_NonControllerGetRaw(u16* state, u32* len, u8* b) { (void)b; g_real_calls++; *len = 0; *state = 1; return 0; }
static u32 x_NonControllerSetRaw(u32 len, u8* b) { (void)len; (void)b; g_real_calls++; return 0; }
static u32 x_NonControllerGetRawEx(u32 d, u8* b, u32* len, u16* state) { (void)d; (void)b; g_real_calls++; *len = 0; *state = 1; return 0; }
static u32 x_NonControllerSetRawEx(u32 d, u8* b, u32 len) { (void)d; (void)b; (void)len; g_real_calls++; return 0; }
static void x_XNotifyQueueUI(u32 type, u32 user, u32 areas, u16* text, void* ctx)
{
	(void)type; (void)user; (void)areas; (void)ctx;
	pthread_mutex_lock(&g_mx);
	narrow(text, g_notes[g_note_n < 63 ? g_note_n++ : 63], 160);
	pthread_mutex_unlock(&g_mx);
}

static u32 k_XexGetModuleHandle(const char* name, void** h)
{
	if (strcmp(name, "xboxkrnl.exe") == 0) { *h = (void*)0x5678; return 0; }
	if (strcmp(name, "xam.xex") != 0)
		return 0xC0000225;
	*h = (void*)0x1234;
	return 0;
}
static u32 k_XexGetProcedureAddress(void* h, u32 ord, void** out);
static u32 x_XamInputGetState(u32 user, u32 flags, u8* state);
static u32 x_XamInputGetKeystroke(u32 user, u32 flags, u8* key);
static u32 x_XamInputGetKeystrokeEx(u32* user, u32 flags, u8* key);
static void k_VdSwap(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);
static u32 k_XexGetProcedureAddress(void* h, u32 ord, void** out)
{
	if (h == (void*)0x5678) /* xboxkrnl.exe: only the lookup function itself */
	{
		if (ord == 0x25B) { *out = (void*)k_VdSwap; return 0; }
		if (ord != 0x197) return 0xC0000139;
		*out = (void*)k_XexGetProcedureAddress;
		return 0;
	}
	void* f = NULL;
	switch (ord)
	{
	case 0x4A1: f = (void*)x_NonControllerGetRaw; break;
	case 0x4A2: f = (void*)x_NonControllerSetRaw; break;
	case 0x52A: f = (void*)x_NonControllerGetRawEx; break;
	case 0x52B: f = (void*)x_NonControllerSetRawEx; break;
	case 0x290: f = (void*)x_XNotifyQueueUI; break;
	case 0x191: f = (void*)x_XamInputGetState; break;
	case 0x193: f = (void*)x_XamInputGetKeystroke; break;
	case 0x198: f = (void*)x_XamInputGetKeystrokeEx; break;
	}
	*out = f;
	return f ? 0 : 0xC0000225;
}

/* ---- fake XAM: Win32 subset ------------------------------------------------ */

static u32 x_GetTickCount(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (u32)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void x_Sleep(u32 ms) { usleep(ms * 1000); }
static u32 x_CloseHandle(Handle* h)
{
	if (!h || h == (void*)-1)
		return 0;
	if (h->kind == H_FILE) close(h->fd);
	if (h->kind == H_DIR) closedir(h->dir);
	if (h->kind == H_THREAD) pthread_detach(h->thread);
	free(h);
	return 1;
}
static u32 x_WaitForSingleObject(Handle* h, u32 ms)
{
	(void)ms;
	if (h && h->kind == H_THREAD)
		pthread_join(h->thread, NULL);
	if (h)
		h->kind = 0; /* joined; CloseHandle must not detach */
	return 0;
}
static __thread u32 g_last_error;
static u32 x_GetLastError(void) { return g_last_error; }
static void* x_CreateFileA(const char* p, u32 access, u32 share, void* sa, u32 disp, u32 attr, void* tmpl)
{
	(void)share; (void)sa; (void)attr; (void)tmpl;
	char path[600];
	host_path(p, path, sizeof(path));
	int flags = (access & (0x40000000 | 4)) ? ((access & 0x80000000) ? O_RDWR : O_WRONLY) : O_RDONLY;
	if (access & 4) flags |= O_APPEND;
	if (disp == 1) flags |= O_CREAT | O_EXCL;
	if (disp == 4) flags |= O_CREAT;
	int fd = open(path, flags, 0644);
	if (fd < 0)
	{
		g_last_error = errno == EEXIST ? 80 : 3; /* ERROR_FILE_EXISTS / ERROR_PATH_NOT_FOUND */
		return (void*)-1;
	}
	Handle* h = calloc(1, sizeof(Handle));
	h->kind = H_FILE;
	h->fd = fd;
	return h;
}
static u32 x_ReadFile(Handle* h, void* b, u32 n, u32* got, void* ov)
{
	(void)ov;
	ssize_t r = read(h->fd, b, n);
	if (got) *got = r < 0 ? 0 : (u32)r;
	return r >= 0;
}
static u32 x_WriteFile(Handle* h, const void* b, u32 n, u32* put, void* ov)
{
	(void)ov;
	ssize_t r = write(h->fd, b, n);
	if (put) *put = r < 0 ? 0 : (u32)r;
	return r >= 0;
}
static u32 x_SetFilePointer(Handle* h, int32_t d, int32_t* hi, u32 m)
{
	(void)hi;
	return (u32)lseek(h->fd, d, m == 2 ? SEEK_END : SEEK_SET);
}
static u32 x_GetFileAttributesA(const char* p)
{
	char path[600];
	struct stat st;
	host_path(p, path, sizeof(path));
	if (stat(path, &st) != 0)
		return 0xFFFFFFFF;
	return S_ISDIR(st.st_mode) ? 0x10 : 0x80;
}
static u32 x_CreateDirectoryA(const char* p, void* sa)
{
	(void)sa;
	char path[600];
	host_path(p, path, sizeof(path));
	return mkdir(path, 0755) == 0;
}
typedef struct
{
	u32 attrs;
	u32 times[6];
	u32 size_hi, size_lo, res0, res1;
	char name[260];
	char alt[14];
} FindData; /* the console's WIN32_FIND_DATA */
static u32 fill_find(Handle* h, FindData* fd)
{
	for (;;)
	{
		struct dirent* e = readdir(h->dir);
		if (!e)
			return 0;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		char p[1024];
		struct stat st;
		snprintf(p, sizeof(p), "%s/%s", h->dir_path, e->d_name);
		stat(p, &st);
		memset(fd, 0, sizeof(*fd));
		fd->attrs = S_ISDIR(st.st_mode) ? 0x10 : 0x80;
		fd->size_lo = (u32)st.st_size;
		snprintf(fd->name, sizeof(fd->name), "%s", e->d_name);
		return 1;
	}
}
static void* x_FindFirstFileA(const char* pattern, FindData* fd)
{
	char path[600];
	host_path(pattern, path, sizeof(path));
	*strrchr(path, '/') = 0;
	DIR* d = opendir(path);
	if (!d)
		return (void*)-1;
	Handle* h = calloc(1, sizeof(Handle));
	h->kind = H_DIR;
	h->dir = d;
	snprintf(h->dir_path, sizeof(h->dir_path), "%s", path);
	if (!fill_find(h, fd))
	{
		closedir(d);
		free(h);
		return (void*)-1;
	}
	return h;
}
static u32 x_FindNextFileA(Handle* h, FindData* fd) { return fill_find(h, fd); }

/* ---- fake XAM: input and system UI ---------------------------------------- */

static u32 g_title_id;
static u32 x_XamGetCurrentTitleId(void) { return g_title_id; }
static u32 x_XamInputGetState(u32 user, u32 flags, u8* state)
{
	(void)flags;
	if (user != 0)
		return 1167;
	memset(state, 0, 16);
	state[4] = (u8)(g_buttons >> 8);
	state[5] = (u8)g_buttons;
	return 0;
}
static u32 x_XamInputGetKeystroke(u32 user, u32 flags, u8* key)
{
	(void)user; (void)flags; (void)key;
	return 0x10D2; /* ERROR_EMPTY */
}
static u32 x_XamInputGetKeystrokeEx(u32* user, u32 flags, u8* key)
{
	(void)user; (void)flags; (void)key;
	return 0x10D2;
}

/* VdSwap: ten arguments, the last two on the stack. Records what it got so
 * the test can check the hook passed everything through. */
static volatile int g_swap_calls;
static volatile u32 g_swap_args[10];
static void k_VdSwap(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9)
{
	const u32 a[10] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
	for (int i = 0; i < 10; i++)
		g_swap_args[i] = a[i];
	g_swap_calls++;
}
typedef struct { u32 result, length, context, event, routine, rctx, ext; } Overlapped;
static u32 x_XamGetOverlappedResult(Overlapped* ov, u32* res, u32 wait)
{
	(void)res; (void)wait;
	return ov->result;
}
/* Nine arguments: Xenon Forge's stub passes the ninth (the overlapped) in
 * the console's place, offset 0x54 of the caller's frame. */
/* Like a console, the keyboard is refused once right after a message box. */
static volatile int g_box_closing, g_keyboard_refused;
static u32 x_XamShowMessageBoxUI(u32 user, u16* title, u16* text, u32 count, u16** buttons, u32 focus, u32 flags, u32* result)
{
	(void)user; (void)count; (void)buttons; (void)focus; (void)flags;
	char* caller_sp = *(char**)__builtin_frame_address(0);
	Overlapped* ov = *(Overlapped**)(caller_sp + 0x54);
	char t[96], x[400];
	narrow(title, t, sizeof(t));
	narrow(text, x, sizeof(x));
	pthread_mutex_lock(&g_mx);
	snprintf(g_shown[g_shown_n < 255 ? g_shown_n++ : 255], 512, "[%s] %s", t, x);
	int a = g_box_i < g_box_n ? g_boxes[g_box_i++] : -1;
	pthread_mutex_unlock(&g_mx);
	if (getenv("SHOW_UI"))
		printf("\n[%s]\n%s\n", t, x);
	*result = a < 0 ? 0 : (u32)a;
	ov->result = a < 0 ? 1223 : 0;
	g_box_closing = 1;
	return 997; /* ERROR_IO_PENDING */
}
static u32 x_XamShowKeyboardUI(u32 user, u32 flags, u16* def, u16* title, u16* desc, u16* out, u32 cch, Overlapped* ov)
{
	(void)user; (void)flags; (void)def; (void)title; (void)desc;
	if (g_box_closing)
	{
		g_box_closing = 0;
		g_keyboard_refused++;
		return 5; /* ERROR_ACCESS_DENIED */
	}
	pthread_mutex_lock(&g_mx);
	const char* t = g_key_i < g_key_n ? g_keys[g_key_i++] : NULL;
	pthread_mutex_unlock(&g_mx);
	u32 i = 0;
	for (; t && t[i] && i + 1 < cch; i++)
		out[i] = (u16)t[i];
	out[i] = 0;
	ov->result = t ? 0 : 1223;
	return 997;
}

/* ---- the loader ------------------------------------------------------------- */

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }

static void* resolve(int lib, u32 ord)
{
	if (lib == 0)
	{
		switch (ord)
		{
		case 0x0D: return (void*)k_ExCreateThread;
		case 0x19: return (void*)k_ExTerminateThread;
		case 0x103: return (void*)k_ObCreateSymbolicLink;
		case 0xCF: return (void*)k_NtClose;
		case 0xBF: return (void*)k_MmIsAddressValid;
		case 0xD2: return (void*)k_NtCreateFile;
		case 0xFF: return (void*)k_NtWriteFile;
		case 0x125: return (void*)k_RtlEnterCriticalSection;
		case 0x12B: return (void*)k_RtlImageXexHeaderField;
		case 0x12E: return (void*)k_RtlInitializeCriticalSection;
		case 0x130: return (void*)k_RtlLeaveCriticalSection;
		case 0x195: return (void*)k_XexGetModuleHandle;
		case 0x197: return (void*)k_XexGetProcedureAddress;
		case 0x193: return (void*)&g_exec_module; /* variable */
		}
	}
	else
	{
		switch (ord)
		{
		case 0x414: return (void*)x_CloseHandle;
		case 0x415: return (void*)x_GetTickCount;
		case 0x417: return (void*)x_SetFilePointer;
		case 0x41C: return (void*)x_ReadFile;
		case 0x41E: return (void*)x_WriteFile;
		case 0x425: return (void*)x_WaitForSingleObject;
		case 0x436: return (void*)x_Sleep;
		case 0x445: return (void*)x_CreateDirectoryA;
		case 0x447: return (void*)x_CreateFileA;
		case 0x416: return (void*)x_GetLastError;
		case 0x449: return (void*)x_FindFirstFileA;
		case 0x44A: return (void*)x_FindNextFileA;
		case 0x44B: return (void*)x_GetFileAttributesA;
		case 0x1CF: return (void*)x_XamGetCurrentTitleId;
		case 0x1FB: return (void*)x_XamGetOverlappedResult;
		case 0x191: return (void*)x_XamInputGetState;
		case 0x2C1: return (void*)x_XamShowKeyboardUI;
		case 0x2CA: return (void*)x_XamShowMessageBoxUI;
		}
	}
	printf("unresolved import: library %d ordinal 0x%X\n", lib, ord);
	exit(2);
}

static u32 g_base, g_entry, g_flags;

static void load_xex(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f) { perror(path); exit(2); }
	static u8 x[1 << 22];
	size_t n = fread(x, 1, sizeof(x), f);
	fclose(f);
	if (n < 0x18 || memcmp(x, "XEX2", 4) != 0) { printf("not a XEX2\n"); exit(2); }

	g_flags = be32(x + 4);
	const u32 pe_off = be32(x + 8), sec_off = be32(x + 0x10), nhdr = be32(x + 0x14);
	u32 ff = 0, imports = 0;
	for (u32 i = 0; i < nhdr; i++)
	{
		const u32 k = be32(x + 0x18 + i * 8), v = be32(x + 0x1C + i * 8);
		if (k == 0x000003FF) ff = v;
		if (k == 0x00010100) g_entry = v;
		if (k == 0x00010201) g_base = v;
		if (k == 0x000103FF) imports = v;
	}
	const u32 image_size = be32(x + sec_off + 4);

	u8* img = mmap((void*)(uintptr_t)g_base, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (img != (u8*)(uintptr_t)g_base) { printf("cannot map 0x%08X\n", g_base); exit(2); }

	/* Unpack: unencrypted, "basic" compression = (data, zeros) blocks. */
	const u32 ff_size = be32(x + ff);
	const u16 encryption = be16(x + ff + 4), compression = be16(x + ff + 6);
	if (encryption != 0 || compression != 1) { printf("unsupported XEX format %u/%u\n", encryption, compression); exit(2); }
	u32 pos = pe_off, out = 0;
	for (u32 b = 0; b < (ff_size - 8) / 8; b++)
	{
		const u32 data = be32(x + ff + 8 + b * 8), zeros = be32(x + ff + 12 + b * 8);
		memcpy(img + out, x + pos, data);
		pos += data;
		out += data + zeros;
	}

	/* 64-bit saves in the entry wrappers -> 32-bit (see the header). */
	int rewritten = 0;
	for (u32 o = 0; o + 4 <= image_size; o += 4)
	{
		u32 w = be32(img + o);
		const u32 op = w >> 26, ra = (w >> 16) & 31, xo = w & 3;
		if ((op == 62 || op == 58) && xo == 0 && ra == 1)
		{
			const u32 rs = (w >> 21) & 31, ds = w & 0xFFFC;
			w = ((op == 62 ? 36u : 32u) << 26) | (rs << 21) | (1u << 16) | ((ds + 4) & 0xFFFF);
			img[o] = (u8)(w >> 24); img[o + 1] = (u8)(w >> 16); img[o + 2] = (u8)(w >> 8); img[o + 3] = (u8)w;
			rewritten++;
		}
	}

	/* Imports, as the kernel loader resolves them. */
	const u32 str_size = be32(x + imports + 4), libs = be32(x + imports + 8);
	u32 p = imports + 12 + str_size, records = 0;
	for (u32 l = 0; l < libs; l++)
	{
		const u32 lsize = be32(x + p);
		const u16 count = be16(x + p + 0x26);
		for (u16 r = 0; r < count; r++)
		{
			const u32 addr = be32(x + p + 0x28 + r * 4);
			u8* at = img + (addr - g_base);
			const u32 value = be32(at), type = value >> 24, ord = value & 0xFFFF;
			const u32 target = (u32)(uintptr_t)resolve((int)l, ord);
			if (type == 0) /* address-table slot or variable slot */
			{
				at[0] = (u8)(target >> 24); at[1] = (u8)(target >> 16); at[2] = (u8)(target >> 8); at[3] = (u8)target;
			}
			else if (type == 1) /* call stub: lis r11,hi; ori r11,r11,lo; (mtctr r11; bctr) */
			{
				const u32 lis = 0x3D600000 | (target >> 16), ori = 0x616B0000 | (target & 0xFFFF);
				u32 words[2] = {lis, ori};
				for (int k = 0; k < 2; k++)
				{
					at[k * 4] = (u8)(words[k] >> 24); at[k * 4 + 1] = (u8)(words[k] >> 16);
					at[k * 4 + 2] = (u8)(words[k] >> 8); at[k * 4 + 3] = (u8)words[k];
				}
			}
			else
			{
				printf("unknown import record %08X\n", value);
				exit(2);
			}
			records++;
		}
		p += lsize;
	}
	__builtin___clear_cache((char*)img, (char*)img + image_size);
	printf("loaded %s: flags 0x%X, base 0x%08X, entry 0x%08X, %u import records, %d register saves made 32-bit\n",
		path, g_flags, g_base, g_entry, records, rewritten);
}

/* ---- a fake toy game -------------------------------------------------------- */

typedef struct
{
	/* LDR_DATA_TABLE_ENTRY: XexHeaderBase at 0x58, TimeDateStamp at 0x50 */
	u8 ldr[0x64];
	u8 header[256];
	u32 slots[4];
	u32* stubs;
	u32 real[2];
} Game;

static void make_game(Game* g, int ex, u32 stamp)
{
	memset(g, 0, sizeof(*g));
	g->stubs = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	*(u32*)(g->ldr + 0x58) = (u32)(uintptr_t)g->header;
	*(u32*)(g->ldr + 0x50) = stamp;
	u32* d = (u32*)g->header; /* Size, NameTableSize, ModuleCount */
	d[0] = 12 + 8 + 0x28 + 16; /* Size of the whole import header */
	d[1] = 8;
	d[2] = 1;
	memcpy(g->header + 12, "xam.xex\0", 8);
	u8* table = g->header + 20;
	*(u16*)(table + 0x26) = 4;            /* ImportCount */
	*(u32*)table = 0x28 + 16;             /* TableSize */
	u32* records = (u32*)(table + 0x28);
	const u32 ords[2] = {ex ? 0x52Au : 0x4A1u, ex ? 0x52Bu : 0x4A2u};
	for (int i = 0; i < 2; i++)
	{
		void* f;
		k_XexGetProcedureAddress(NULL, ords[i], &f);
		g->real[i] = (u32)(uintptr_t)f;
		g->slots[i * 2] = g->real[i];
		u32* s = g->stubs + i * 4;
		s[0] = 0x38600000; s[1] = 0x38800000; s[2] = 0x7D6903A6; s[3] = 0x4E800420;
		records[i * 2] = (u32)(uintptr_t)&g->slots[i * 2];
		records[i * 2 + 1] = (u32)(uintptr_t)s;
	}
}

/* A game like LEGO Dimensions: it imports only XexGetProcedureAddress and
 * looks the toy-pad functions up while running. */
static void make_lookup_game(Game* g, u32 stamp)
{
	memset(g, 0, sizeof(*g));
	g->stubs = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	*(u32*)(g->ldr + 0x58) = (u32)(uintptr_t)g->header;
	*(u32*)(g->ldr + 0x50) = stamp;
	u32* d = (u32*)g->header;
	d[0] = 12 + 16 + 0x28 + 8;
	d[1] = 16;
	d[2] = 1;
	memcpy(g->header + 12, "xboxkrnl.exe\0\0\0\0", 16);
	u8* table = g->header + 28;
	*(u16*)(table + 0x26) = 2;
	*(u32*)table = 0x28 + 8;
	u32* records = (u32*)(table + 0x28);
	g->real[0] = (u32)(uintptr_t)k_XexGetProcedureAddress;
	g->slots[0] = g->real[0];
	u32* st = g->stubs;
	/* what the console's loader writes into a call stub */
	st[0] = 0x3D600000 | (g->real[0] >> 16); st[1] = 0x616B0000 | (g->real[0] & 0xFFFF);
	st[2] = 0x7D6903A6; st[3] = 0x4E800420;
	records[0] = (u32)(uintptr_t)&g->slots[0];
	records[1] = (u32)(uintptr_t)st;
}

typedef u32 (*GetProc)(void*, u32, void**);
typedef u32 (*GetRawEx)(u32, u8*, u32*, u16*);
typedef u32 (*SetRawEx)(u32, u8*, u32);
typedef u32 (*GetRaw)(u16*, u32*, u8*);
typedef u32 (*SetRaw)(u32, u8*);

static u32 read_ex(Game* g, u8* out)
{
	for (int i = 0; i < 200; i++)
	{
		u32 len = 32;
		u16 st = 0;
		memset(out, 0, 32);
		if (((GetRawEx)g->stubs)(5, out, &len, &st) != 0)
			return 0;
		if (len)
			return len;
		usleep(5000);
	}
	return 0;
}

static void open_menu(void)
{
	g_buttons = 0x0100 | 0x0200 | 0x0020; /* LB + RB + Back */
	usleep(250000);
	g_buttons = 0;
	for (int i = 0; i < 600 && (script_left() || g_threads > 1); i++)
		usleep(20000);
	usleep(100000);
}

static void script(const int* boxes, int nb, const char* key)
{
	pthread_mutex_lock(&g_mx);
	for (int i = 0; i < nb; i++)
		g_boxes[g_box_n++] = boxes[i];
	if (key)
		snprintf(g_keys[g_key_n++], 64, "%s", key);
	pthread_mutex_unlock(&g_mx);
}

static int file_bytes(const char* xbox_path, u8* out, size_t n)
{
	char p[600];
	host_path(xbox_path, p, sizeof(p));
	FILE* f = fopen(p, "rb");
	if (!f)
		return 0;
	size_t got = fread(out, 1, n, f);
	fclose(f);
	return got == n;
}

static int log_contains(const char* s);
static int wait_log(const char* s, int ms)
{
	for (int i = 0; i < ms / 50; i++)
	{
		if (log_contains(s))
			return 1;
		usleep(50000);
	}
	return log_contains(s);
}

static int log_contains(const char* s)
{
	static char buf[65536];
	char p[600];
	host_path("TPHDD:\\ToyPortal\\log.txt", p, sizeof(p));
	FILE* f = fopen(p, "rb");
	if (!f)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return strstr(buf, s) != NULL;
}

/* A game that draws: it imports GetRawEx/SetRawEx and XamInputGetState from
 * XAM and VdSwap from the kernel, each as an address slot plus call stub
 * (stubs: 0 GetRawEx, 1 SetRawEx, 2 XamInputGetState, 3 VdSwap). */
typedef struct
{
	u8 ldr[0x64];
	u8 header[512];
	u32 slots[8];
	u32* stubs;
	u32 real[4];
} DrawGame;

static void make_draw_game(DrawGame* g)
{
	memset(g, 0, sizeof(*g));
	g->stubs = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	*(u32*)(g->ldr + 0x58) = (u32)(uintptr_t)g->header;
	*(u32*)(g->ldr + 0x50) = 11;
	u32* d = (u32*)g->header;
	d[1] = 24;
	d[2] = 2;
	memcpy(g->header + 12, "xam.xex\0", 8);
	memcpy(g->header + 20, "xboxkrnl.exe\0\0\0\0", 16);
	u8* table = g->header + 36;
	void* f;
	for (int m = 0; m < 2; m++)
	{
		const int first = m ? 3 : 0, n = m ? 1 : 3;
		*(u16*)(table + 0x26) = (u16)(n * 2);
		*(u32*)table = 0x28 + n * 8;
		u32* records = (u32*)(table + 0x28);
		for (int i = 0; i < n; i++)
		{
			const int k = first + i;
			static const u32 ords[4] = {0x52A, 0x52B, 0x191, 0x25B};
			k_XexGetProcedureAddress(m ? (void*)0x5678 : (void*)0x1234, ords[k], &f);
			g->real[k] = (u32)(uintptr_t)f;
			g->slots[k * 2] = g->real[k];
			u32* st = g->stubs + k * 4;
			st[0] = 0x3D600000 | (g->real[k] >> 16); st[1] = 0x616B0000 | (g->real[k] & 0xFFFF);
			st[2] = 0x7D6903A6; st[3] = 0x4E800420;
			records[i * 2] = (u32)(uintptr_t)&g->slots[k * 2];
			records[i * 2 + 1] = (u32)(uintptr_t)st;
		}
		table += *(u32*)table;
	}
	d[0] = (u32)(table - g->header);
}

/* Two 640x480 tiled A8R8G8B8 front buffers the "game" swaps between. */
enum { FB_W = 640, FB_H = 480, FB_SPAN = FB_W * FB_H * 4 };
static u8* g_fb[2];
static u32 g_fb_addr[2];
static u32 g_fetch[2][6] __attribute__((aligned(16)));
static volatile int g_render, g_render_quit;
static DrawGame g_draw;

typedef void (*VdSwapFn)(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);
static u32 g_fmt = 6, g_colorspace = 0, g_w = FB_W, g_h = FB_H;
static u32 g_ring[64];

static u32 tiled(u32 x, u32 y) /* Xenia's Tiled2D, 4-byte blocks, pitch 640 */
{
	const u32 outer = ((y >> 5) * (FB_W >> 5) + (x >> 5)) << 6, inner = (((y >> 1) & 7) << 3) | (x & 7);
	const u32 b = (outer | inner) << 2, bank = (y >> 4) & 1, pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
	return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (b & 15) | (((b >> 4) & 1) << 5) | (((b >> 5) & 7) << 8) | (b >> 8 << 12);
}
static u32 fb_pixel(int i, u32 x, u32 y) { return be32(g_fb[i] + tiled(x, y)); }

static void* render_thread(void* arg)
{
	(void)arg;
	for (u32 frame = 0; !g_render_quit; frame++)
	{
		if (!g_render) { usleep(5000); frame--; continue; }
		const int i = frame & 1;
		memset(g_fb[i], 0x40, FB_SPAN); /* the GPU resolving the game's picture */
		((VdSwapFn)(g_draw.stubs + 12))((u32)(uintptr_t)g_ring, (u32)(uintptr_t)g_fetch[i], 1, 2, 0xBEEF0001,
			(u32)(uintptr_t)&g_fb_addr[i], (u32)(uintptr_t)&g_fmt, (u32)(uintptr_t)&g_colorspace,
			(u32)(uintptr_t)&g_w, (u32)(uintptr_t)&g_h);
		usleep(16000);
	}
	return NULL;
}

typedef u32 (*GetRawExFn)(u32, u8*, u32*, u16*);
static u32 draw_read(u8* out)
{
	for (int i = 0; i < 300; i++)
	{
		u32 len = 32;
		u16 st = 0;
		memset(out, 0, 32);
		if (((GetRawExFn)g_draw.stubs)(5, out, &len, &st) != 0)
			return 0;
		if (len)
			return len;
		usleep(5000);
	}
	return 0;
}

static void press(u16 buttons, int ms)
{
	g_buttons = buttons;
	usleep(ms * 1000);
	g_buttons = 0;
	usleep(150000);
}

static u16 game_buttons(void)
{
	u8 st[16];
	memset(st, 0xAA, sizeof(st));
	if (((u32 (*)(u32, u32, u8*))(g_draw.stubs + 8))(0, 1, st) != 0)
		return 0xFFFF;
	return (u16)(st[4] << 8 | st[5]);
}

/* GRID_PPM=file.ppm saves what the first front buffer shows (untiled). */
static void save_frame(const char* path)
{
	FILE* f = path ? fopen(path, "wb") : NULL;
	if (!f)
		return;
	fprintf(f, "P6 %d %d 255\n", FB_W, FB_H);
	for (u32 y = 0; y < FB_H; y++)
		for (u32 x = 0; x < FB_W; x++)
		{
			const u32 v = fb_pixel(0, x, y);
			const u8 rgb[3] = {(u8)(v >> 16), (u8)(v >> 8), (u8)v};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

static int count_files(const char* xbox_dir)
{
	char p[600];
	host_path(xbox_dir, p, sizeof(p));
	DIR* d = opendir(p);
	if (!d) return -1;
	int n = 0;
	struct dirent* e;
	while ((e = readdir(d)))
		n += e->d_name[0] != '.';
	closedir(d);
	return n;
}


/* The grid's colours as the A8R8G8B8 front buffer holds them. */
enum { C_TITLE = 0xFF1E468C, C_CELL = 0xFF343A48, C_CELL_ON = 0xFF24843A, C_CURSOR = 0xFFFFCC00, C_GAME = 0x40404040 };
/* At 640x480 the grid is drawn 1x at (32, 80). */
#define GX(x) (32 + (x))
#define GY(y) (80 + (y))

/* A .tpi picture: "TPI1", width, height, big-endian ARGB pixels. pattern 0:
 * a plain gradient (R from x, G from y, B 0x80); otherwise one colour. */
static void write_tpi(const char* xbox_path, int w, int h, u32 colour, const char* magic, int extra)
{
	char p[600];
	host_path(xbox_path, p, sizeof(p));
	for (char* c = p + strlen(g_root) + 1; *c; c++)
		if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
	FILE* f = fopen(p, "wb");
	if (!f)
		return;
	u8 head[8] = {0, 0, 0, 0, (u8)(w >> 8), (u8)w, (u8)(h >> 8), (u8)h};
	memcpy(head, magic, 4);
	fwrite(head, 1, 8, f);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const u32 v = colour ? colour : 0xFF000080u | (u32)((x * 2) & 0xFF) << 16 | (u32)((y * 6) & 0xFF) << 8;
			const u8 px[4] = {(u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v};
			fwrite(px, 1, 4, f);
		}
	for (int i = 0; i < extra; i++)
		fputc(0, f);
	fclose(f);
}

static int run_grid(u32 (*entry)(void*, u32, void*))
{
	make_draw_game(&g_draw);
	for (int i = 0; i < 2; i++)
	{
		g_fb[i] = aligned_alloc(4096, FB_SPAN);
		g_fb_addr[i] = (u32)(uintptr_t)g_fb[i];
		/* xe_gpu_texture_fetch_t: tiled, pitch 640/32, k_8_8_8_8, 8in32, 640x480, .zyxw */
		g_fetch[i][0] = (1u << 31) | ((FB_W / 32) << 22) | 2;
		g_fetch[i][1] = 6 | (2u << 6) | (g_fb_addr[i] & 0xFFFFF000u);
		g_fetch[i][2] = (FB_W - 1) | ((FB_H - 1) << 13);
		g_fetch[i][3] = (2 | (1 << 3) | (0 << 6) | (3 << 9)) << 1;
	}
	g_exec_module = g_draw.ldr;
	g_title_id = 0x4D530A13;
	CHECK(entry((void*)(uintptr_t)g_base, 1, NULL) == 1);
	CHECK(g_draw.slots[6] != g_draw.real[3]);      /* VdSwap import hooked */
	CHECK(g_draw.slots[4] != g_draw.real[2]);      /* XamInputGetState import hooked */
	CHECK(g_draw.slots[0] != g_draw.real[0]);      /* and the toy pad */

	/* The game wakes the toy pad, so the grid knows it is LEGO Dimensions. */
	u8 pkt[32], rp[32];
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x11; pkt[2] = 0x55; pkt[3] = 0x0F; pkt[4] = 0xB0; pkt[5] = 0x01;
	CHECK(((u32 (*)(u32, u8*, u32))(g_draw.stubs + 4))(5, pkt, 32) == 0);
	CHECK(draw_read(rp) == 32 && rp[2] == 0x55);
	for (int i = 0; i < 300 && !note_contains("ready"); i++)
		usleep(20000);
	CHECK(note_contains("LB+RB+Y for the grid"));

	/* The player's own pictures (none ship with Toy Portal 360): Batman by
	 * name, Gandalf by his number (2), and two broken ones. */
	write_tpi("TPHDD:\\ToyPortal\\Pictures\\Dimensions\\Batman.tpi", 128, 38, 0, "TPI1", 0);
	write_tpi("TPHDD:\\ToyPortal\\Pictures\\Dimensions\\2.tpi", 64, 20, 0xFF10C0F0u, "TPI1", 0);
	write_tpi("TPHDD:\\ToyPortal\\Pictures\\Dimensions\\Wyldstyle.tpi", 16, 16, 0xFFFF0000u, "\x89PNG", 0);
	write_tpi("TPHDD:\\ToyPortal\\Pictures\\Dimensions\\Aquaman.tpi", 128, 38, 0xFFFF0000u, "TPI1", 4);

	pthread_t rt;
	g_render = 1;
	pthread_create(&rt, NULL, render_thread, NULL);
	usleep(200000);
	CHECK(g_swap_calls > 3);
	/* All ten arguments reach the real VdSwap, the last two from the stack. */
	CHECK(g_swap_args[0] == (u32)(uintptr_t)g_ring && g_swap_args[4] == 0xBEEF0001 &&
		g_swap_args[8] == (u32)(uintptr_t)&g_w && g_swap_args[9] == (u32)(uintptr_t)&g_h);
	g_buttons = 0x4000; /* X */
	usleep(60000);
	CHECK(game_buttons() == 0x4000); /* the game sees the controller */
	g_buttons = 0;

	/* LB+RB+Y opens the grid. */
	press(0x0100 | 0x0200 | 0x8000, 250);
	CHECK(wait_log("Grid: drawing on front buffer", 5000));
	CHECK(log_contains("tiled format 6 endian 2 swizzle 60A at 1x"));
	g_buttons = 0x4000;
	usleep(60000);
	CHECK(game_buttons() == 0); /* held back while the grid is open */
	g_buttons = 0;
	CHECK(wait_log("Grid: 0 Dimensions figure file(s) found", 5000));
	CHECK(log_contains("Grid: picture Batman.tpi shown (128x38)"));
	CHECK(log_contains("Grid: picture 2.tpi shown (64x20)"));
	CHECK(log_contains("Grid: picture Wyldstyle.tpi skipped: not a .tpi file"));
	CHECK(log_contains("Grid: picture Aquaman.tpi skipped: too big"));

	/* Stop the game's frames: the grid keeps drawing on both buffers. */
	g_render = 0;
	usleep(300000);
	for (int i = 0; i < 2; i++)
	{
		CHECK(fb_pixel(i, GX(4), GY(4)) == C_TITLE);
		CHECK(fb_pixel(i, GX(7 + 2), GY(30 + 50)) == C_CELL);
		CHECK(fb_pixel(i, GX(7 - 1), GY(30 + 20)) == C_CURSOR);   /* cursor on the first cell */
		CHECK(fb_pixel(i, GX(7 + 142 + 130), GY(30 + 50)) == C_CELL);
		/* Batman's picture fills the top of his square, pixel for pixel. */
		CHECK(fb_pixel(i, GX(7 + 4), GY(30 + 4)) == 0xFF000080u);
		CHECK(fb_pixel(i, GX(7 + 4 + 10), GY(30 + 4 + 5)) == 0xFF141E80u);
		CHECK(fb_pixel(i, GX(7 + 4 + 127), GY(30 + 4 + 37)) == (0xFF000080u | 0xFEu << 16 | 0xDEu << 8));
		/* Gandalf's smaller one is centred: (128-64)/2 = 32, (38-20)/2 = 9. */
		CHECK(fb_pixel(i, GX(149 + 4 + 32), GY(30 + 4 + 9)) == 0xFF10C0F0u);
		CHECK(fb_pixel(i, GX(149 + 4 + 31), GY(30 + 4 + 9)) == C_CELL);
		CHECK(fb_pixel(i, GX(149 + 4 + 32 + 63), GY(30 + 4 + 9 + 19)) == 0xFF10C0F0u);
		/* The broken ones show no picture. */
		CHECK(fb_pixel(i, GX(291 + 70), GY(30 + 20)) != 0xFFFF0000u && fb_pixel(i, GX(433 + 70), GY(30 + 20)) != 0xFFFF0000u);
		CHECK(fb_pixel(i, 5, 5) == C_GAME && fb_pixel(i, 635, 475) == C_GAME); /* rest untouched */
	}
	g_render = 1;

	/* A: makes Batman (the first character) and puts him on the centre pad. */
	press(0x1000, 120);
	char path[64];
	snprintf(path, sizeof(path), "TPHDD:\\ToyPortal\\Dimensions\\Batman.bin");
	u8 tag[180];
	for (int i = 0; i < 100 && !file_bytes(path, tag, 180); i++)
		usleep(30000);
	CHECK(file_bytes(path, tag, 180));
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[4] == 1 && rp[7] == 0);
	CHECK(wait_log("Grid: Made and placed Batman", 3000));
	g_render = 0;
	usleep(300000);
	CHECK(fb_pixel(0, GX(7 + 2), GY(30 + 20)) == C_CELL_ON && fb_pixel(1, GX(7 + 2), GY(30 + 20)) == C_CELL_ON);
	CHECK(fb_pixel(0, GX(7 + 4 + 10), GY(30 + 4 + 5)) == 0xFF141E80u); /* the picture stays */
	save_frame(getenv("GRID_PPM"));
	g_render = 1;

	/* A again takes him off. */
	press(0x1000, 120);
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[7] == 1);
	CHECK(wait_log("Grid: Removed Batman", 3000));
	g_render = 0;
	usleep(300000);
	CHECK(fb_pixel(0, GX(7 + 2), GY(30 + 20)) == C_CELL);
	g_render = 1;

	/* RB turns the page; A there makes a different figure; A once more on
	 * Batman's page uses his file instead of making another. */
	press(0x0200, 120);
	press(0x1000, 120);
	CHECK(wait_log("Grid: Made and placed", 3000));
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[7] == 0);
	CHECK(count_files("TPHDD:\\ToyPortal\\Dimensions") == 2);
	press(0x0100, 120);
	press(0x1000, 120);
	CHECK(wait_log("Grid: Placed Batman", 3000));
	CHECK(count_files("TPHDD:\\ToyPortal\\Dimensions") == 2);
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[4] == 2 && rp[7] == 0); /* centre taken: left pad */

	/* Fill the toy pad (7 spots): Gandalf, Wyldstyle, Aquaman, Benny, Bart. */
	static const struct { u16 move; const char* name; } fill[5] = {
		{0x0008, "Gandalf"}, {0x0008, "Wyldstyle"}, {0x0008, "Aquaman"},
		{0x0002, "Benny"}, {0x0004, "Bart Simpson"}};
	for (int i = 0; i < 5; i++)
	{
		char want[64];
		press(fill[i].move, 120);
		press(0x1000, 120);
		snprintf(want, sizeof(want), "Grid: Made and placed %s", fill[i].name);
		CHECK(wait_log(want, 3000));
		CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[7] == 0);
	}
	/* An eighth (Bane) swaps out the one on longest: the page-2 figure on the
	 * centre pad. The game sees it leave, then Bane arrive on that pad. */
	press(0x0004, 120);
	press(0x1000, 120);
	CHECK(wait_log("Grid: Swapping: ", 3000));
	CHECK(log_contains(" off, Bane on"));
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[4] == 1 && rp[7] == 1);
	CHECK(wait_log("Grid: Made and placed Bane", 3000));
	CHECK(draw_read(rp) == 32 && rp[2] == 0x56 && rp[4] == 1 && rp[7] == 0);
	CHECK(!log_contains("toy pad is full"));

	/* B closes it: the game gets the controller back and its frames win. */
	press(0x2000, 120);
	CHECK(wait_log("Grid closed (B)", 3000));
	g_buttons = 0x4000;
	usleep(60000);
	CHECK(game_buttons() == 0x4000);
	g_buttons = 0;
	usleep(200000);
	g_render = 0;
	usleep(200000);
	CHECK(fb_pixel(0, GX(4), GY(4)) == C_GAME && fb_pixel(1, GX(4), GY(4)) == C_GAME);

	g_render_quit = 1;
	pthread_join(rt, NULL);
	CHECK(entry((void*)(uintptr_t)g_base, 0, NULL) == 1);
	usleep(200000);
	CHECK(g_threads == 0);
	printf("grid: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}

typedef u32 (*Entry)(void*, u32, void*);

int main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 4) { printf("usage: run_xex <file.xex> <dashlaunch|sideload> <dir>\n"); return 2; }
	g_root = argv[3];
	const int lookup = strcmp(argv[2], "lookup") == 0;
	const int grid = strcmp(argv[2], "grid") == 0;
	const int modules = strcmp(argv[2], "modules") == 0;
	const int sideload = lookup || modules || grid || strcmp(argv[2], "sideload") == 0;
	g_in_title = sideload;
	load_xex(argv[1]);
	const Entry entry = (Entry)(uintptr_t)g_entry;
	CHECK(g_flags == (sideload ? 0x9u : 0x8u)); /* TITLE|DLL vs DLL */

	if (grid)
		return run_grid(entry);

	if (modules)
	{
		/* A game that loads more of its code after it starts: the executable has
		 * no portal imports, and a module loaded later imports GetRawEx/SetRawEx. */
		static Game exe, mod;
		static u8 mod_header[16 + 256];
		make_lookup_game(&exe, 8);
		make_game(&mod, 1, 9);
		memcpy(mod_header, "XEX2", 4);
		memcpy(mod_header + 16, mod.header, 256);
		*(u32*)(mod.ldr + 0x58) = (u32)(uintptr_t)mod_header;
		*(u32*)(mod.ldr + 0x1C) = 0x88000000; /* ImageBase */
		*(u32*)(mod.ldr + 0x20) = 0x00100000; /* SizeOfNtImage */
		g_exec_module = exe.ldr;
		g_title_id = 0x41560930;
		CHECK(entry((void*)(uintptr_t)g_base, 1, NULL) == 1);
		usleep(300000);
		CHECK(!note_contains("ready"));
		/* the game loads its other module: InLoadOrderLinks exe <-> mod */
		*(u32*)(exe.ldr + 0) = (u32)(uintptr_t)mod.ldr; *(u32*)(exe.ldr + 4) = (u32)(uintptr_t)mod.ldr;
		*(u32*)(mod.ldr + 0) = (u32)(uintptr_t)exe.ldr; *(u32*)(mod.ldr + 4) = (u32)(uintptr_t)exe.ldr;
		for (int i = 0; i < 300 && mod.slots[0] == mod.real[0]; i++)
			usleep(20000);
		CHECK(mod.slots[0] != mod.real[0] && mod.slots[2] != mod.real[1]); /* module patched */
		if (mod.slots[0] == mod.real[0])
		{
			printf("modules: %d passed, %d failed (module never patched)\n", g_pass, g_fail);
			return 1;
		}
		for (int i = 0; i < 300 && !note_contains("ready"); i++)
			usleep(20000);
		CHECK(note_contains("Toy Portal 360 ready"));
		u8 pk[32], rp[32];
		memset(pk, 0, 32);
		pk[0] = 0x0B; pk[1] = 0x11; pk[2] = 0x55; pk[3] = 0x0F; pk[4] = 0xB0; pk[5] = 0x01;
		CHECK(((SetRawEx)(mod.stubs + 4))(5, pk, 32) == 0);
		int got = 0;
		for (int i = 0; i < 200 && !got; i++)
		{
			u32 len = 32; u16 stt = 0;
			memset(rp, 0, 32);
			got = ((GetRawEx)mod.stubs)(5, rp, &len, &stt) == 0 && len == 32;
			if (!got) usleep(5000);
		}
		CHECK(got && rp[0] == 0x0B && rp[2] == 0x55 && memcmp(rp + 5, "(c) LEGO 2014", 13) == 0);
		CHECK(g_real_calls == 0);
		CHECK(entry((void*)(uintptr_t)g_base, 0, NULL) == 1);
		usleep(200000);
		printf("modules: %d passed, %d failed\n", g_pass, g_fail);
		return g_fail ? 1 : 0;
	}

	if (lookup)
	{
		/* LEGO Dimensions on a console: no toy-pad imports, it looks them up. */
		static Game look;
		make_lookup_game(&look, 7);
		g_exec_module = look.ldr;
		g_title_id = 0x5752084B;
		CHECK(entry((void*)(uintptr_t)g_base, 1, NULL) == 1);
		CHECK(look.slots[0] != look.real[0]); /* XexGetProcedureAddress import hooked */
		CHECK(!note_contains("ready"));       /* not a toy game until it asks */
		void* get = NULL; void* set = NULL; void* other = NULL;
		CHECK(((GetProc)look.stubs)((void*)0x1234, 0x52A, &get) == 0);
		CHECK(((GetProc)look.stubs)((void*)0x1234, 0x52B, &set) == 0);
		CHECK(((GetProc)look.stubs)((void*)0x1234, 0x290, &other) == 0);
		CHECK(get && get != (void*)x_NonControllerGetRawEx && set && set != (void*)x_NonControllerSetRawEx);
		CHECK(other == (void*)x_XNotifyQueueUI); /* other lookups untouched */
		u8 pk[32], rp[32];
		memset(pk, 0, 32);
		pk[0] = 0x0B; pk[1] = 0x11; pk[2] = 0x55; pk[3] = 0x0F; pk[4] = 0xB0; pk[5] = 0x01;
		for (int i = 0; i < 300 && !note_contains("ready"); i++)
			usleep(20000);
		CHECK(note_contains("Toy Portal 360 ready"));
		CHECK(((SetRawEx)set)(5, pk, 32) == 0);
		int got = 0;
		for (int i = 0; i < 200 && !got; i++)
		{
			u32 len = 32; u16 stt = 0;
			memset(rp, 0, 32);
			got = ((GetRawEx)get)(5, rp, &len, &stt) == 0 && len == 32;
			if (!got) usleep(5000);
		}
		CHECK(got && rp[0] == 0x0B && rp[2] == 0x55 && memcmp(rp + 5, "(c) LEGO 2014", 13) == 0);
		CHECK(g_real_calls == 0);
		CHECK(entry((void*)(uintptr_t)g_base, 0, NULL) == 1);
		usleep(200000);
		CHECK(log_contains("Title 5752084B: hooked the portal functions it looked up, mode=emulate"));
		printf("lookup: %d passed, %d failed\n", g_pass, g_fail);
		return g_fail ? 1 : 0;
	}

	static Game dim, sky, inf, other;
	make_game(&dim, 1, 1);
	make_game(&sky, 0, 2);
	make_game(&other, 1, 3);
	make_game(&inf, 1, 4);
	u8 pkt[32], reply[32];

	if (sideload)
	{
		/* Peer Pressure: game in memory, DLL entry runs before the game. */
		g_exec_module = dim.ldr;
		g_title_id = 0x4D530A13;
		CHECK(entry((void*)(uintptr_t)g_base, 1, NULL) == 1);
		CHECK(dim.slots[0] != dim.real[0] && dim.slots[2] != dim.real[1]); /* hooked inside DllMain */
	}
	else
	{
		CHECK(entry((void*)(uintptr_t)g_base, 1, NULL) == 1);
		g_exec_module = dim.ldr;
		g_title_id = 0x4D530A13;
		for (int i = 0; i < 300 && dim.slots[0] == dim.real[0]; i++)
			usleep(20000);
		CHECK(dim.slots[0] != dim.real[0]);
	}

	/* Dimensions wake through the hooked import: the real binary answers. */
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x11; pkt[2] = 0x55; pkt[3] = 0x0F; pkt[4] = 0xB0; pkt[5] = 0x01;
	CHECK(((SetRawEx)(dim.stubs + 4))(5, pkt, 32) == 0);
	CHECK(read_ex(&dim, reply) == 32 && reply[0] == 0x0B && reply[1] == 0x11 && reply[2] == 0x55 &&
		memcmp(reply + 5, "(c) LEGO 2014", 13) == 0);
	CHECK(g_real_calls == 0);
	for (int i = 0; i < 300 && !note_contains("ready"); i++)
		usleep(20000);
	CHECK(note_contains("Toy Portal 360 ready - hold LB+RB+Back for the menu"));

	/* Menu: More > Create > "gandalf" > Create > Place it > Left pad, then B. */
	{
		const int b[] = {2, 0, 2, 0, 0, -1};
		script(b, 6, "gandalf");
		open_menu();
	}
	u8 tag[180];
	CHECK(file_bytes("TPHDD:\\ToyPortal\\Dimensions\\Gandalf.bin", tag, 180));
	CHECK(g_keyboard_refused > 0); /* refused once, then retried */
	CHECK(shown_contains("Left: Gandalf"));
	CHECK(shown_contains("1 of 1\n\nGandalf\n\nPress B to go back."));
	CHECK(note_contains("Placed Gandalf.bin"));
	CHECK(read_ex(&dim, reply) == 32 && reply[2] == 0x56 && reply[1] == 14 && reply[4] == 2 && reply[7] == 0);

	/* Model id request: decrypts to Gandalf (2) in the real binary. */
	/* The game writes page 0x10; it reaches the file. */
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x0B; pkt[2] = 0x55; pkt[3] = 0x08; pkt[4] = 0xD3; pkt[5] = 0x02; pkt[6] = 1; pkt[7] = 0x10;
	pkt[8] = 0xAB; pkt[9] = 0xCD; pkt[10] = 0xEF; pkt[11] = 0x12;
	((SetRawEx)(dim.stubs + 4))(5, pkt, 32);
	CHECK(read_ex(&dim, reply) == 32 && reply[2] == 0x55 && reply[3] == 0x02);
	usleep(1200000);
	CHECK(file_bytes("TPHDD:\\ToyPortal\\Dimensions\\Gandalf.bin", tag, 180) && tag[0x40] == 0xAB && tag[0x43] == 0x12);

	if (!sideload)
	{
		/* Switch to a Skylanders game (non-Ex functions, bare frames). */
		g_exec_module = sky.ldr;
		g_title_id = 0x41560883;
		for (int i = 0; i < 300 && sky.slots[0] == sky.real[0]; i++)
			usleep(20000);
		CHECK(sky.slots[0] != sky.real[0]);
		memset(pkt, 0, 32);
		pkt[0] = 'A'; pkt[1] = 1;
		((SetRaw)(sky.stubs + 4))(32, pkt);
		u32 len = 32;
		u16 st = 0;
		CHECK(((GetRaw)sky.stubs)(&st, &len, reply) == 0 && len == 32 && reply[0] == 'A' && reply[2] == 0xFF && st == 1);
		const int b[] = {2, 0, 2, 0, -1};
		script(b, 5, "spyro");
		open_menu();
		CHECK(shown_contains("On the portal:\n  Spyro"));
		int arrived = 0;
		for (int i = 0; i < 60 && !arrived; i++)
		{
			len = 32;
			((GetRaw)sky.stubs)(&st, &len, reply);
			arrived = len && reply[0] == 'S' && (reply[1] & 3) == 1;
			usleep(25000);
		}
		CHECK(arrived);

		/* Disney Infinity: the binary's SHA-1/AES/CRC create and read a figure. */
		g_exec_module = inf.ldr;
		g_title_id = 0x4D5309C4;
		for (int i = 0; i < 300 && inf.slots[0] == inf.real[0]; i++)
			usleep(20000);
		memset(pkt, 0, 32);
		pkt[0] = 0xFF; pkt[1] = 0x03; pkt[2] = 0x80; pkt[3] = 0x01;
		((SetRawEx)(inf.stubs + 4))(5, pkt, 32);
		CHECK(read_ex(&inf, reply) == 32 && reply[0] == 0xAA && reply[1] == 0x15);
		{
			const int b[] = {2, 0, 2, 0, 1, -1}; /* ... Place it, Player 2 */
			script(b, 6, "sulley");
			open_menu();
		}
		CHECK(shown_contains("Player 2: Sulley"));
		CHECK(shown_contains("Sulley (1.0)"));
		CHECK(read_ex(&inf, reply) == 32 && reply[0] == 0xAB && reply[2] == 3);
		u8 fig[320];
		CHECK(file_bytes("TPHDD:\\ToyPortal\\Infinity\\Sulley.bin", fig, 320));

		/* A protocol it doesn't emulate goes to the real XAM functions. */
		g_exec_module = other.ldr;
		g_title_id = 0x12345678;
		for (int i = 0; i < 300 && other.slots[0] == other.real[0]; i++)
			usleep(20000);
		const int before = g_real_calls;
		memset(pkt, 0, 32);
		pkt[0] = 0x42;
		((SetRawEx)(other.stubs + 4))(5, pkt, 32);
		CHECK(g_real_calls == before + 1);
	}

	/* Unload (DashLaunch: never at run time; Peer Pressure: on game exit). */
	CHECK(entry((void*)(uintptr_t)g_base, 0, NULL) == 1);
	usleep(200000);
	CHECK(g_threads == 0);
	if (sideload)
		CHECK(g_system_threads == 0 && g_title_threads >= 2);
	else
		CHECK(g_system_threads >= 2 && g_title_threads == 0);

	/* The binary's own printf wrote these. */
	CHECK(log_contains("Detected Dimensions (device 5, frames wrapped, prefix 0B)"));
	CHECK(log_contains(sideload ? "ToyPortal360 started (Peer Pressure sideload) in title 4D530A13" :
	                              "ToyPortal360 started (DashLaunch plugin)"));
	CHECK(log_contains("Title 4D530A13: hooked 2 portal import(s), mode=emulate"));
	if (!sideload)
		CHECK(log_contains("Unrecognised portal protocol (first byte 42)"));

	printf("%s: %d passed, %d failed\n", argv[2], g_pass, g_fail);
	return g_fail ? 1 : 0;
}
