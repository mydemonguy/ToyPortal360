// ToyPortal360 - the small C library the plugin needs under Xenon Forge,
// which has no C library for the console. Implemented in runtime.cpp.
// Formatting follows MSVC: in narrow functions %s is narrow and %ls wide;
// in wide functions %s is wide and %hs narrow.
#ifndef TOYPORTAL_FORGE_CRT_H
#define TOYPORTAL_FORGE_CRT_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);
char* strcat(char* d, const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* h, const char* n);
char* strtok(char* s, const char* delim);
char* strtok_s(char* s, const char* delim, char** context);
int _stricmp(const char* a, const char* b);
int _strnicmp(const char* a, const char* b, size_t n);
char* _strupr(char* s);
char* _strlwr(char* s);
int atoi(const char* s);
unsigned long strtoul(const char* s, char** end, int base);

int _vsnprintf(char* out, size_t n, const char* fmt, va_list ap);
int _snprintf(char* out, size_t n, const char* fmt, ...);
int _vsnwprintf(wchar_t* out, size_t n, const wchar_t* fmt, va_list ap);
int _snwprintf(wchar_t* out, size_t n, const wchar_t* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
