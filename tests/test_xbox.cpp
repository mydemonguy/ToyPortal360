// Runs the plugin's Xbox-only code (src/xbox) end to end under qemu-ppc,
// against the fake runtime in tests/xdk_stub. Covers: import patching of a
// fake title, executing the generated PowerPC jump stubs, a game session
// through the hooks, scripted menu flows, saving to disk, title switches,
// pass-through of unknown protocols and settings.
#include "xdk_stub/xdk_control.h"
#include "../src/xbox/plugin.h"
#include "../src/core/figure_db.h"
#include "../src/core/infinity_base.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string>

extern "C" BOOL APIENTRY DllMain(HANDLE module, DWORD reason, LPVOID);
void* fake_real_function(int which);

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- a fake title with an import table --------------------------------------

struct FakeTitle
{
	LDR_DATA_TABLE_ENTRY ldr;
	BYTE header[512];
	DWORD vars[10];
	DWORD* stubs; // 5 stubs of 4 instructions, in executable memory
	DWORD real[5];
};

static DWORD* exec_alloc(size_t bytes)
{
	void* p = mmap(NULL, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? NULL : (DWORD*)p;
}

// ordinals: which XAM functions this title imports (0 = none).
static DWORD stub_target(const DWORD* w)
{
	if ((w[0] >> 16) != 0x3D60 || w[2] != 0x7D6903A6 || w[3] != 0x4E800420)
		return 0;
	const DWORD hi = (w[0] & 0xFFFF) << 16;
	if ((w[1] >> 16) == 0x616B)
		return hi | (w[1] & 0xFFFF);
	if ((w[1] >> 16) == 0x396B)
		return hi + (DWORD)(int)(short)(w[1] & 0xFFFF);
	return 0;
}

// stubs_only: the title has no import pointers, only call stubs that the
// loader filled with lis r11 / ori r11 / mtctr / bctr to the function.
static void build_title(FakeTitle* t, const DWORD* ordinals, int count, DWORD stamp, bool stubs_only = false)
{
	memset(t, 0, sizeof(*t));
	t->stubs = exec_alloc(4096);
	t->ldr.XexHeaderBase = t->header;
	t->ldr.TimeDateStamp = stamp;

	XEX_IMPORT_DESCRIPTOR* d = (XEX_IMPORT_DESCRIPTOR*)t->header;
	d->NameTableSize = 8;
	d->ModuleCount = count ? 1 : 0;
	memcpy(t->header + sizeof(*d), "xam.xex\0", 8);
	XEX_IMPORT_TABLE* table = (XEX_IMPORT_TABLE*)(t->header + sizeof(*d) + 8);
	table->ImportTable.ImportCount = (WORD)((count + 1) * 2);
	table->TableSize = sizeof(XEX_IMPORT_TABLE) + table->ImportTable.ImportCount * 4;
	DWORD* records = (DWORD*)(table + 1);
	for (int i = 0; i <= count; i++)
	{
		// The last import is an unrelated function that must not be touched.
		t->real[i] = i < count ? (DWORD)fake_real_function((int)ordinals[i]) : 0xDEAD0000;
		t->vars[i * 2] = t->real[i];
		DWORD* stub = t->stubs + i * 4;
		stub[0] = 0x38600000; // li r3, 0   (as left by the linker)
		stub[1] = 0x38800000; // li r4, 0
		stub[2] = 0x7D6903A6; // mtctr r11
		stub[3] = 0x4E800420; // bctr
		records[i * 2] = (DWORD)&t->vars[i * 2];
		records[i * 2 + 1] = (DWORD)stub;
		if (stubs_only)
		{
			stub[0] = 0x3D600000 | (t->real[i] >> 16);
			stub[1] = 0x616B0000 | (t->real[i] & 0xFFFF);
			records[i] = (DWORD)stub;
		}
	}
	if (stubs_only)
		table->ImportTable.ImportCount = (WORD)(count + 1);
}

typedef DWORD (*GetRaw_t)(WORD*, DWORD*, BYTE*);
typedef DWORD (*SetRaw_t)(DWORD, BYTE*);
typedef DWORD (*GetRawEx_t)(DWORD, BYTE*, DWORD*, WORD*);
typedef DWORD (*SetRawEx_t)(DWORD, BYTE*, DWORD);

static bool wait_for(bool (*cond)(void*), void* ctx, int ms)
{
	for (int i = 0; i < ms / 20; i++)
	{
		if (cond(ctx))
			return true;
		usleep(20000);
	}
	return cond(ctx);
}
static bool patched(void* p) { FakeTitle* t = (FakeTitle*)p; return t->vars[0] != t->real[0]; }
static bool menu_done(void*) { return fake::script_left() == 0 && fake::threads_running() <= 1; }
static bool hooked(void*) { return g_title_hooked; }
static bool unhooked(void*) { return !g_title_hooked; }

static void open_menu()
{
	fake::buttons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
	usleep(250000);
	fake::buttons = 0;
	CHECK(wait_for(menu_done, NULL, 8000));
	usleep(100000);
}

static bool file_size(const char* xbox_path, long* size)
{
	struct stat st;
	if (stat(fake::host_path(xbox_path).c_str(), &st) != 0)
		return false;
	*size = (long)st.st_size;
	return true;
}

static bool read_host(const char* xbox_path, BYTE* out, size_t n)
{
	FILE* f = fopen(fake::host_path(xbox_path).c_str(), "rb");
	if (!f)
		return false;
	const size_t got = fread(out, 1, n, f);
	fclose(f);
	return got == n;
}

static std::string read_text(const char* xbox_path)
{
	std::string s;
	FILE* f = fopen(fake::host_path(xbox_path).c_str(), "rb");
	if (!f)
		return s;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		s.append(buf, n);
	fclose(f);
	return s;
}

static bool any_shown_contains(const char* needle)
{
	std::vector<fake::Shown> v = fake::shown();
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].text.find(needle) != std::string::npos || v[i].title.find(needle) != std::string::npos)
			return true;
	return false;
}

