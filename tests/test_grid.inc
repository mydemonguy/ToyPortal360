// Tests for the character grid's core: Xenia's tiling formula, colour
// packing, the texture header, copying the grid image, word wrapping,
// cursor/paging, key repeat and drawing. Included by test_core.cpp.
#include "../src/core/overlay.h"
#include "../src/core/grid_menu.h"
#include <vector>

static const u8 kFontForTests[95][16] = {
#include "../src/core/font8x16.inc"
};

// Xenia's texture_address::TiledCombine / Tiled2D, copied as written
// (signed arithmetic), as the reference for overlay_tiled_offset.
static int xenia_tiled_combine(int outer_inner_bytes, unsigned bank, unsigned pipe, unsigned y_lsb)
{
	return (int)((y_lsb << 4) | (pipe << 6) | (bank << 11)) |
		(outer_inner_bytes & 15) |
		(((outer_inner_bytes >> 4) & 1) << 5) |
		(((outer_inner_bytes >> 5) & 7) << 8) |
		(outer_inner_bytes >> 8 << 12);
}

static int xenia_tiled_2d(int x, int y, unsigned pitch_aligned, unsigned bytes_per_block_log2)
{
	const int outer_blocks = ((y >> 5) * (int)(pitch_aligned >> 5) + (x >> 5)) << 6;
	const int inner_blocks = (((y >> 1) & 7) << 3) | (x & 7);
	const int outer_inner_bytes = (outer_blocks | inner_blocks) << bytes_per_block_log2;
	const unsigned bank = (unsigned)(y >> 4) & 1;
	const unsigned pipe = ((unsigned)(x >> 3) & 3) ^ (((unsigned)(y >> 3) & 1) << 1);
	return xenia_tiled_combine(outer_inner_bytes, bank, pipe, (unsigned)y & 1);
}

static OverlaySurface make_surface(u32 w, u32 h, bool tiled, u32 format, u32 endian, u32 swizzle)
{
	OverlaySurface s;
	memset(&s, 0, sizeof(s));
	s.width = w;
	s.height = h;
	s.pitch = (w + 31) & ~31u;
	s.tiled = tiled;
	s.format = format;
	s.endian = endian;
	s.swizzle = swizzle;
	s.address = 0x10000000;
	return s;
}

static const u32 kSwizzleZYXW = 2 | (1 << 3) | (0 << 6) | (3 << 9); // A8R8G8B8
static const u32 kSwizzleXYZW = 0 | (1 << 3) | (2 << 6) | (3 << 9);

static void test_overlay_tiling()
{
	// Same as Xenia's formula, for the pitches front buffers have.
	static const u32 pitches[] = {640, 1024, 1280, 1920, 32, 864};
	int mismatches = 0;
	for (int p = 0; p < 6; p++)
		for (u32 y = 0; y < 100; y++)
			for (u32 x = 0; x < pitches[p]; x += (y % 7 == 0) ? 1 : 13)
				if (overlay_tiled_offset(x, y, pitches[p]) != (u32)xenia_tiled_2d((int)x, (int)y, pitches[p], 2))
					mismatches++;
	for (int i = 0; i < 20000; i++)
	{
		const u32 x = (u32)(rnd8() | (rnd8() << 8)) % 1280, y = (u32)(rnd8() | (rnd8() << 8)) % 736;
		if (overlay_tiled_offset(x, y, 1280) != (u32)xenia_tiled_2d((int)x, (int)y, 1280, 2))
			mismatches++;
	}
	CHECK(mismatches == 0);

	// Every pixel of a 1280x720 picture has its own 4 bytes inside the span.
	OverlaySurface s = make_surface(1280, 720, true, kOverlay8888, 2, kSwizzleZYXW);
	const u32 span = overlay_span(s);
	CHECK(span == 1280u * 736u * 4u);
	std::vector<bool> used(span / 4, false);
	int overlaps = 0, outside = 0, unaligned = 0;
	for (u32 y = 0; y < 720; y++)
		for (u32 x = 0; x < 1280; x++)
		{
			const u32 o = overlay_offset(s, x, y);
			if (o & 3) { unaligned++; continue; }
			if (o >= span) { outside++; continue; }
			if (used[o / 4]) overlaps++;
			used[o / 4] = true;
		}
	CHECK(overlaps == 0 && outside == 0 && unaligned == 0);

	// Four pixels from a multiple of 4 are 16 contiguous bytes (the blit relies on it).
	int broken = 0;
	for (u32 y = 0; y < 64; y++)
		for (u32 x = 0; x < 1280; x += 4)
			for (u32 k = 1; k < 4; k++)
				if (overlay_offset(s, x + k, y) != overlay_offset(s, x, y) + 4 * k)
					broken++;
	CHECK(broken == 0);

	// The flush range of some rows covers every byte of those rows.
	int missed = 0;
	for (int t = 0; t < 50; t++)
	{
		const u32 y0 = (u32)rnd8() * 2 % 700, y1 = y0 + 1 + rnd8() % 20;
		u32 first, end;
		overlay_row_range(s, y0, y1, &first, &end);
		for (u32 y = y0; y < y1; y++)
			for (u32 x = 0; x < 1280; x += 3)
			{
				const u32 o = overlay_offset(s, x, y);
				if (o < first || o + 4 > end)
					missed++;
			}
	}
	CHECK(missed == 0);

	// Linear pictures are plain rows.
	OverlaySurface lin = make_surface(640, 480, false, kOverlay8888, 2, kSwizzleZYXW);
	CHECK(overlay_offset(lin, 3, 2) == (2 * 640 + 3) * 4);
	CHECK(overlay_span(lin) == 640u * 480u * 4u);
}

