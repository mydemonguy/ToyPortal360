// A small fake of the Xbox 360 runtime (kernel, XAM, Win32 file API, system
// UI) so the plugin's Xbox-only code can run under qemu-ppc for testing.
#include <errno.h>
#include "xdk_control.h"
#include "../../src/xbox/kernel.h"
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <deque>
#include <string>
#include <vector>

// ---- control state ----------------------------------------------------------

static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static std::string s_root = "/tmp";
static std::deque<int> s_boxes;
static std::deque<std::string> s_keys;
static std::vector<fake::Shown> s_shown;
static std::vector<std::string> s_notes;
static volatile int s_threads = 0;
static PLDR_DATA_TABLE_ENTRY s_exec = NULL;
static DWORD s_title_id = 0;

namespace fake
{
	volatile WORD buttons = 0;
	volatile int real_get_calls = 0, real_set_calls = 0;
	volatile int system_threads = 0, title_threads = 0;

	void set_root(const char* d) { s_root = d; }
	std::string host_path(const char* p)
	{
		std::string s(p);
		const size_t colon = s.find(':');
		std::string out = s_root + "/" + s.substr(0, colon) + "/";
		for (size_t i = colon + 1; i < s.size(); i++)
		{
			if (s[i] == '\\')
			{
				if (i != colon + 1)
					out += '/';
			}
			else
				out += s[i];
		}
		return out;
	}
	void script_box(int a) { pthread_mutex_lock(&s_mx); s_boxes.push_back(a); pthread_mutex_unlock(&s_mx); }
	void script_keyboard(const char* t) { pthread_mutex_lock(&s_mx); s_keys.push_back(t); pthread_mutex_unlock(&s_mx); }
	int script_left() { pthread_mutex_lock(&s_mx); int n = (int)(s_boxes.size() + s_keys.size()); pthread_mutex_unlock(&s_mx); return n; }
	std::vector<Shown> shown() { pthread_mutex_lock(&s_mx); std::vector<Shown> v = s_shown; pthread_mutex_unlock(&s_mx); return v; }
	std::vector<std::string> notifications() { pthread_mutex_lock(&s_mx); std::vector<std::string> v = s_notes; pthread_mutex_unlock(&s_mx); return v; }
	void set_title(void* e, DWORD id) { s_exec = (PLDR_DATA_TABLE_ENTRY)e; s_title_id = id; }
	int threads_running() { return s_threads; }
}

static std::string narrow(const wchar_t* w)
{
	std::string s;
	for (; w && *w; w++)
		s += *w < 128 ? (char)*w : '?';
	return s;
}

// ---- threads, locks, time ---------------------------------------------------

extern "C" { PLDR_DATA_TABLE_ENTRY* XexExecutableModuleHandle = &s_exec; }

void InitializeCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_t* m = new pthread_mutex_t;
	pthread_mutex_init(m, &a);
	cs->x = (long)m;
}
void EnterCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_lock((pthread_mutex_t*)cs->x); }
void LeaveCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_unlock((pthread_mutex_t*)cs->x); }

DWORD GetTickCount()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (DWORD)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
void Sleep(DWORD ms) { usleep(ms * 1000); }

struct Handle
{
	int kind; // 1 file, 2 dir, 3 thread
	int fd;
	DIR* dir;
	std::string dir_path;
	pthread_t thread;
};

struct ThreadArgs { LPTHREAD_START_ROUTINE fn; LPVOID arg; };
static void* thread_main(void* p)
{
	ThreadArgs a = *(ThreadArgs*)p;
	delete (ThreadArgs*)p;
	__sync_fetch_and_add(&s_threads, 1);
	a.fn(a.arg);
	__sync_fetch_and_sub(&s_threads, 1);
	return NULL;
}

