// ToyPortal360 - Xbox 360 kernel/XAM declarations that the public XDK
// headers leave out. These are the definitions widely used by RGH/JTAG
// homebrew (DashLaunch plugins); they link against the XDK's xboxkrnl.lib.
#ifndef TOYPORTAL_KERNEL_H
#define TOYPORTAL_KERNEL_H

#include <xtl.h>

typedef struct _TP_ANSI_STRING
{
	USHORT Length;
	USHORT MaximumLength;
	PCHAR Buffer;
} TP_ANSI_STRING;

typedef struct _TP_UNICODE_STRING
{
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
} TP_UNICODE_STRING;

typedef struct _LDR_DATA_TABLE_ENTRY
{
	LIST_ENTRY InLoadOrderLinks;           // 0x00
	LIST_ENTRY InClosureOrderLinks;        // 0x08
	LIST_ENTRY InInitializationOrderLinks; // 0x10
	PVOID NtHeadersBase;                   // 0x18
	PVOID ImageBase;                       // 0x1C
	DWORD SizeOfNtImage;                   // 0x20
	TP_UNICODE_STRING FullDllName;         // 0x24
	TP_UNICODE_STRING BaseDllName;         // 0x2C
	DWORD Flags;                           // 0x34
	DWORD SizeOfFullImage;                 // 0x38
	PVOID EntryPoint;                      // 0x3C
	WORD LoadCount;                        // 0x40
	WORD ModuleIndex;                      // 0x42
	PVOID DllBaseOriginal;                 // 0x44
	DWORD CheckSum;                        // 0x48
	DWORD ModuleLoadFlags;                 // 0x4C
	DWORD TimeDateStamp;                   // 0x50
	PVOID LoadedImports;                   // 0x54
	PVOID XexHeaderBase;                   // 0x58
	TP_ANSI_STRING LoadFileName;           // 0x5C
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _XEX_IMPORT_DESCRIPTOR
{
	DWORD Size;
	DWORD NameTableSize;
	DWORD ModuleCount;
} XEX_IMPORT_DESCRIPTOR, *PXEX_IMPORT_DESCRIPTOR;

typedef struct _HV_IMAGE_IMPORT_TABLE
{
	BYTE NextImportDigest[0x14];
	DWORD ModuleNumber;
	DWORD Version[2];
	BYTE Unused;
	BYTE ModuleIndex;
	WORD ImportCount;
} HV_IMAGE_IMPORT_TABLE;

typedef struct _XEX_IMPORT_TABLE
{
	DWORD TableSize;
	HV_IMAGE_IMPORT_TABLE ImportTable;
} XEX_IMPORT_TABLE, *PXEX_IMPORT_TABLE;

#define XEX_HEADER_IMPORTS 0x000103FF

extern "C"
{
	DWORD XexGetModuleHandle(PSZ moduleName, PHANDLE hand);
	DWORD XexGetProcedureAddress(HANDLE hand, DWORD dwOrdinal, PVOID Address);
	PVOID RtlImageXexHeaderField(PVOID XexHeaderBase, DWORD ImageField);
	LONG ObCreateSymbolicLink(TP_ANSI_STRING* SymbolicLinkName, TP_ANSI_STRING* DeviceName);
#ifndef TOYPORTAL_FORGE // the Xenon Forge build gets these from src/forge
	DWORD ExCreateThread(PHANDLE pHandle, DWORD dwStackSize, LPDWORD lpThreadId, PVOID apiThreadStartup,
		LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlagsMod);
	VOID XapiThreadStartup(VOID (__cdecl* StartRoutine)(VOID*), PVOID StartContext);
	extern PLDR_DATA_TABLE_ENTRY* XexExecutableModuleHandle;
#endif
	// Starts a system thread (plugin.cpp for the SDK build, src/forge for Xenon Forge).
	// system_thread: survives title changes (DashLaunch); false for a thread that
	// belongs to the running game (Peer Pressure sideload).
	bool tp_start_thread(LPTHREAD_START_ROUTINE fn, LPVOID arg, HANDLE* out_handle, bool system_thread);
}

// XAM ordinals hooked by ToyPortal360 (from Xenia's xam_table.inc).
enum
{
	kOrdXamInputNonControllerGetRaw = 0x4A1,
	kOrdXamInputNonControllerSetRaw = 0x4A2,
	kOrdXamInputNonControllerGetRawEx = 0x52A,
	kOrdXamInputNonControllerSetRawEx = 0x52B,
	kOrdXNotifyQueueUI = 0x290, // 656
	kOrdXamGetCurrentTitleId = 0x1CF,
	kOrdXexGetProcedureAddress = 0x197 // xboxkrnl.exe
};

#endif
