// The Peer Pressure sideload build (TOYPORTAL_SIDELOAD) under qemu-ppc with
// the fake Xbox runtime: Peer Pressure loads the DLL into a game after the
// game is in memory and before its code runs, and unloads it on exit.
#include "xdk_stub/xdk_control.h"
#include "../src/xbox/plugin.h"
#include "../src/core/dimensions_toypad.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string>

extern "C" BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID);
void* fake_real_function(int which);

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct FakeTitle
{
	LDR_DATA_TABLE_ENTRY ldr;
	BYTE header[512];
	DWORD vars[6];
	DWORD* stubs;
	DWORD real[3];
};

static void build_title(FakeTitle* t)
{
	memset(t, 0, sizeof(*t));
	t->stubs = (DWORD*)mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	t->ldr.XexHeaderBase = t->header;
	XEX_IMPORT_DESCRIPTOR* d = (XEX_IMPORT_DESCRIPTOR*)t->header;
	d->NameTableSize = 8;
	d->ModuleCount = 1;
	memcpy(t->header + sizeof(*d), "xam.xex\0", 8);
	XEX_IMPORT_TABLE* table = (XEX_IMPORT_TABLE*)(t->header + sizeof(*d) + 8);
	table->ImportTable.ImportCount = 4;
	table->TableSize = sizeof(XEX_IMPORT_TABLE) + 16;
	DWORD* records = (DWORD*)(table + 1);
	const DWORD ords[2] = {kOrdXamInputNonControllerGetRawEx, kOrdXamInputNonControllerSetRawEx};
	for (int i = 0; i < 2; i++)
	{
		t->real[i] = (DWORD)fake_real_function((int)ords[i]);
		t->vars[i * 2] = t->real[i];
		DWORD* stub = t->stubs + i * 4;
		stub[0] = 0x38600000; stub[1] = 0x38800000; stub[2] = 0x7D6903A6; stub[3] = 0x4E800420;
		records[i * 2] = (DWORD)&t->vars[i * 2];
		records[i * 2 + 1] = (DWORD)stub;
	}
}

typedef DWORD (*GetRawEx_t)(DWORD, BYTE*, DWORD*, WORD*);
typedef DWORD (*SetRawEx_t)(DWORD, BYTE*, DWORD);

static DWORD game_read(FakeTitle* t, BYTE* out)
{
	for (int i = 0; i < 100; i++)
	{
		DWORD len = 32;
		WORD state = 0;
		((GetRawEx_t)t->stubs)(5, out, &len, &state);
		if (len)
			return len;
		usleep(5000);
	}
	return 0;
}

static bool note_contains(const char* s)
{
	std::vector<std::string> v = fake::notifications();
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].find(s) != std::string::npos)
			return true;
	return false;
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	char dir[] = "/tmp/toyportal_sideloadXXXXXX";
	fake::set_root(mkdtemp(dir));

	static FakeTitle game;
	build_title(&game);
	fake::set_title(&game.ldr, 0x4D530A13);

	// Peer Pressure: game in memory, DLL loaded, DllMain runs before the game.
	DllMain(NULL, DLL_PROCESS_ATTACH, NULL);
	CHECK(game.vars[0] != game.real[0] && game.vars[2] != game.real[1]); // hooked inside DllMain
	CHECK(g_title_hooked && g_config.emulate);

	// The game talks to the portal at once, before the thread's disk set-up.
	BYTE pkt[32], reply[32];
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x11; pkt[2] = 0x55; pkt[3] = 0x0F; pkt[4] = 0xB0; pkt[5] = 0x01;
	CHECK(((SetRawEx_t)(game.stubs + 4))(5, pkt, 32) == 0);
	CHECK(game_read(&game, reply) == 32 && reply[2] == 0x55 && memcmp(reply + 5, "(c) LEGO 2014", 13) == 0);

	// Thread set-up finishes: storage, settings, "ready" notification.
	for (int i = 0; i < 100 && !note_contains("ready"); i++)
		usleep(20000);
	CHECK(note_contains("ready"));
	CHECK(g_router.kind() == kToyDimensions); // not reset by the late config load

	// Menu: create Batman, put him on the left pad.
	fake::script_box(2); fake::script_box(0); fake::script_keyboard("batman");
	fake::script_box(2); fake::script_box(0); fake::script_box(0); fake::script_box(-1);
	fake::buttons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
	usleep(250000);
	fake::buttons = 0;
	for (int i = 0; i < 400 && (fake::script_left() || fake::threads_running() > 1); i++)
		usleep(20000);
	CHECK(g_router.dim.count() == 1);
	CHECK(game_read(&game, reply) == 32 && reply[2] == 0x56);

	// The game writes a page, then exits at once: unloading must still save it.
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x0B; pkt[2] = 0x55; pkt[3] = 0x08; pkt[4] = 0xD3; pkt[5] = 0x02; pkt[6] = 1; pkt[7] = 0x11;
	pkt[8] = 0x5A; pkt[9] = 0x5B; pkt[10] = 0x5C; pkt[11] = 0x5D;
	((SetRawEx_t)(game.stubs + 4))(5, pkt, 32);
	DllMain(NULL, DLL_PROCESS_DETACH, NULL);
	CHECK(fake::threads_running() == 0);
	BYTE tag[kDimFigureSize];
	FILE* f = fopen(fake::host_path(g_router.dim.figure(0).path).c_str(), "rb");
	CHECK(f && fread(tag, 1, kDimFigureSize, f) == kDimFigureSize);
	if (f) fclose(f);
	CHECK(tag[0x44] == 0x5A && tag[0x47] == 0x5D);

	// Every thread belonged to the game; none was a system thread.
	CHECK(fake::system_threads == 0 && fake::title_threads >= 2);

	FILE* lf = fopen(fake::host_path("TPHDD:\\ToyPortal\\log.txt").c_str(), "rb");
	char log[8192] = {0};
	if (lf) { fread(log, 1, sizeof(log) - 1, lf); fclose(lf); }
	CHECK(strstr(log, "Peer Pressure sideload") != NULL);
	CHECK(strstr(log, "hooked 2 portal import(s)") != NULL);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