static bool any_note_contains(const char* needle)
{
	std::vector<std::string> v = fake::notifications();
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].find(needle) != std::string::npos)
			return true;
	return false;
}

// Reads from the game's side until a packet arrives (or gives up).
static DWORD game_read_ex(FakeTitle* t, int stub, BYTE* out)
{
	for (int i = 0; i < 100; i++)
	{
		DWORD len = 32;
		WORD state = 0;
		memset(out, 0, 32);
		const DWORD r = ((GetRawEx_t)(t->stubs + stub * 4))(5, out, &len, &state);
		if (r != 0)
			return 0;
		if (len)
			return len;
		usleep(5000);
	}
	return 0;
}

// ---- tests ------------------------------------------------------------------

static void test_jump_encoding()
{
	// Pick targets inside one 192 KB executable block so they cover both
	// halves of a 64 KB page: low half >= 0x8000 needs the "lis +1" fix-up.
	BYTE* block = (BYTE*)exec_alloc(192 * 1024);
	CHECK(block != NULL);
	if (!block)
		return;
	const DWORD base = ((DWORD)block + 0xFFFF) & ~0xFFFFu; // next 64 KB boundary
	const DWORD targets[4] = {base + 0x8000, base + 0x7FF0, base + 0xA5A0, base + 0x0010};
	for (int i = 0; i < 4; i++)
	{
		DWORD* fn = (DWORD*)targets[i];
		fn[0] = 0x38600000 | (40 + i); // li r3, 40+i
		fn[1] = 0x4E800020;            // blr
	}
	DWORD* stub = exec_alloc(4096);
	for (int i = 0; i < 4; i++)
	{
		ppc_write_jump(stub + i * 4, targets[i]);
		const int r = ((int (*)())(stub + i * 4))();
		if (r != 40 + i)
			printf("  jump to %08lX returned %d: %08lX %08lX\n", targets[i], r, stub[i * 4], stub[i * 4 + 1]);
		CHECK(r == 40 + i);
	}
}

