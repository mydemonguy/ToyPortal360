/* Probe.xex: launched from Aurora. Loads the test DLL built at eight
 * addresses with the same flags as a title DLL (TITLE | DLL) and writes
 * LOAD-<address>-<status>.txt for each (status 00000000 = loaded), then
 * MAP-<start>-<end>.txt for the memory in use. Then returns to the dashboard. */
#include "probe.h"
#include <xbox360/xam.h>

static const ULONG bases[] = { 0x84000000, 0x86000000, 0x88000000, 0x8A000000,
                               0x8C000000, 0x8E000000, 0x8F000000, 0x8FF00000 };

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	probe_file("PROBE-STARTED.txt");
	probe_map("MAP", 16);
	for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
		char path[160], name[64];
		char* o = put_str(path, PROBE_DIR "Dll");
		o = put_hex(o, bases[i] >> 16, 4);
		o = put_str(o, ".xex");
		*o = 0;
		PLDR_DATA_TABLE_ENTRY h = NULL;
		NTSTATUS st = XexLoadImage(path, XEX_MODULE_FLAG_TITLE | XEX_MODULE_FLAG_DLL, 0, &h);
		o = put_str(name, "LOAD-");
		o = put_hex(o, bases[i], 8);
		o = put_str(o, "-");
		o = put_hex(o, (ULONG)st, 8);
		o = put_str(o, ".txt");
		*o = 0;
		probe_file(name);
		if (st >= 0 && h)
			XexUnloadImage(h);
	}
	probe_file("PROBE-FINISHED.txt");
	XamLoaderLaunchTitle(NULL, 0);
	return 0;
}
