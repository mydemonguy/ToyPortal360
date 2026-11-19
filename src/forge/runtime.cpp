// ToyPortal360 - runtime for the Xenon Forge build: a small C library,
// MSVC-style printf, DllMain that runs C++ constructors, and threads.
#include "xtl.h"

extern "C" {

// ---- memory and strings ------------------------------------------------------

// The output pointers are volatile so clang cannot turn these loops back
// into calls to memcpy/memset, which would recurse.
void* memcpy(void* d, const void* s, size_t n)
{
	volatile unsigned char* o = (volatile unsigned char*)d;
	const unsigned char* i = (const unsigned char*)s;
	while (n--)
		*o++ = *i++;
	return d;
}

void* memmove(void* d, const void* s, size_t n)
{
	volatile unsigned char* o = (volatile unsigned char*)d;
	const unsigned char* i = (const unsigned char*)s;
	if (o < i)
		while (n--)
			*o++ = *i++;
	else
		while (n--)
			o[n] = i[n];
	return d;
}

void* memset(void* d, int c, size_t n)
{
	volatile unsigned char* o = (volatile unsigned char*)d;
	while (n--)
		*o++ = (unsigned char)c;
	return d;
}

int memcmp(const void* a, const void* b, size_t n)
{
	const unsigned char* x = (const unsigned char*)a;
	const unsigned char* y = (const unsigned char*)b;
	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

size_t strlen(const char* s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

char* strcpy(char* d, const char* s)
{
	char* o = d;
	while ((*o++ = *s++))
		;
	return d;
}

char* strncpy(char* d, const char* s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++)
		d[i] = s[i];
	for (; i < n; i++)
		d[i] = 0;
	return d;
}

char* strcat(char* d, const char* s)
{
	strcpy(d + strlen(d), s);
	return d;
}

int strcmp(const char* a, const char* b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
	for (; n; n--, a++, b++)
	{
		if (*a != *b || !*a)
			return (unsigned char)*a - (unsigned char)*b;
	}
	return 0;
}

char* strchr(const char* s, int c)
{
	for (;; s++)
	{
		if (*s == (char)c)
			return (char*)s;
		if (!*s)
			return NULL;
	}
}

char* strrchr(const char* s, int c)
{
	const char* last = NULL;
	for (;; s++)
	{
		if (*s == (char)c)
			last = s;
		if (!*s)
			return (char*)last;
	}
}

char* strstr(const char* h, const char* n)
{
	if (!*n)
		return (char*)h;
	for (; *h; h++)
	{
		const char* a = h;
		const char* b = n;
		while (*a && *b && *a == *b)
			a++, b++;
		if (!*b)
			return (char*)h;
	}
	return NULL;
}

char* strtok_s(char* s, const char* delim, char** context)
{
	if (!s)
		s = *context;
	if (!s)
		return NULL;
	while (*s && strchr(delim, *s))
		s++;
	if (!*s)
	{
		*context = NULL;
		return NULL;
	}
	char* start = s;
	while (*s && !strchr(delim, *s))
		s++;
	if (*s)
		*s++ = 0;
	*context = s;
	return start;
}

char* strtok(char* s, const char* delim)
{
	static char* context;
	return strtok_s(s, delim, &context);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

int _strnicmp(const char* a, const char* b, size_t n)
{
	for (; n; n--, a++, b++)
	{
		const int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
		if (x != y || !x)
			return x - y;
	}
	return 0;
}

int _stricmp(const char* a, const char* b) { return _strnicmp(a, b, (size_t)-1); }

char* _strupr(char* s)
{
	for (char* p = s; *p; p++)
		*p = (char)upper((unsigned char)*p);
	return s;
}

char* _strlwr(char* s)
{
	for (char* p = s; *p; p++)
		*p = (char)lower((unsigned char)*p);
	return s;
}

unsigned long strtoul(const char* s, char** end, int base)
{
	while (*s == ' ' || *s == '\t')
		s++;
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		s += 2;
		base = 16;
	}
	else if (base == 0)
	{
		base = s[0] == '0' ? 8 : 10;
	}
	unsigned long v = 0;
	for (;; s++)
	{
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (lower((unsigned char)*s) >= 'a' && lower((unsigned char)*s) <= 'z')
			d = lower((unsigned char)*s) - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * (unsigned long)base + (unsigned long)d;
	}
	if (end)
		*end = (char*)s;
	return v;
}

int atoi(const char* s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	const bool neg = *s == '-';
	if (*s == '-' || *s == '+')
		s++;
	const int v = (int)strtoul(s, NULL, 10);
	return neg ? -v : v;
}

// ---- printf (MSVC semantics, char or wchar_t output) -----------------------

struct Out
{
	void* buf;
	size_t cap;
	size_t len;
	bool wide;
	void put(unsigned int c)
	{
		if (len + 1 < cap)
		{
			if (wide)
				((wchar_t*)buf)[len] = (wchar_t)c;
			else
				((char*)buf)[len] = (char)c;
		}
		len++;
	}
};

static unsigned int fmt_char(const void* fmt, size_t i, bool wide)
{
	return wide ? (unsigned int)((const wchar_t*)fmt)[i] : (unsigned char)((const char*)fmt)[i];
}

static int vformat(void* buf, size_t cap, bool wide, const void* fmt, va_list ap)
{
	Out o = {buf, cap, 0, wide};
	for (size_t i = 0;;)
	{
		unsigned int c = fmt_char(fmt, i++, wide);
		if (!c)
			break;
		if (c != '%')
		{
			o.put(c);
			continue;
		}
		bool left = false, zero = false;
		for (;; i++)
		{
			c = fmt_char(fmt, i, wide);
			if (c == '-')
				left = true;
			else if (c == '0')
				zero = true;
			else
				break;
		}
		int width = 0;
		for (c = fmt_char(fmt, i, wide); c >= '0' && c <= '9'; c = fmt_char(fmt, ++i, wide))
			width = width * 10 + (int)(c - '0');
		int size = 0; // -1 = h, 1 = l/w
		for (c = fmt_char(fmt, i, wide); c == 'h' || c == 'l' || c == 'w'; c = fmt_char(fmt, ++i, wide))
			size = c == 'h' ? -1 : 1;
		const unsigned int conv = fmt_char(fmt, i++, wide);

		char num[16];
		int nlen = 0;
		bool negative = false;
		if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'p')
		{
			unsigned int v = va_arg(ap, unsigned int);
			if ((conv == 'd' || conv == 'i') && (int)v < 0)
			{
				negative = true;
				v = 0u - v;
			}
			const unsigned int base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : 10;
			const char* digits = conv == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
			do
			{
				num[nlen++] = digits[v % base];
				v /= base;
			} while (v);
			if (conv == 'p')
				while (nlen < 8)
					num[nlen++] = '0';
			int total = nlen + (negative ? 1 : 0);
			if (!left && !zero)
				for (; total < width; total++)
					o.put(' ');
			if (negative)
				o.put('-');
			if (!left && zero)
				for (; total < width; total++)
					o.put('0');
			while (nlen)
				o.put((unsigned char)num[--nlen]);
			for (; total < width; total++)
				o.put(' ');
		}
		else if (conv == 's')
		{
			// Wide string: %ls/%ws anywhere, or plain %s in a wide format.
			const bool arg_wide = size == 1 || (wide && size == 0);
			const void* s = va_arg(ap, const void*);
			if (!s)
				s = arg_wide ? (const void*)L"(null)" : (const void*)"(null)";
			int n = 0;
			while (arg_wide ? ((const wchar_t*)s)[n] : ((const char*)s)[n])
				n++;
			if (!left)
				for (int p = n; p < width; p++)
					o.put(' ');
			for (int k = 0; k < n; k++)
				o.put(arg_wide ? (unsigned int)((const wchar_t*)s)[k] : (unsigned char)((const char*)s)[k]);
			if (left)
				for (int p = n; p < width; p++)
					o.put(' ');
		}
		else if (conv == 'c')
		{
			o.put(va_arg(ap, unsigned int));
		}
		else if (conv == '%')
		{
			o.put('%');
		}
		else if (!conv)
		{
			break;
		}
	}
	// MSVC's _snprintf: -1 and no terminator when the output does not fit.
	if (o.len < cap)
	{
		if (wide)
			((wchar_t*)buf)[o.len] = 0;
		else
			((char*)buf)[o.len] = 0;
		return (int)o.len;
	}
	return -1;
}

int _vsnprintf(char* out, size_t n, const char* fmt, va_list ap) { return vformat(out, n, false, fmt, ap); }
int _vsnwprintf(wchar_t* out, size_t n, const wchar_t* fmt, va_list ap) { return vformat(out, n, true, fmt, ap); }

int _snprintf(char* out, size_t n, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int r = vformat(out, n, false, fmt, ap);
	va_end(ap);
	return r;
}

int _snwprintf(wchar_t* out, size_t n, const wchar_t* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int r = vformat(out, n, true, fmt, ap);
	va_end(ap);
	return r;
}

// ---- module entry --------------------------------------------------------------

// The linker defines the bounds of .init_array (Xenon Forge's link script).
typedef void (*CONSTRUCTOR)(void);
extern const CONSTRUCTOR __init_array_start[] __attribute__((weak));
extern const CONSTRUCTOR __init_array_end[] __attribute__((weak));

BOOL PluginDllMain(HANDLE module, DWORD reason, LPVOID reserved);
static CRITICAL_SECTION s_thread_lock;

// Called by Xenon Forge's crt-dll entry, which saves the console's 64-bit
// registers around it.
#undef DllMain
BOOL DllMain(HANDLE module, DWORD reason, LPVOID reserved)
{
	static bool constructed = false;
	if (reason == DLL_PROCESS_ATTACH && !constructed)
	{
		constructed = true;
		RtlInitializeCriticalSection(&s_thread_lock);
		for (const CONSTRUCTOR* c = __init_array_start; c < __init_array_end; c++)
			(*c)();
	}
	return PluginDllMain(module, reason, reserved);
}

// ---- threads -----------------------------------------------------------------

DWORD ExCreateThread(PHANDLE handle, DWORD stack, LPDWORD id, PVOID startup, LPTHREAD_START_ROUTINE start, LPVOID context, DWORD flags);

// No compiler atomics here: the console's loader rejects lwarx/stwcx. pairs
// that are not in its interrupt-protected form, so a critical section guards
// the slots instead.
struct ThreadStart
{
	LPTHREAD_START_ROUTINE fn;
	LPVOID arg;
	volatile int in_use;
};
static ThreadStart s_threads[8];

// Without an XAPI startup routine the kernel enters the thread here with
// r3 = context. A raw thread must end with ExTerminateThread.
static DWORD thread_entry(LPVOID context)
{
	ThreadStart* t = (ThreadStart*)context;
	RtlEnterCriticalSection(&s_thread_lock);
	LPTHREAD_START_ROUTINE fn = t->fn;
	LPVOID arg = t->arg;
	t->in_use = 0;
	RtlLeaveCriticalSection(&s_thread_lock);
	const DWORD code = fn(arg);
	ExTerminateThread(code);
	return code;
}

bool tp_start_thread(LPTHREAD_START_ROUTINE fn, LPVOID arg, HANDLE* out_handle, bool system_thread)
{
	for (int i = 0; i < 8; i++)
	{
		RtlEnterCriticalSection(&s_thread_lock);
		const bool taken = s_threads[i].in_use != 0;
		if (!taken)
		{
			s_threads[i].in_use = 1;
			s_threads[i].fn = fn;
			s_threads[i].arg = arg;
		}
		RtlLeaveCriticalSection(&s_thread_lock);
		if (taken)
			continue;
		HANDLE h = NULL;
		DWORD tid = 0;
		// 0x2: a system thread, which outlives the running title.
		if (ExCreateThread(&h, 0, &tid, NULL, thread_entry, &s_threads[i], system_thread ? 0x2 : 0) != 0 || !h)
		{
			s_threads[i].in_use = 0;
			return false;
		}
		if (out_handle)
			*out_handle = h;
		else
			CloseHandle(h);
		return true;
	}
	return false;
}

} // extern "C"
