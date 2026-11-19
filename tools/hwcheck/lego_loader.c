/* LegoLoader.xex: sideloaded into LEGO Dimensions at 0x8F000000 (where
 * LegoProbe loaded). It writes L2-RAN.txt, starts a thread (L2-THREAD.txt),
 * then for each ToyPortal360 copy in Hdd1:\PeerPressure\Probe\ records how
 * the memory at its address is used (Q-<address>-<status>-<state>-<size>.txt)
 * and tries to load it (L2-<address>-<status>.txt, 00000000 = loaded). It
 * stops at the first copy that loads and leaves it running. */
#include "probe.h"

static const struct { const char* file; ULONG base; } copies[] = {
	{ PROBE_DIR "TP8A00.xex", 0x8A000000 },
	{ PROBE_DIR "TP8600.xex", 0x86000000 },
	{ PROBE_DIR "TP8F10.xex", 0x8F100000 },
};

static void named(const char* a, ULONG v1, ULONG v2, ULONG v3, ULONG v4, int n)
{
	char name[96];
	char* o = put_str(name, a);
	ULONG v[4] = { v1, v2, v3, v4 };
	for (int i = 0; i < n; i++) { *o++ = '-'; o = put_hex(o, v[i], 8); }
	o = put_str(o, ".txt");
	*o = 0;
	probe_file(name);
}

static void loader_thread(PVOID context)
{
	(void)context;
	probe_file("L2-THREAD.txt");
	for (unsigned i = 0; i < sizeof(copies) / sizeof(copies[0]); i++) {
		MEMORY_BASIC_INFORMATION mbi;
		NTSTATUS q = NtQueryVirtualMemory((PVOID)copies[i].base, &mbi, MM_CURRENT_PROCESS_REGION);
		named("Q", copies[i].base, (ULONG)q, q >= 0 ? mbi.State : 0, q >= 0 ? (ULONG)mbi.RegionSize : 0, 4);
		PLDR_DATA_TABLE_ENTRY h = NULL;
		NTSTATUS st = XexLoadImage(copies[i].file, XEX_MODULE_FLAG_TITLE | XEX_MODULE_FLAG_DLL, 0, &h);
		named("L2", copies[i].base, (ULONG)st, 0, 0, 2);
		if (st >= 0)
			break;
	}
	probe_file("L2-DONE.txt");
	ExTerminateThread(0);
}

BOOL DllMain(HANDLE Module, DWORD Reason, PVOID Reserved)
{
	(void)Module; (void)Reserved;
	if (Reason == DLL_PROCESS_ATTACH) {
		probe_file("L2-RAN.txt");
		HANDLE h = NULL;
		ULONG id = 0;
		NTSTATUS st = ExCreateThread(&h, 0, &id, NULL, (PKSTART_ROUTINE)loader_thread, NULL, 0);
		if (st < 0)
			named("L2-NOTHREAD", (ULONG)st, 0, 0, 0, 1);
		else if (h)
			NtClose(h);
	}
	return TRUE;
}