static void test_overlay_pack_and_fetch()
{
	// A8R8G8B8, 8-in-32 swap (the usual front buffer): ARGB in a native word.
	OverlaySurface s = make_surface(1280, 720, true, kOverlay8888, 2, kSwizzleZYXW);
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0xFF112233u);
	// No swap: the GPU reads the bytes little-endian, so the native word is reversed.
	s.endian = 0;
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0x332211FFu);
	// 8-in-16 and 16-in-32.
	s.endian = 1;
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0x2233FF11u);
	s.endian = 3;
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0x11FF3322u);
	// R in the low byte (.xyzw).
	s.endian = 2;
	s.swizzle = kSwizzleXYZW;
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0xFF332211u);
	// A swizzle that makes no sense falls back to A8R8G8B8.
	s.swizzle = 0;
	CHECK(overlay_pack(s, 0x11, 0x22, 0x33) == 0xFF112233u);
	// 2_10_10_10: 10-bit colours, 2-bit alpha.
	s.format = kOverlay2101010;
	s.swizzle = kSwizzleXYZW;
	CHECK(overlay_pack(s, 0xFF, 0, 0) == 0xC00003FFu);
	CHECK(overlay_pack(s, 0, 0, 0xFF) == 0xFFF00000u);
	s.swizzle = kSwizzleZYXW;
	CHECK(overlay_pack(s, 0xFF, 0, 0) == 0xFFF00000u);

	// The texture header VdSwap gets (Xenia's xe_gpu_texture_fetch_t).
	u32 f[6] = {0, 0, 0, 0, 0, 0};
	f[0] = (1u << 31) | ((1280u / 32) << 22) | 2;     // tiled, pitch, type
	f[1] = 6 | (2u << 6) | ((0xBF234000u >> 12) << 12); // k_8_8_8_8, 8in32, address
	f[2] = 1279 | (719u << 13);
	f[3] = kSwizzleZYXW << 1;
	OverlaySurface p;
	CHECK(overlay_parse_fetch(f, 0, &p));
	CHECK(p.tiled && p.pitch == 1280 && p.width == 1280 && p.height == 720 && p.format == kOverlay8888 &&
		p.gpu_format == 6 && p.endian == 2 && p.swizzle == kSwizzleZYXW && p.address == 0xBF234000u);
	CHECK(overlay_parse_fetch(f, 0xA0100000u, &p) && p.address == 0xA0100000u);
	f[1] = (f[1] & ~63u) | 54; // k_2_10_10_10_AS_16_16_16_16
	CHECK(overlay_parse_fetch(f, 0, &p) && p.format == kOverlay2101010);
	f[1] = (f[1] & ~63u) | 26; // k_16_16_16_16: not drawn on
	CHECK(!overlay_parse_fetch(f, 0, &p) && p.gpu_format == 26);
	f[1] = (f[1] & ~63u) | 6;
	f[0] &= ~(511u << 22); // no pitch given: rounded-up width
	f[0] &= ~(1u << 31);
	CHECK(overlay_parse_fetch(f, 0, &p) && !p.tiled && p.pitch == 1280);
}