extern "C" DWORD ExCreateThread(PHANDLE h, DWORD, LPDWORD id, PVOID, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags)
{
	if (flags & 0x2)
		fake::system_threads++;
	else
		fake::title_threads++;
	Handle* hd = new Handle();
	hd->kind = 3;
	ThreadArgs* a = new ThreadArgs;
	a->fn = fn;
	a->arg = arg;
	pthread_create(&hd->thread, NULL, thread_main, a);
	if (id) *id = 1;
	*h = hd;
	return 0;
}
extern "C" VOID XapiThreadStartup(VOID (*)(VOID*), PVOID) {}

DWORD WaitForSingleObject(HANDLE h, DWORD)
{
	Handle* hd = (Handle*)h;
	if (hd && hd->kind == 3)
		pthread_join(hd->thread, NULL);
	return 0;
}

BOOL CloseHandle(HANDLE h)
{
	Handle* hd = (Handle*)h;
	if (!hd || h == INVALID_HANDLE_VALUE)
		return FALSE;
	if (hd->kind == 1)
		close(hd->fd);
	if (hd->kind == 3)
		pthread_detach(hd->thread);
	delete hd;
	return TRUE;
}

// ---- files ------------------------------------------------------------------

static __thread DWORD s_last_error = 0;
DWORD GetLastError() { return s_last_error; }

