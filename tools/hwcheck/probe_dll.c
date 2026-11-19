/* A do-nothing title DLL, built once per test address. */
#include <xbox360/xboxkrnl.h>
BOOL DllMain(HANDLE Module, DWORD Reason, PVOID Reserved)
{
	(void)Module; (void)Reason; (void)Reserved;
	return TRUE;
}