static void test_overlay_blit()
{
	// Copy a random palette image at 1x and 2x into fake tiled memory and
	// read it back through the tiling formula.
	static u8 image[kGridImageW * kGridImageH];
	for (int i = 0; i < kGridImageW * kGridImageH; i++)
		image[i] = (u8)(rnd8() % kGridColors);
	// width, height, scale, tiled
	const u32 cases[4][4] = {{640, 480, 1, 1}, {1280, 720, 2, 1}, {1920, 1080, 3, 1}, {1280, 720, 2, 0}};
	for (int t = 0; t < 4; t++)
	{
		OverlaySurface s = make_surface(cases[t][0], cases[t][1], cases[t][3] != 0, kOverlay8888, 2, kSwizzleZYXW);
		std::vector<u8> mem(overlay_span(s), 0xEE);
		u32 packed[kGridColors];
		for (int c = 0; c < kGridColors; c++)
			packed[c] = overlay_pack(s, kGridPalette[c][0], kGridPalette[c][1], kGridPalette[c][2]);
		const u32 scale = cases[t][2], ox = t ? 64 + 4 * t : 32, oy = t ? 50 + t : 88;
		CHECK(overlay_blit(s, &mem[0], image, kGridImageW, kGridImageH, scale, ox, oy, packed));
		int wrong = 0, touched_outside = 0;
		for (u32 y = 0; y < s.height; y++)
			for (u32 x = 0; x < s.width; x++)
			{
				const u32 v = rd_be32(&mem[overlay_offset(s, x, y)]);
				const bool inside = x >= ox && y >= oy && x < ox + kGridImageW * scale && y < oy + kGridImageH * scale;
				if (inside && v != packed[image[((y - oy) / scale) * kGridImageW + (x - ox) / scale]])
					wrong++;
				if (!inside && v != 0xEEEEEEEEu)
					touched_outside++;
			}
		CHECK(wrong == 0 && touched_outside == 0);
	}
	// Refuses what doesn't fit or isn't 4-aligned.
	OverlaySurface small = make_surface(512, 300, true, kOverlay8888, 2, kSwizzleZYXW);
	std::vector<u8> mem(overlay_span(small));
	u32 packed[kGridColors] = {0};
	CHECK(!overlay_blit(small, &mem[0], image, kGridImageW, kGridImageH, 1, 0, 0, packed));
	OverlaySurface big = make_surface(640, 480, true, kOverlay8888, 2, kSwizzleZYXW);
	std::vector<u8> mem2(overlay_span(big));
	CHECK(!overlay_blit(big, &mem2[0], image, kGridImageW, kGridImageH, 1, 30, 0, packed));
}

static void test_grid_wrap()
{
	char l[3][32];
	CHECK(grid_wrap("Spyro", 15, 3, l) == 1 && strcmp(l[0], "Spyro") == 0);
	CHECK(grid_wrap("Series 2 Lightning Rod", 15, 3, l) == 2 && strcmp(l[0], "Series 2") == 0 && strcmp(l[1], "Lightning Rod") == 0);
	CHECK(grid_wrap("Legendary Series 2 Chop Chop and Friends Edition", 15, 3, l) == 3 &&
		strcmp(l[0], "Legendary") == 0 && strcmp(l[1], "Series 2 Chop") == 0 && strcmp(l[2], "Chop and..") == 0);
	CHECK(grid_wrap("Supercalifragilisticexpialidocious", 15, 3, l) == 3 &&
		strcmp(l[0], "Supercalifragil") == 0 && strcmp(l[1], "isticexpialidoc") == 0 && strcmp(l[2], "ious") == 0);
	CHECK(grid_wrap("Exactly fifteen", 15, 3, l) == 1 && strcmp(l[0], "Exactly fifteen") == 0);
	CHECK(grid_wrap("", 15, 3, l) == 0);
	// Every real name fits in a cell.
	int count = 0, bad = 0;
	const SkyName* s = sky_names(&count);
	for (int i = 0; i < count; i++)
	{
		const int n = grid_wrap(s[i].name, 15, 3, l);
		for (int k = 0; k < n; k++)
			if ((int)strlen(l[k]) > 15)
				bad++;
		if (n < 1)
			bad++;
	}
	CHECK(bad == 0);
}

