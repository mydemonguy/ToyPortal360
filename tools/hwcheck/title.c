/* ForgeCheck.xex: a standalone program built with the same toolchain as
 * ToyPortal360. Launch it from Aurora: if FORGE-PROGRAM-RAN.txt appears in
 * Hdd1:\PeerPressure\SideLoading\, the console runs files from this toolchain.
 * It then returns to the dashboard. */
#include "marker.h"
#include <xbox360/xam.h>

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	write_marker(MARKER_DIR "FORGE-PROGRAM-RAN.txt", "The console ran a Xenon Forge program.\r\n");
	XamLoaderLaunchTitle(NULL, 0);
	return 0;
}