static void test_dimensions_title(FakeTitle* t)
{
	// GetRawEx at stub 0, SetRawEx at stub 1.
	CHECK(wait_for(patched, t, 8000));
	CHECK(wait_for(hooked, NULL, 2000));
	CHECK(t->vars[0] != t->real[0] && t->vars[2] != t->real[1]);
	CHECK(t->vars[4] == 0xDEAD0000); // unrelated import untouched
	CHECK(t->stubs[0] >> 16 == 0x3D60 && t->stubs[2] == 0x7D6903A6 && t->stubs[3] == 0x4E800420);
	for (int i = 0; i < 100 && !any_note_contains("ready"); i++) // shown just after hooking
		Sleep(10);
	CHECK(any_note_contains("ready"));

	BYTE pkt[32], reply[32];
	// Wake, wrapped the way the 360 game sends it.
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x11; pkt[2] = 0x55; pkt[3] = 0x0F; pkt[4] = 0xB0; pkt[5] = 0x01;
	CHECK(((SetRawEx_t)(t->stubs + 4))(5, pkt, 32) == 0);
	CHECK(game_read_ex(t, 0, reply) == 32);
	CHECK(reply[0] == 0x0B && reply[1] == 0x11 && reply[2] == 0x55 && memcmp(reply + 5, "(c) LEGO 2014", 13) == 0);
	CHECK(g_router.kind() == kToyDimensions);
	CHECK(fake::real_set_calls == 0 && fake::real_get_calls == 0);

	// Menu: More > Create > "gandalf" > Create > Place it > Left pad, then B.
	fake::script_box(2);
	fake::script_box(0);
	fake::script_keyboard("gandalf");
	fake::script_box(2);
	fake::script_box(0);
	fake::script_box(0);
	fake::script_box(-1);
	open_menu();

	long size = 0;
	CHECK(file_size("TPHDD:\\ToyPortal\\Dimensions\\Gandalf.bin", &size) && size == kDimFigureSize);
	BYTE tag[kDimFigureSize];
	CHECK(read_host("TPHDD:\\ToyPortal\\Dimensions\\Gandalf.bin", tag, kDimFigureSize));
	CHECK(DimensionsToypad::figure_id_of(tag) == 2);
	CHECK(any_note_contains("Placed Gandalf.bin"));
	CHECK(any_shown_contains("Left: Gandalf"));

	// The game hears about it: wrapped 0x56 event for the left pad, index 1.
	CHECK(game_read_ex(t, 0, reply) == 32);
	CHECK(reply[0] == 0x0B && reply[1] == 14 && reply[2] == 0x56 && reply[4] == kPadLeft && reply[6] == 1 && reply[7] == 0);

	// The game writes page 0x10; the plugin saves it to disk shortly after.
	memset(pkt, 0, 32);
	pkt[0] = 0x0B; pkt[1] = 0x0B; pkt[2] = 0x55; pkt[3] = 0x08; pkt[4] = 0xD3; pkt[5] = 0x02; pkt[6] = 1; pkt[7] = 0x10;
	pkt[8] = 0xAB; pkt[9] = 0xCD; pkt[10] = 0xEF; pkt[11] = 0x01;
	((SetRawEx_t)(t->stubs + 4))(5, pkt, 32);
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[2] == 0x55 && reply[3] == 0x02);
	usleep(1200000);
	CHECK(read_host("TPHDD:\\ToyPortal\\Dimensions\\Gandalf.bin", tag, kDimFigureSize));
	CHECK(tag[0x40] == 0xAB && tag[0x43] == 0x01);

	// Placing the same file twice is refused.
	fake::script_box(0);             // Place figure
	fake::script_box(1);             // Centre pad
	fake::script_keyboard("gand");   // filter
	fake::script_box(2);             // Place
	fake::script_box(0);             // OK on the warning
	fake::script_box(-1);
	open_menu();
	CHECK(any_shown_contains("already on the toy pad"));
	CHECK(any_shown_contains("Put it on the toy pad now?"));

	// Move to the centre pad: lifted from left, set down on centre (index 2).
	fake::script_box(2);  // More
	fake::script_box(1);  // Move figure
	fake::script_box(2);  // Move (only one figure)
	fake::script_box(1);  // Centre
	fake::script_box(-1);
	open_menu();
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[2] == 0x56 && reply[4] == kPadLeft && reply[7] == 1);
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[2] == 0x56 && reply[4] == kPadCenter && reply[6] == 2 && reply[7] == 0);
	CHECK(any_shown_contains("Centre: Gandalf"));

	// Remove it.
	fake::script_box(1);  // Remove figure
	fake::script_box(2);  // Remove
	fake::script_box(-1);
	open_menu();
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[2] == 0x56 && reply[7] == 1);
	CHECK(g_router.dim.count() == 0);
	CHECK(any_note_contains("Removed Gandalf"));

	// Unknown filter shows the "no files" help instead of an empty browser.
	fake::script_box(0);
	fake::script_box(0);
	fake::script_keyboard("zzzz-no-such-figure");
	fake::script_box(0);
	fake::script_box(-1);
	open_menu();
	CHECK(any_shown_contains("No figure files found in\nHdd:\\ToyPortal\\Dimensions"));
}

