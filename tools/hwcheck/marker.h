/* Writes a small text file to Hdd1:\PeerPressure\SideLoading\ with kernel
 * calls only, so a test leaves visible proof that its code ran. */
#include <xbox360/xboxkrnl.h>

static int tp_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }

#define MARKER_DIR "\\Device\\Harddisk0\\Partition1\\PeerPressure\\SideLoading\\"

/* path: a full device path, e.g. MARKER_DIR "X.txt". */
static NTSTATUS write_marker(const char* path, const char* text)
{
	int n = tp_strlen(path);
	OBJECT_STRING str = { (USHORT)n, (USHORT)(n + 1), (PCHAR)path };
	OBJECT_ATTRIBUTES attr = { NULL, &str, OBJ_CASE_INSENSITIVE };
	IO_STATUS_BLOCK io;
	HANDLE file = NULL;
	NTSTATUS st = NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL,
		FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (st < 0)
		return st;
	st = NtWriteFile(file, NULL, NULL, NULL, &io, (PVOID)text, (ULONG)tp_strlen(text), NULL);
	NtClose(file);
	return st;
}
