/* DllCheck.xex: a title DLL that does nothing but leave a marker file when it
 * loads. Peer Pressure sideloads it into LEGO Dimensions with the test
 * 5752084b.ini. If DLL-TEST-RAN.txt appears, Peer Pressure loaded a DLL from
 * this toolchain and ran its DllMain. */
#include "marker.h"

BOOL DllMain(HANDLE Module, DWORD Reason, PVOID Reserved)
{
	(void)Module; (void)Reserved;
	if (Reason == DLL_PROCESS_ATTACH)
		write_marker(MARKER_DIR "DLL-TEST-RAN.txt", "Peer Pressure loaded the test DLL and ran DllMain.\r\n");
	return TRUE;
}