static void test_skylanders_title(FakeTitle* t)
{
	// Non-Ex functions: GetRaw at stub 0, SetRaw at stub 1.
	CHECK(wait_for(patched, t, 4000));
	CHECK(wait_for(hooked, NULL, 2000));
	CHECK(g_router.kind() == kToyUnknown && g_router.dim.count() == 0);

	BYTE pkt[32], reply[32];
	memset(pkt, 0, 32);
	pkt[0] = 'R';
	CHECK(((SetRaw_t)(t->stubs + 4))(32, pkt) == 0);
	pkt[0] = 'A'; pkt[1] = 0x01;
	((SetRaw_t)(t->stubs + 4))(32, pkt);
	CHECK(g_router.kind() == kToySkylanders);

	DWORD len = 32;
	WORD state = 0;
	CHECK(((GetRaw_t)t->stubs)(&state, &len, reply) == 0 && len == 32 && reply[0] == 'R' && state == 1);
	len = 32;
	((GetRaw_t)t->stubs)(&state, &len, reply);
	CHECK(reply[0] == 'A' && reply[2] == 0xFF);

	// Create and place the first "spyro" match.
	fake::script_box(2);
	fake::script_box(0);
	fake::script_keyboard("spyro");
	fake::script_box(2);
	fake::script_box(0);
	fake::script_box(-1);
	open_menu();
	CHECK(g_router.sky.count() == 1);
	const SkyFigure& f = g_router.sky.figure(0);
	CHECK(skylander_crc16(0xFFFF, f.data, 0x1E) == rd_le16(f.data + 0x1E));
	long size = 0;
	CHECK(file_size(f.path, &size) && size == kSkyFigureSize);
	const char* name = sky_name_for(g_router.sky.figure_id(0), g_router.sky.figure_variant(0));
	CHECK(name && strstr(name, "Spyro") != NULL);
	CHECK(any_shown_contains("On the portal:"));

	// Status packets report the arrival.
	bool arrived = false;
	for (int i = 0; i < 50 && !arrived; i++)
	{
		len = 32;
		((GetRaw_t)t->stubs)(&state, &len, reply);
		arrived = len && reply[0] == 'S' && (rd_le16(reply + 1) & 3) == 1;
		usleep(25000);
	}
	CHECK(arrived);

	// A block write lands in the file.
	memset(pkt, 0, 32);
	pkt[0] = 'W'; pkt[1] = 0x10; pkt[2] = 0x09;
	for (int i = 0; i < 16; i++) pkt[3 + i] = (BYTE)(0x30 + i);
	((SetRaw_t)(t->stubs + 4))(32, pkt);
	usleep(1200000);
	BYTE sky[kSkyFigureSize];
	char path[260];
	strcpy(path, g_router.sky.figure(0).path);
	CHECK(read_host(path, sky, kSkyFigureSize) && sky[0x90] == 0x30 && sky[0x9F] == 0x3F);
}

