/* Shared helpers for the address probes: results are written as file names
 * in Hdd1:\PeerPressure\Probe\ so they can be read in any file manager. */
#include <xbox360/xboxkrnl.h>

#define PROBE_DIR "\\Device\\Harddisk0\\Partition1\\PeerPressure\\Probe\\"

static char* put_str(char* o, const char* s) { while (*s) *o++ = *s++; return o; }
static char* put_hex(char* o, ULONG v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) {
		ULONG d = (v >> (i * 4)) & 0xF;
		*o++ = (char)(d < 10 ? '0' + d : 'A' + d - 10);
	}
	return o;
}

/* Creates PROBE_DIR<name> (name is a file name) with a one-line body. */
static void probe_file(const char* name)
{
	char path[200];
	char* o = put_str(path, PROBE_DIR);
	o = put_str(o, name);
	*o = 0;
	USHORT n = (USHORT)(o - path);
	OBJECT_STRING str = { n, (USHORT)(n + 1), path };
	OBJECT_ATTRIBUTES attr = { NULL, &str, OBJ_CASE_INSENSITIVE };
	IO_STATUS_BLOCK io;
	HANDLE file = NULL;
	if (NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL,
		0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE) < 0)
		return;
	static const char body[] = "ToyPortal360 probe result\r\n";
	NtWriteFile(file, NULL, NULL, NULL, &io, (PVOID)body, sizeof(body) - 1, NULL);
	NtClose(file);
}

/* Writes <prefix>-<start>-<end>.txt for each run of mapped 64 KB pages in
 * 0x80000000-0x8FFFFFFF (at most max_files files). */
static void probe_map(const char* prefix, int max_files)
{
	ULONG start = 0;
	int in_run = 0, files = 0;
	for (ULONG a = 0x80000000u; ; a += 0x10000u) {
		int used = a < 0x90000000u && MmQueryAddressProtect((PVOID)a) != 0;
		if (used && !in_run) { start = a; in_run = 1; }
		if (!used && in_run) {
			char name[64];
			char* o = put_str(name, prefix);
			o = put_str(o, "-");
			o = put_hex(o, start, 8);
			o = put_str(o, "-");
			o = put_hex(o, a - 1, 8);
			o = put_str(o, ".txt");
			*o = 0;
			probe_file(name);
			in_run = 0;
			if (++files >= max_files) return;
		}
		if (a >= 0x90000000u) break;
	}
}
