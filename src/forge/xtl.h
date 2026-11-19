// ToyPortal360 - XDK-style API for the open-source Xenon Forge toolchain
// (github.com/landaire/rust-xbox360). The plugin is written against the
// Xbox 360 SDK's names; this header maps them onto XAM and kernel exports,
// which Xenon Forge's import stubs (plus src/forge/forge.S) link by ordinal.
//
// Only used when building with TOYPORTAL_FORGE (see tools/build_forge.sh).
#ifndef TOYPORTAL_FORGE_XTL_H
#define TOYPORTAL_FORGE_XTL_H

#include <stddef.h>
#include <stdarg.h>
#include "forge_crt.h"

#ifdef __cplusplus
#define TP_EXTERN_C extern "C"
#else
#define TP_EXTERN_C
#endif

typedef unsigned long DWORD, *LPDWORD, *PDWORD, ULONG, ULONG_PTR;
typedef unsigned short WORD, USHORT;
typedef unsigned char BYTE, UCHAR;
typedef short SHORT;
typedef int BOOL;
typedef long LONG, HRESULT, NTSTATUS;
typedef unsigned long long ULONGLONG;
typedef void VOID, *PVOID, *LPVOID, *HANDLE, **PHANDLE;
typedef char CHAR, *PCHAR, *PSZ;
typedef const char* LPCSTR;
typedef wchar_t WCHAR, *PWSTR, *PWCHAR, *LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

#define WINAPI
#define APIENTRY
#define __cdecl
#define TRUE 1
#define FALSE 0

typedef struct _LIST_ENTRY { struct _LIST_ENTRY* Flink; struct _LIST_ENTRY* Blink; } LIST_ENTRY;

#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_PARAMETER 87
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
#define MAX_PATH 260
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1

// ---- kernel ------------------------------------------------------------------

// RTL_CRITICAL_SECTION: 0x1C bytes (see Xenon Forge's xboxkrnl/rtl.h).
typedef struct _CRITICAL_SECTION { DWORD opaque[7]; } CRITICAL_SECTION;
TP_EXTERN_C VOID RtlInitializeCriticalSection(CRITICAL_SECTION*);
TP_EXTERN_C VOID RtlEnterCriticalSection(CRITICAL_SECTION*);
TP_EXTERN_C VOID RtlLeaveCriticalSection(CRITICAL_SECTION*);
#define InitializeCriticalSection RtlInitializeCriticalSection
#define EnterCriticalSection RtlEnterCriticalSection
#define LeaveCriticalSection RtlLeaveCriticalSection

TP_EXTERN_C DWORD ExTerminateThread(DWORD code);

// ---- XAM's Win32 subset ----------------------------------------------------

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct _WIN32_FIND_DATA
{
	DWORD dwFileAttributes;
	FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
	DWORD nFileSizeHigh, nFileSizeLow;
	DWORD dwReserved0, dwReserved1;
	CHAR cFileName[MAX_PATH];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATA;

TP_EXTERN_C HANDLE CreateFileA(LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
TP_EXTERN_C BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
TP_EXTERN_C BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
TP_EXTERN_C DWORD SetFilePointer(HANDLE, LONG, LONG*, DWORD);
TP_EXTERN_C DWORD GetFileAttributesA(LPCSTR);
TP_EXTERN_C BOOL CreateDirectoryA(LPCSTR, void*);
TP_EXTERN_C HANDLE FindFirstFileA(LPCSTR, WIN32_FIND_DATA*);
TP_EXTERN_C BOOL FindNextFileA(HANDLE, WIN32_FIND_DATA*);
TP_EXTERN_C BOOL CloseHandle(HANDLE);
TP_EXTERN_C VOID Sleep(DWORD);
TP_EXTERN_C DWORD GetTickCount(VOID);
TP_EXTERN_C DWORD WaitForSingleObject(HANDLE, DWORD);
#define CreateFile CreateFileA
#define GetFileAttributes GetFileAttributesA
#define CreateDirectory CreateDirectoryA
#define FindFirstFile FindFirstFileA
#define FindNextFile FindNextFileA
#define FindClose CloseHandle

// ---- input -----------------------------------------------------------------

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
typedef struct _XINPUT_GAMEPAD
{
	WORD wButtons;
	BYTE bLeftTrigger, bRightTrigger;
	SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;
typedef struct _XINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
#define XINPUT_FLAG_GAMEPAD 1
TP_EXTERN_C DWORD XamInputGetState(DWORD user, DWORD flags, XINPUT_STATE* state);
static inline DWORD XInputGetState(DWORD user, XINPUT_STATE* state)
{
	return XamInputGetState(user, XINPUT_FLAG_GAMEPAD, state);
}

// ---- system UI ---------------------------------------------------------------

#define XUSER_INDEX_ANY 0xFF
#define XMB_NOICON 0
#define VKBD_DEFAULT 0
typedef struct _XOVERLAPPED
{
	DWORD InternalLow, InternalHigh, InternalContext;
	HANDLE hEvent;
	PVOID pCompletionRoutine;
	DWORD dwCompletionContext, dwExtendedError;
} XOVERLAPPED; // 0x1C bytes, as Xenia's XAM_OVERLAPPED
typedef struct _MESSAGEBOX_RESULT { union { DWORD dwButtonPressed; WORD rgwPasscode[4]; }; } MESSAGEBOX_RESULT;

TP_EXTERN_C DWORD XamShowMessageBoxUI(DWORD, LPCWSTR, LPCWSTR, DWORD, LPCWSTR*, DWORD, DWORD, MESSAGEBOX_RESULT*, XOVERLAPPED*);
TP_EXTERN_C DWORD XamShowKeyboardUI(DWORD, DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, XOVERLAPPED*);
TP_EXTERN_C DWORD XamGetOverlappedResult(XOVERLAPPED*, DWORD*, BOOL);
#define XShowMessageBoxUI XamShowMessageBoxUI
#define XShowKeyboardUI XamShowKeyboardUI
#define XGetOverlappedResult XamGetOverlappedResult
#define XHasOverlappedIoCompleted(ov) ((ov)->InternalLow != ERROR_IO_PENDING)

// ---- plugin platform hooks (src/forge/runtime.cpp, src/forge/forge.S) -------

// Calls a console function by address. Console code stores its arguments in
// its caller's frame, which code built for the SysV ABI does not reserve;
// this trampoline does.
TP_EXTERN_C DWORD tp_ccall(PVOID fn, DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6);

// The kernel slot that holds the address of XexExecutableModuleHandle.
TP_EXTERN_C void* __imp_XexExecutableModuleHandle;
#define XexExecutableModuleHandle ((struct _LDR_DATA_TABLE_ENTRY**)__imp_XexExecutableModuleHandle)

// The plugin's DllMain; the runtime's DllMain runs C++ constructors first.
#define DllMain PluginDllMain

#endif