HANDLE CreateFile(const char* path, DWORD access, DWORD, void*, DWORD disp, DWORD, HANDLE)
{
	const std::string p = fake::host_path(path);
	int flags = (access & (GENERIC_WRITE | FILE_APPEND_DATA)) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
	if (access & FILE_APPEND_DATA)
		flags |= O_APPEND;
	if (disp == CREATE_NEW)
		flags |= O_CREAT | O_EXCL;
	else if (disp == OPEN_ALWAYS)
		flags |= O_CREAT;
	const int fd = open(p.c_str(), flags, 0644);
	if (fd < 0)
	{
		s_last_error = errno == EEXIST ? ERROR_FILE_EXISTS : 3; // 3 = ERROR_PATH_NOT_FOUND
		return INVALID_HANDLE_VALUE;
	}
	Handle* h = new Handle();
	h->kind = 1;
	h->fd = fd;
	return h;
}
BOOL ReadFile(HANDLE h, void* b, DWORD n, DWORD* got, void*)
{
	const ssize_t r = read(((Handle*)h)->fd, b, n);
	if (got) *got = r < 0 ? 0 : (DWORD)r;
	return r >= 0;
}
BOOL WriteFile(HANDLE h, const void* b, DWORD n, DWORD* put, void*)
{
	const ssize_t r = write(((Handle*)h)->fd, b, n);
	if (put) *put = r < 0 ? 0 : (DWORD)r;
	return r >= 0;
}
DWORD SetFilePointer(HANDLE h, LONG d, LONG*, DWORD m)
{
	return (DWORD)lseek(((Handle*)h)->fd, d, m == FILE_END ? SEEK_END : SEEK_SET);
}
DWORD GetFileAttributes(const char* path)
{
	struct stat st;
	if (stat(fake::host_path(path).c_str(), &st) != 0)
		return INVALID_FILE_ATTRIBUTES;
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
BOOL CreateDirectory(const char* path, void*)
{
	return mkdir(fake::host_path(path).c_str(), 0755) == 0;
}
static BOOL fill_find(Handle* h, WIN32_FIND_DATA* fd)
{
	for (;;)
	{
		dirent* e = readdir(h->dir);
		if (!e)
			return FALSE;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		struct stat st;
		stat((h->dir_path + "/" + e->d_name).c_str(), &st);
		fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
		fd->nFileSizeLow = (DWORD)st.st_size;
		strncpy(fd->cFileName, e->d_name, 259);
		return TRUE;
	}
}
HANDLE FindFirstFile(const char* pattern, WIN32_FIND_DATA* fd)
{
	std::string p = fake::host_path(pattern);
	p = p.substr(0, p.rfind('/'));
	DIR* d = opendir(p.c_str());
	if (!d)
		return INVALID_HANDLE_VALUE;
	Handle* h = new Handle();
	h->kind = 2;
	h->dir = d;
	h->dir_path = p;
	if (!fill_find(h, fd))
	{
		closedir(d);
		delete h;
		return INVALID_HANDLE_VALUE;
	}
	return h;
}
BOOL FindNextFile(HANDLE h, WIN32_FIND_DATA* fd) { return fill_find((Handle*)h, fd); }
BOOL FindClose(HANDLE h) { closedir(((Handle*)h)->dir); delete (Handle*)h; return TRUE; }

extern "C" LONG ObCreateSymbolicLink(TP_ANSI_STRING* link, TP_ANSI_STRING*)
{
	// "\System??\TPHDD:" -> make <root>/TPHDD
	std::string l(link->Buffer, link->Length);
	std::string drive = l.substr(l.rfind('\\') + 1);
	drive = drive.substr(0, drive.size() - 1);
	mkdir((s_root + "/" + drive).c_str(), 0755);
	return 0;
}

// ---- CRT name differences ---------------------------------------------------

int _snprintf(char* b, size_t n, const char* f, ...)
{
	va_list a;
	va_start(a, f);
	const int r = vsnprintf(b, n, f, a);
	va_end(a);
	return r;
}
int _vsnprintf(char* b, size_t n, const char* f, va_list a) { return vsnprintf(b, n, f, a); }
int _snwprintf(wchar_t* b, size_t n, const wchar_t* f, ...)
{
	// MSVC's %s in a wide format means a wide string; glibc's means narrow.
	std::wstring fmt(f);
	for (size_t i = 0; (i = fmt.find(L"%s", i)) != std::wstring::npos; i += 3)
		fmt.replace(i, 2, L"%ls");
	// MSVC's %hs is a narrow string, which is glibc's plain %s here.
	for (size_t i = 0; (i = fmt.find(L"%hs", i)) != std::wstring::npos; i += 2)
		fmt.replace(i, 3, L"%s");
	va_list a;
	va_start(a, f);
	const int r = vswprintf(b, n, fmt.c_str(), a);
	va_end(a);
	return r;
}
char* _strupr(char* s) { for (char* p = s; *p; p++) *p = (char)toupper(*p); return s; }
char* _strlwr(char* s) { for (char* p = s; *p; p++) *p = (char)tolower(*p); return s; }
int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
char* strtok_s(char* s, const char* d, char** c) { return strtok_r(s, d, c); }

void __dcbst(int, void*) {}
void __icbi(int, void*) {}
void __sync() { __sync_synchronize(); }
void __isync() {}
unsigned long long __mftb() { return GetTickCount() * 12345ull; }

// ---- input and system UI ----------------------------------------------------

DWORD XInputGetState(DWORD user, XINPUT_STATE* st)
{
	if (user != 0)
		return 1167; // not connected
	memset(st, 0, sizeof(*st));
	st->Gamepad.wButtons = fake::buttons;
	return ERROR_SUCCESS;
}

// Like a console, the keyboard is refused once right after a message box,
// while that box is still closing.
static bool s_box_closing = false;

DWORD XShowMessageBoxUI(DWORD, LPCWSTR title, LPCWSTR text, DWORD, LPCWSTR*, DWORD, DWORD, MESSAGEBOX_RESULT* r, XOVERLAPPED* ov)
{
	pthread_mutex_lock(&s_mx);
	s_box_closing = true;
	fake::Shown s;
	s.title = narrow(title);
	s.text = narrow(text);
	s_shown.push_back(s);
	if (getenv("SHOW_UI"))
		printf("\n[%s]\n%s\n", s.title.c_str(), s.text.c_str());
	int a = -1;
	if (!s_boxes.empty())
	{
		a = s_boxes.front();
		s_boxes.pop_front();
	}
	pthread_mutex_unlock(&s_mx);
	ov->InternalLow = a < 0 ? 1223 /* ERROR_CANCELLED */ : 0;
	r->dwButtonPressed = a < 0 ? 0 : (DWORD)a;
	return ERROR_IO_PENDING;
}

DWORD XShowKeyboardUI(DWORD, DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR out, DWORD n, XOVERLAPPED* ov)
{
	pthread_mutex_lock(&s_mx);
	if (s_box_closing)
	{
		s_box_closing = false;
		pthread_mutex_unlock(&s_mx);
		return ERROR_ACCESS_DENIED;
	}
	std::string t = "\x1b";
	if (!s_keys.empty())
	{
		t = s_keys.front();
		s_keys.pop_front();
	}
	pthread_mutex_unlock(&s_mx);
	ov->InternalLow = t == "\x1b" ? 1223 : 0;
	DWORD i = 0;
	for (; t != "\x1b" && i < t.size() && i + 1 < n; i++)
		out[i] = t[i];
	out[i] = 0;
	return ERROR_IO_PENDING;
}
BOOL XHasOverlappedIoCompleted(XOVERLAPPED*) { return TRUE; }
DWORD XGetOverlappedResult(XOVERLAPPED* ov, DWORD*, BOOL) { return ov->InternalLow; }

// ---- XAM exports resolved by ordinal ----------------------------------------

static DWORD real_GetRaw(WORD* state, DWORD* len, BYTE*) { fake::real_get_calls++; *len = 0; *state = 1; return 0; }
static DWORD real_SetRaw(DWORD, BYTE*) { fake::real_set_calls++; return 0; }
static DWORD real_GetRawEx(DWORD, BYTE*, DWORD* len, WORD* state) { fake::real_get_calls++; *len = 0; *state = 1; return 0; }
static DWORD real_SetRawEx(DWORD, BYTE*, DWORD) { fake::real_set_calls++; return 0; }
static VOID real_XNotifyQueueUI(DWORD, DWORD, ULONGLONG, PWCHAR text, PVOID)
{
	pthread_mutex_lock(&s_mx);
	s_notes.push_back(narrow(text));
	if (getenv("SHOW_UI"))
		printf("\n(notification) %s\n", narrow(text).c_str());
	pthread_mutex_unlock(&s_mx);
}
static DWORD real_XamGetCurrentTitleId() { return s_title_id; }

extern "C" DWORD XexGetModuleHandle(PSZ name, PHANDLE h)
{
	if (strcmp(name, "xboxkrnl.exe") == 0)
	{
		*h = (HANDLE)0x5678;
		return 0;
	}
	if (strcmp(name, "xam.xex") != 0)
		return 0xC0000225;
	*h = (HANDLE)0x1234;
	return 0;
}
extern "C" DWORD XexGetProcedureAddress(HANDLE h, DWORD ord, PVOID out)
{
	if (h == (HANDLE)0x5678) // xboxkrnl.exe: only the lookup function itself
	{
		if (ord != 0x197)
			return 0xC0000139;
		*(void**)out = (void*)XexGetProcedureAddress;
		return 0;
	}
	void* f = NULL;
	switch (ord)
	{
	case kOrdXamInputNonControllerGetRaw: f = (void*)real_GetRaw; break;
	case kOrdXamInputNonControllerSetRaw: f = (void*)real_SetRaw; break;
	case kOrdXamInputNonControllerGetRawEx: f = (void*)real_GetRawEx; break;
	case kOrdXamInputNonControllerSetRawEx: f = (void*)real_SetRawEx; break;
	case kOrdXNotifyQueueUI: f = (void*)real_XNotifyQueueUI; break;
	case kOrdXamGetCurrentTitleId: f = (void*)real_XamGetCurrentTitleId; break;
	}
	*(void**)out = f;
	return f ? 0 : 0xC0000225;
}

// The fake title keeps its import descriptor right at XexHeaderBase.
extern "C" PVOID RtlImageXexHeaderField(PVOID base, DWORD field)
{
	if (field != XEX_HEADER_IMPORTS)
		return NULL;
	// 16 bytes in when the header starts with a real "XEX2" signature.
	return *(DWORD*)base == 0x58455832 ? (BYTE*)base + 16 : base;
}

extern "C" BOOL MmIsAddressValid(PVOID p)
{
	return p != NULL;
}

// Lets the test know the real functions' addresses.
void* fake_real_function(int which)
{
	void* f = NULL;
	XexGetProcedureAddress(NULL, which, &f);
	return f;
}
