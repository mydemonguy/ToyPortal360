/* LegoProbe.xex: sideloaded into LEGO Dimensions by Peer Pressure. Writes
 * LEGO-PROBE-RAN.txt and LEGO-<start>-<end>.txt for the memory in use in the
 * game's process, so ToyPortal360 can be placed where nothing else is. */
#include "probe.h"
BOOL DllMain(HANDLE Module, DWORD Reason, PVOID Reserved)
{
	(void)Module; (void)Reserved;
	if (Reason == DLL_PROCESS_ATTACH) {
		probe_file("LEGO-PROBE-RAN.txt");
		probe_map("LEGO", 16);
	}
	return TRUE;
}