static void test_infinity_title(FakeTitle* t)
{
	CHECK(wait_for(patched, t, 4000));
	CHECK(wait_for(hooked, NULL, 2000));

	BYTE pkt[32], reply[32];
	memset(pkt, 0, 32);
	pkt[0] = 0xFF; pkt[1] = 0x03; pkt[2] = 0x80; pkt[3] = 0x01; // activate
	((SetRawEx_t)(t->stubs + 4))(5, pkt, 32);
	CHECK(g_router.kind() == kToyInfinity);
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[0] == 0xAA && reply[1] == 0x15);

	// Sulley on player 2.
	fake::script_box(2); fake::script_box(0); fake::script_keyboard("sulley");
	fake::script_box(2); fake::script_box(0); fake::script_box(1); fake::script_box(-1);
	open_menu();
	long size = 0;
	CHECK(file_size("TPHDD:\\ToyPortal\\Infinity\\Sulley.bin", &size) && size == kInfFigureSize);
	BYTE fig[kInfFigureSize];
	CHECK(read_host("TPHDD:\\ToyPortal\\Infinity\\Sulley.bin", fig, kInfFigureSize));
	CHECK(InfinityBase::figure_number_of(fig) == 0x0F4242);
	CHECK(g_router.inf.occupied(6));
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[0] == 0xAB && reply[2] == 3 && reply[4] == 0 && reply[5] == 0);

	// A play set goes straight to the hexagon spot (no side question).
	fake::script_box(2); fake::script_box(0); fake::script_keyboard("cars play set");
	fake::script_box(2); fake::script_box(0); fake::script_box(-1);
	open_menu();
	CHECK(g_router.inf.occupied(0));
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[0] == 0xAB && reply[2] == 1);

	// An ability disc on player 2's side.
	fake::script_box(2); fake::script_box(0); fake::script_keyboard("bolt's super");
	fake::script_box(2); fake::script_box(0); fake::script_box(1); fake::script_box(-1);
	open_menu();
	CHECK(g_router.inf.occupied(7));
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[0] == 0xAB && reply[2] == 3);
	CHECK(any_shown_contains("Player 2: Sulley, Bolt's Super Strength"));
	CHECK(any_shown_contains("Play set & discs: Cars Play Set"));
	CHECK(any_shown_contains("Sulley (1.0)"));

	// Present-figures query lists all three.
	memset(pkt, 0, 32);
	pkt[0] = 0xFF; pkt[1] = 0x02; pkt[2] = 0xA1; pkt[3] = 0x02;
	((SetRawEx_t)(t->stubs + 4))(5, pkt, 32);
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[1] == 7);

	// The game writes Sulley's block 1 (file block 4); it reaches the file.
	memset(pkt, 0, 32);
	pkt[0] = 0xFF; pkt[1] = 0x14; pkt[2] = 0xA3; pkt[3] = 0x03; pkt[4] = 0; pkt[5] = 1;
	for (int i = 0; i < 16; i++) pkt[7 + i] = (BYTE)(0x70 + i);
	((SetRawEx_t)(t->stubs + 4))(5, pkt, 32);
	CHECK(game_read_ex(t, 0, reply) == 32 && reply[1] == 0x02);
	usleep(1200000);
	CHECK(read_host("TPHDD:\\ToyPortal\\Infinity\\Sulley.bin", fig, kInfFigureSize) && fig[64] == 0x70 && fig[79] == 0x7F);

	// Player 2's character spot is taken now: placing another is refused.
	fake::script_box(2); fake::script_box(0); fake::script_keyboard("mr. incredible");
	fake::script_box(2); fake::script_box(0); fake::script_box(1); fake::script_box(0); fake::script_box(-1);
	open_menu();
	CHECK(any_shown_contains("That spot on the base is taken"));

	// Remove Sulley.
	fake::script_box(1); fake::script_box(1); fake::script_box(2); fake::script_box(-1);
	open_menu();
	CHECK(!g_router.inf.occupied(6) && g_router.inf.occupied(0) && g_router.inf.occupied(7));
	CHECK(any_note_contains("Removed"));
}

static void test_unknown_protocol(FakeTitle* t)
{
	CHECK(wait_for(patched, t, 4000));
	CHECK(wait_for(hooked, NULL, 2000));
	const int sets = fake::real_set_calls, gets = fake::real_get_calls;
	BYTE pkt[32], reply[32];
	memset(pkt, 0, 32);
	pkt[0] = 0x42; pkt[1] = 0x11; pkt[2] = 0x80; // nothing we emulate
	((SetRawEx_t)(t->stubs + 4))(5, pkt, 32);
	CHECK(fake::real_set_calls == sets + 1);
	DWORD len = 32;
	WORD state = 0;
	((GetRawEx_t)t->stubs)(5, reply, &len, &state);
	CHECK(fake::real_get_calls == gets + 1);
	CHECK(g_title_unsupported);
}

static void test_no_toy_title(FakeTitle* t)
{
	(void)t;
	CHECK(wait_for(unhooked, NULL, 4000));
	fake::buttons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_BACK;
	usleep(250000);
	fake::buttons = 0;
	usleep(100000);
	CHECK(any_note_contains("no toy game is running"));
}

static bool stubs_patched(void* p)
{
	FakeTitle* t = (FakeTitle*)p;
	return stub_target(t->stubs) != t->real[0] && stub_target(t->stubs + 4) != t->real[1];
}

static void test_stub_only_title(FakeTitle* t)
{
	CHECK(wait_for(stubs_patched, t, 8000));
	CHECK(wait_for(hooked, NULL, 2000));
	CHECK(stub_target(t->stubs) != 0 && stub_target(t->stubs + 4) != 0);
	CHECK(stub_target(t->stubs + 8) == 0xDEAD0000); // unrelated import untouched
}

// LEGO Dimensions on a console: it imports only XexGetProcedureAddress and
// looks the toy-pad functions up while running.
typedef DWORD (*GetProc_t)(HANDLE, DWORD, PVOID*);
static bool lookup_patched(void* p)
{
	FakeTitle* t = (FakeTitle*)p;
	return t->vars[0] != t->real[0];
}