static void test_grid_menu()
{
	GridMenu m;
	m.reset(37); // pages of 16, 16, 5
	CHECK(m.pages() == 3 && m.cursor() == 0 && m.page() == 0);
	CHECK(m.press(kBtnRight) == kGridMoved && m.cursor() == 1);
	CHECK(m.press(kBtnDown) == kGridMoved && m.cursor() == 5);
	CHECK(m.press(kBtnUp) == kGridMoved && m.cursor() == 1);
	CHECK(m.press(kBtnUp) == kGridNothing && m.cursor() == 1);
	CHECK(m.press(kBtnLeft) == kGridMoved && m.cursor() == 0);
	// Left from the first column goes to the previous page (wrapping round).
	CHECK(m.press(kBtnLeft) == kGridMoved && m.cursor() == 35 && m.page() == 2);
	// Right from the last column wraps to the first page.
	CHECK(m.press(kBtnRight) == kGridMoved && m.cursor() == 0);
	// RB/LB keep the place on the page.
	m.press(kBtnDown); m.press(kBtnRight); // cursor 5
	CHECK(m.press(kBtnRB) == kGridMoved && m.cursor() == 21 && m.page() == 1);
	CHECK(m.press(kBtnRB) == kGridMoved && m.cursor() == 36); // page 3 has 5 items: clamped
	CHECK(m.press(kBtnRB) == kGridMoved && m.cursor() == 4);  // wraps round, row 1 col 0
	CHECK(m.press(kBtnLB) == kGridMoved && m.page() == 2);
	// Down on a short last page stops where there is no row below.
	m.reset(37);
	m.press(kBtnLB); // page 3: items 32-36
	CHECK(m.cursor() == 32);
	CHECK(m.press(kBtnDown) == kGridMoved && m.cursor() == 36);
	CHECK(m.press(kBtnDown) == kGridNothing);
	CHECK(m.press(kBtnA) == kGridToggle && m.press(kBtnB) == kGridClose);
	// Right along the last row of a page moves to the next page.
	m.reset(40);
	for (int i = 0; i < 6; i++)
		m.press(i < 3 ? kBtnDown : kBtnRight);
	CHECK(m.cursor() == 15);
	CHECK(m.press(kBtnRight) == kGridMoved && m.cursor() == 28 && m.page() == 1);
	// No figures: only B does anything.
	m.reset(0);
	CHECK(m.press(kBtnA) == kGridNothing && m.press(kBtnRight) == kGridNothing && m.press(kBtnB) == kGridClose);
}

static void test_grid_input()
{
	GridInput in;
	// The combo that opened the grid (LB+RB+Y) is ignored until let go.
	in.reset(kBtnLB | kBtnRB | kBtnY);
	CHECK(in.update(kBtnLB | kBtnRB | kBtnY, 0, 0, 1000) == 0);
	CHECK(in.update(kBtnLB | kBtnRB | kBtnY, 0, 0, 2000) == 0); // no key repeat either
	CHECK(in.update(kBtnLB, 0, 0, 2010) == 0);
	CHECK(in.update(0, 0, 0, 2020) == 0);
	CHECK(in.update(kBtnRB, 0, 0, 2030) == kBtnRB); // a new press counts
	CHECK(in.update(0, 0, 0, 2040) == 0);
	CHECK(in.update(kBtnA, 0, 0, 2050) == kBtnA);
	CHECK(in.update(kBtnA, 0, 0, 3000) == 0); // A does not repeat
	CHECK(in.update(0, 0, 0, 3010) == 0);
	// Held d-pad: one press, a pause, then repeats.
	CHECK(in.update(kBtnRight, 0, 0, 4000) == kBtnRight);
	CHECK(in.update(kBtnRight, 0, 0, 4300) == 0);
	CHECK(in.update(kBtnRight, 0, 0, 4350) == kBtnRight);
	CHECK(in.update(kBtnRight, 0, 0, 4400) == 0);
	CHECK(in.update(kBtnRight, 0, 0, 4460) == kBtnRight);
	CHECK(in.update(0, 0, 0, 4470) == 0);
	// The left stick works like the d-pad (up is positive).
	CHECK(in.update(0, 0, 30000, 5000) == kBtnUp);
	CHECK(in.update(0, 0, 5000, 5010) == 0); // dead zone
	CHECK(in.update(0, -30000, 0, 5020) == kBtnLeft);
	CHECK(GridInput::with_stick(0, 20000, -20000) == (kBtnRight | kBtnDown));
}

