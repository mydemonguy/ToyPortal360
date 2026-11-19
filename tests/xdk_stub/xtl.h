// Minimal stand-in for the Xbox 360 SDK headers, ONLY for syntax-checking
// src/xbox with GCC (tests/check_xbox_syntax.sh). Not used for real builds.
#ifndef XDK_STUB_XTL_H
#define XDK_STUB_XTL_H
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <strings.h>

typedef unsigned long DWORD;
typedef unsigned short WORD, USHORT;
typedef unsigned char BYTE;
typedef int BOOL;
typedef long LONG, HRESULT;
typedef unsigned long long ULONGLONG;
typedef void VOID;
typedef void* PVOID, *LPVOID, *HANDLE;
typedef HANDLE* PHANDLE;
typedef char* PCHAR, *PSZ;
typedef wchar_t* PWSTR, *PWCHAR, *LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef DWORD* LPDWORD;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
#define WINAPI
#define __cdecl
#define APIENTRY
#define TRUE 1
#define FALSE 0
struct LIST_ENTRY { LIST_ENTRY* Flink; LIST_ENTRY* Blink; };
struct CRITICAL_SECTION { long x; };
void InitializeCriticalSection(CRITICAL_SECTION*);
void EnterCriticalSection(CRITICAL_SECTION*);
void LeaveCriticalSection(CRITICAL_SECTION*);
DWORD GetTickCount();
void Sleep(DWORD);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL CloseHandle(HANDLE);

#define ERROR_SUCCESS 0
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ACCESS_DENIED 5
#define ERROR_IO_PENDING 997
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 1
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_BEGIN 0
#define FILE_END 2
struct WIN32_FIND_DATA { DWORD dwFileAttributes; DWORD nFileSizeLow; char cFileName[260]; };
HANDLE CreateFile(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
DWORD SetFilePointer(HANDLE, LONG, LONG*, DWORD);
DWORD GetFileAttributes(const char*);
BOOL CreateDirectory(const char*, void*);
HANDLE FindFirstFile(const char*, WIN32_FIND_DATA*);
BOOL FindNextFile(HANDLE, WIN32_FIND_DATA*);
BOOL FindClose(HANDLE);

int _snprintf(char*, size_t, const char*, ...);
int _vsnprintf(char*, size_t, const char*, va_list);
int _snwprintf(wchar_t*, size_t, const wchar_t*, ...);
char* _strupr(char*);
char* _strlwr(char*);
int _stricmp(const char*, const char*);
int _strnicmp(const char*, const char*, size_t);
char* strtok_s(char*, const char*, char**);

#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
struct XINPUT_GAMEPAD { WORD wButtons; };
struct XINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; };
DWORD XInputGetState(DWORD, XINPUT_STATE*);

#define XUSER_INDEX_ANY 0xFF
#define XMB_NOICON 0
#define VKBD_DEFAULT 0
struct XOVERLAPPED { DWORD InternalLow; };
struct MESSAGEBOX_RESULT { DWORD dwButtonPressed; };
DWORD XShowMessageBoxUI(DWORD, LPCWSTR, LPCWSTR, DWORD, LPCWSTR*, DWORD, DWORD, MESSAGEBOX_RESULT*, XOVERLAPPED*);
DWORD XShowKeyboardUI(DWORD, DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, XOVERLAPPED*);
BOOL XHasOverlappedIoCompleted(XOVERLAPPED*);
DWORD XGetOverlappedResult(XOVERLAPPED*, DWORD*, BOOL);

#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#endif
