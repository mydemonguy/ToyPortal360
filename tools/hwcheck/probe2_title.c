/* Probe2.xex: launched from Aurora, outside any game. Loads each ToyPortal360
 * copy from Hdd1:\PeerPressure\Probe\ the way Peer Pressure loads a title DLL
 * and writes T3-<address>-<status>.txt (00000000 = loaded; ToyPortal360's
 * own TP- markers then show its start-up), waits, unloads it, and finally
 * tries LegoLoader.xex (T3-LOADER-<status>.txt). Then returns to the dashboard. */
#include "probe.h"
#include <xbox360/xam.h>

static void wait_ms(LONG ms)
{
	LARGE_INTEGER t;
	t.QuadPart = -(LONGLONG)ms * 10000;
	KeDelayExecutionThread(1, FALSE, &t);
}

static void result(const char* tag, NTSTATUS st)
{
	char name[64];
	char* o = put_str(name, "T3-");
	o = put_str(o, tag);
	*o++ = '-';
	o = put_hex(o, (ULONG)st, 8);
	o = put_str(o, ".txt");
	*o = 0;
	probe_file(name);
}

static NTSTATUS try_load(const char* path, const char* tag, int unload)
{
	PLDR_DATA_TABLE_ENTRY h = NULL;
	NTSTATUS st = XexLoadImage(path, XEX_MODULE_FLAG_TITLE | XEX_MODULE_FLAG_DLL, 0, &h);
	result(tag, st);
	if (st >= 0 && h) {
		wait_ms(3000);
		if (unload)
			XexUnloadImage(h);
	}
	return st;
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	probe_file("T3-STARTED.txt");
	try_load(PROBE_DIR "TP8A00.xex", "8A000000", 1);
	try_load(PROBE_DIR "TP8600.xex", "86000000", 1);
	try_load(PROBE_DIR "TP8F10.xex", "8F100000", 1);
	try_load(PROBE_DIR "LegoLoader.xex", "LOADER", 0);
	wait_ms(5000);
	probe_file("T3-FINISHED.txt");
	XamLoaderLaunchTitle(NULL, 0);
	return 0;
}