static void test_grid_render()
{
	static u8 img[kGridImageW * kGridImageH];
	GridView v;
	memset(&v, 0, sizeof(v));
	strcpy(v.title, "Toy Portal 360 - Skylanders");
	strcpy(v.page, "Page 1 of 31");
	strcpy(v.footer, "A: place/remove");
	for (int i = 0; i < 6; i++)
		strcpy(v.cells[i].name, "Spyro");
	v.cells[2].placed = true;
	v.cell_count = 6;
	v.cursor = 5;
	grid_render(img, v);
#define PX(x, y) img[(y) * kGridImageW + (x)]
	CHECK(PX(0, 0) == kGridBorder && PX(kGridImageW - 1, kGridImageH - 1) == kGridBorder);
	CHECK(PX(3, 3) == kGridTitle);
	int x, y;
	grid_cell_origin(0, &x, &y);
	CHECK(x == 7 && y == 30);
	CHECK(PX(x + kGridCellW - 2, y + kGridCellH - 2) == kGridCell);
	grid_cell_origin(2, &x, &y);
	CHECK(PX(x + kGridCellW - 2, y + kGridCellH - 2) == kGridCellOn);
	CHECK(PX(x + kGridCellW - 8, y + 7) == kGridText); // the "on the portal" mark
	grid_cell_origin(5, &x, &y);
	CHECK(PX(x - 1, y + 20) == kGridCursor && PX(x + 1, y + 20) == kGridCursor && PX(x + 3, y + 20) == kGridCell);
	grid_cell_origin(6, &x, &y); // past cell_count: nothing drawn
	CHECK(PX(x + 10, y + 10) == kGridBack);
	grid_cell_origin(15, &x, &y);
	CHECK(x + kGridCellW + 2 <= kGridImageW && y + kGridCellH + 2 < kGridImageH - 24);
	// Some text was drawn in the cell, the title and the footer.
	int cell_text = 0, title_text = 0, footer_text = 0;
	grid_cell_origin(0, &x, &y);
	for (int j = 0; j < kGridCellH; j++)
		for (int i = 0; i < kGridCellW; i++)
			cell_text += PX(x + i, y + j) == kGridText;
	for (int j = 2; j < 26; j++)
		for (int i = 0; i < kGridImageW; i++)
			title_text += PX(i, j) == kGridText;
	for (int j = kGridImageH - 24; j < kGridImageH - 2; j++)
		for (int i = 0; i < kGridImageW; i++)
			footer_text += PX(i, j) == kGridDim;
	CHECK(cell_text > 40 && title_text > 200 && footer_text > 60);

	// A glyph is drawn exactly as the font has it.
	memset(img, 0, sizeof(img));
	CHECK(grid_text(img, 100, 100, "A", 4) == 108);
	int differ = 0;
	for (int j = 0; j < 16; j++)
		for (int i = 0; i < 8; i++)
			differ += (PX(100 + i, 100 + j) == 4) != ((kFontForTests['A' - 32][j] & (0x80 >> i)) != 0);
	CHECK(differ == 0);
	int lit = 0;
	for (int j = 0; j < 16; j++)
		for (int i = 0; i < 8; i++)
			lit += PX(100 + i, 100 + j) == 4;
	CHECK(lit > 15);
#undef PX
}

static void test_grid()
{
	test_overlay_tiling();
	test_overlay_pack_and_fetch();
	test_overlay_blit();
	test_grid_wrap();
	test_grid_menu();
	test_grid_input();
	test_grid_render();
}