static void test_lookup_title(FakeTitle* t)
{
	CHECK(wait_for(lookup_patched, t, 8000));
	usleep(300000);
	CHECK(!g_title_hooked); // not a toy game until it asks for a toy-pad function
	PVOID get = NULL, other = NULL;
	CHECK(((GetProc_t)t->vars[0])((HANDLE)0x1234, kOrdXamInputNonControllerGetRawEx, &get) == 0);
	CHECK(get != NULL && get != fake_real_function(kOrdXamInputNonControllerGetRawEx));
	CHECK(((GetProc_t)t->vars[0])((HANDLE)0x1234, kOrdXNotifyQueueUI, &other) == 0);
	CHECK(other == fake_real_function(kOrdXNotifyQueueUI)); // other lookups untouched
	CHECK(wait_for(hooked, NULL, 4000));
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	char dir[] = "/tmp/toyportal_testXXXXXX";
	fake::set_root(mkdtemp(dir));
	printf("fake console storage: %s\n", dir);

	test_jump_encoding();

	// Settings written before boot: logging on.
	mkdir(fake::host_path("TPHDD:\\").c_str(), 0755);
	mkdir(fake::host_path("TPHDD:\\ToyPortal").c_str(), 0755);
	FILE* ini = fopen(fake::host_path("TPHDD:\\ToyPortal\\toyportal.ini").c_str(), "wb");
	fputs("mode = emulate\r\nlog = 1\r\ncombo = LB + RB + BACK\r\n", ini);
	fclose(ini);

	static FakeTitle dim, sky, inf, unk, dash, stubonly;
	const DWORD ex[2] = {kOrdXamInputNonControllerGetRawEx, kOrdXamInputNonControllerSetRawEx};
	const DWORD plain[2] = {kOrdXamInputNonControllerGetRaw, kOrdXamInputNonControllerSetRaw};
	build_title(&dim, ex, 2, 1);
	build_title(&sky, plain, 2, 2);
	build_title(&inf, ex, 2, 3);
	build_title(&unk, ex, 2, 5);
	build_title(&dash, NULL, 0, 4);
	build_title(&stubonly, ex, 2, 6, true);
	static FakeTitle lookup;
	build_title(&lookup, ex, 1, 7);
	lookup.real[0] = (DWORD)(void*)XexGetProcedureAddress; // its one import
	lookup.vars[0] = lookup.real[0];

	fake::set_title(&dim.ldr, 0x4D530A13);
	DllMain(NULL, DLL_PROCESS_ATTACH, NULL);

	printf("-- jump stubs + Dimensions title\n");
	test_dimensions_title(&dim);
	printf("-- Skylanders title\n");
	fake::set_title(&sky.ldr, 0x41560883);
	test_skylanders_title(&sky);
	printf("-- Disney Infinity title\n");
	fake::set_title(&inf.ldr, 0x4D5309C4);
	test_infinity_title(&inf);
	printf("-- unknown protocol passes through\n");
	fake::set_title(&unk.ldr, 0x12345678);
	test_unknown_protocol(&unk);
	printf("-- dashboard (no toy imports)\n");
	fake::set_title(&dash.ldr, 0xFFFE07D1);
	test_no_toy_title(&dash);
	printf("-- title with call stubs only (no import pointers)\n");
	fake::set_title(&stubonly.ldr, 0x5752084B);
	test_stub_only_title(&stubonly);
	printf("-- title that looks the functions up (LEGO Dimensions)\n");
	fake::set_title(&lookup.ldr, 0x5752084C);
	test_lookup_title(&lookup);

	DllMain(NULL, DLL_PROCESS_DETACH, NULL);

	const std::string log = read_text("TPHDD:\\ToyPortal\\log.txt");
	CHECK(log.find("hooked 2 portal import(s)") != std::string::npos);
	CHECK(log.find("Detected Dimensions") != std::string::npos);
	CHECK(log.find("frames wrapped, prefix 0B") != std::string::npos);
	CHECK(log.find("Detected Skylanders") != std::string::npos);
	CHECK(log.find("Unrecognised portal protocol") != std::string::npos);
	CHECK(log.find("Detected Infinity") != std::string::npos);
	CHECK(log.find("emu>") != std::string::npos);
	CHECK(log.find("hooked the portal functions it looked up") != std::string::npos);

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
