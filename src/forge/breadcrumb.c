/* Start-up markers for the Peer Pressure build (TOYPORTAL_BREADCRUMBS): each
 * step writes an empty-ish file Hdd1:\PeerPressure\Probe\TP-<tag>-<value>.txt
 * with kernel calls only, so a console run shows how far the plugin got even
 * when nothing appears on screen. */
#include <xbox360/xboxkrnl.h>

static char* put_str(char* o, const char* s) { while (*s) *o++ = *s++; return o; }

void tp_crumb(const char* tag, unsigned value)
{
	char path[160];
	char* o = put_str(path, "\\Device\\Harddisk0\\Partition1\\PeerPressure\\Probe\\TP-");
	o = put_str(o, tag);
	*o++ = '-';
	for (int i = 7; i >= 0; i--) {
		unsigned d = (value >> (i * 4)) & 0xF;
		*o++ = (char)(d < 10 ? '0' + d : 'A' + d - 10);
	}
	o = put_str(o, ".txt");
	*o = 0;
	USHORT n = (USHORT)(o - path);
	OBJECT_STRING str = { n, (USHORT)(n + 1), path };
	OBJECT_ATTRIBUTES attr = { NULL, &str, OBJ_CASE_INSENSITIVE };
	IO_STATUS_BLOCK io;
	HANDLE file = NULL;
	if (NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL,
		0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE) < 0)
		return;
	static const char body[] = "ToyPortal360 start-up marker\r\n";
	NtWriteFile(file, NULL, NULL, NULL, &io, (PVOID)body, sizeof(body) - 1, NULL);
	NtClose(file);
}

/* Writes Hdd1:\PeerPressure\Probe\TP-IMPORTS.bin, everything needed to see
 * how the game imports system functions:
 *   "TPIM", the four XAM toy-pad addresses we look for (4 x 4 bytes),
 *   the import descriptor as the loader left it (its first word is its size),
 *   then for every import record: its address and the 16 bytes there,
 *   then "XAM!" and the address of each XAM export, ordinals 0 to 0xBFF. */
static HANDLE dump_open(void)
{
	static char path[] = "\\Device\\Harddisk0\\Partition1\\PeerPressure\\Probe\\TP-IMPORTS.bin";
	USHORT n = (USHORT)(sizeof(path) - 1);
	OBJECT_STRING str = { n, (USHORT)(n + 1), path };
	OBJECT_ATTRIBUTES attr = { NULL, &str, OBJ_CASE_INSENSITIVE };
	IO_STATUS_BLOCK io;
	HANDLE file = NULL;
	if (NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL,
		0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE) < 0)
		return NULL;
	return file;
}

static void dump_write(HANDLE f, const void* p, ULONG len)
{
	IO_STATUS_BLOCK io;
	NtWriteFile(f, NULL, NULL, NULL, &io, (PVOID)p, len, NULL);
}

void tp_dump_imports(void* exe, const unsigned* wanted)
{
	HANDLE f = dump_open();
	if (!f)
		return;
	dump_write(f, "TPIM", 4);
	dump_write(f, wanted, 16);
	PLDR_DATA_TABLE_ENTRY ldr = (PLDR_DATA_TABLE_ENTRY)exe;
	const ULONG* desc = (ldr && ldr->XexHeaderBase)
		? (const ULONG*)RtlImageXexHeaderField(ldr->XexHeaderBase, 0x000103FF) : NULL;
	if (desc && MmIsAddressValid((PVOID)desc)) {
		const ULONG size = desc[0];
		dump_write(f, desc, size);
		const BYTE* t = (const BYTE*)desc + 12 + desc[1];
		for (ULONG m = 0; m < desc[2]; m++) {
			const ULONG table_size = *(const ULONG*)t;
			const USHORT count = *(const USHORT*)(t + 0x26);
			const ULONG* records = (const ULONG*)(t + 0x28);
			for (USHORT i = 0; i < count; i++) {
				ULONG rec[5] = { records[i], 0, 0, 0, 0 };
				const ULONG* at = (const ULONG*)records[i];
				if (MmIsAddressValid((PVOID)at) && MmIsAddressValid((PVOID)(at + 3)))
					for (int k = 0; k < 4; k++) rec[1 + k] = at[k];
				dump_write(f, rec, sizeof(rec));
			}
			t += table_size;
		}
	} else {
		ULONG zero = 0;
		dump_write(f, &zero, 4);
	}
	dump_write(f, "XAM!", 4);
	PLDR_DATA_TABLE_ENTRY xam = NULL;
	static ULONG exports[0xC00];
	if (XexGetModuleHandle("xam.xex", &xam) >= 0 && xam)
		for (ULONG o = 0; o < 0xC00; o++) {
			PVOID a = NULL;
			exports[o] = XexGetProcedureAddress(xam, o, &a) >= 0 ? (ULONG)a : 0;
		}
	dump_write(f, exports, sizeof(exports));
	/* "KRN!" and the kernel's exports, ordinals 0 to 0x3FF */
	dump_write(f, "KRN!", 4);
	PLDR_DATA_TABLE_ENTRY krnl = NULL;
	for (ULONG o = 0; o < 0x400; o++)
		exports[o] = 0;
	if (XexGetModuleHandle("xboxkrnl.exe", &krnl) >= 0 && krnl)
		for (ULONG o = 0; o < 0x400; o++) {
			PVOID a = NULL;
			exports[o] = XexGetProcedureAddress(krnl, o, &a) >= 0 ? (ULONG)a : 0;
		}
	dump_write(f, exports, 0x400 * 4);
	NtClose(f);
}
