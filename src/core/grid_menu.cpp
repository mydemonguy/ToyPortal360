// ToyPortal360 - the 4x4 character grid (see grid_menu.h).
#include "grid_menu.h"

static const u8 kFont[95][16] = {
#include "font8x16.inc"
};

const u8 kGridPalette[kGridColors][3] = {
	{18, 22, 30},    // back
	{52, 58, 72},    // cell
	{36, 132, 58},   // cell with its figure on the portal
	{255, 204, 0},   // cursor
	{255, 255, 255}, // text
	{176, 180, 190}, // dim text
	{210, 214, 222}, // border
	{30, 70, 140},   // title bar
	{0, 0, 0},       // toy pad lights: set while drawing
	{0, 0, 0},
	{0, 0, 0}
};

const char* grid_colour_name(u8 r, u8 g, u8 b)
{
	const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	if (mx < 24)
		return 0; // off
	if ((mx - mn) * 100 < mx * 25)
		return "White";
	// Hue in degrees.
	int h;
	const int d = mx - mn;
	if (mx == r)
		h = 60 * (g - b) / d;
	else if (mx == g)
		h = 120 + 60 * (b - r) / d;
	else
		h = 240 + 60 * (r - g) / d;
	if (h < 0)
		h += 360;
	if (h < 15 || h >= 345) return "Red";
	if (h < 40) return "Orange";
	if (h < 70) return "Yellow";
	if (h < 160) return "Green";
	if (h < 200) return "Cyan";
	if (h < 255) return "Blue";
	if (h < 320) return "Purple";
	return "Pink";
}

// ---- cursor and paging ------------------------------------------------------

GridMenu::GridMenu() : count_(0), cursor_(0) {}

void GridMenu::reset(int count)
{
	count_ = count > 0 ? count : 0;
	cursor_ = 0;
}

void GridMenu::go(int page, int row, int col)
{
	const int p = pages();
	page = ((page % p) + p) % p;
	cursor_ = page * kGridPerPage + row * kGridCols + col;
	if (cursor_ >= count_)
		cursor_ = count_ - 1;
	if (cursor_ < 0)
		cursor_ = 0;
}

GridAction GridMenu::press(u16 pressed)
{
	if (pressed & kBtnB)
		return kGridClose;
	if (count_ == 0)
		return kGridNothing;
	if (pressed & kBtnA)
		return kGridToggle;

	const int before = cursor_;
	const int in_page = cursor_ % kGridPerPage;
	const int row = in_page / kGridCols, col = in_page % kGridCols;
	const int page = cursor_ / kGridPerPage;
	if (pressed & kBtnUp)
	{
		if (row > 0)
			cursor_ -= kGridCols;
	}
	else if (pressed & kBtnDown)
	{
		const int below = page * kGridPerPage + (row + 1) * kGridCols; // first cell of the next row
		if (row + 1 < kGridRows && below < count_)
			cursor_ = cursor_ + kGridCols < count_ ? cursor_ + kGridCols : count_ - 1;
	}
	else if (pressed & kBtnLeft)
	{
		if (col > 0)
			cursor_--;
		else
			go(page - 1, row, kGridCols - 1);
	}
	else if (pressed & kBtnRight)
	{
		if (col + 1 < kGridCols && cursor_ + 1 < count_)
			cursor_++;
		else
			go(page + 1, row, 0);
	}
	else if (pressed & kBtnLB)
		go(page - 1, row, col);
	else if (pressed & kBtnRB)
		go(page + 1, row, col);
	return cursor_ != before ? kGridMoved : kGridNothing;
}

// ---- input --------------------------------------------------------------------

static const u16 kRepeatButtons = kBtnUp | kBtnDown | kBtnLeft | kBtnRight | kBtnLB | kBtnRB;
static const u32 kRepeatDelayMs = 350;
static const u32 kRepeatRateMs = 110;
static const short kStickDeadZone = 16000;

GridInput::GridInput() : prev_(0), ignore_(0), repeating_(0), next_repeat_(0) {}

void GridInput::reset(u16 held_now)
{
	prev_ = held_now;
	ignore_ = held_now;
	repeating_ = 0;
	next_repeat_ = 0;
}

u16 GridInput::with_stick(u16 buttons, short x, short y)
{
	if (x < -kStickDeadZone) buttons |= kBtnLeft;
	if (x > kStickDeadZone) buttons |= kBtnRight;
	if (y > kStickDeadZone) buttons |= kBtnUp;
	if (y < -kStickDeadZone) buttons |= kBtnDown;
	return buttons;
}

u16 GridInput::update(u16 buttons, short stick_x, short stick_y, u32 now_ms)
{
	u16 cur = with_stick(buttons, stick_x, stick_y);
	// Buttons held when the grid opened count only once let go.
	ignore_ &= cur;
	cur &= (u16)~ignore_;
	u16 pressed = (u16)(cur & ~prev_);
	const u16 held = (u16)(cur & kRepeatButtons);
	if (held != repeating_)
	{
		repeating_ = held;
		next_repeat_ = now_ms + kRepeatDelayMs;
	}
	else if (held && (int)(now_ms - next_repeat_) >= 0)
	{
		pressed |= held;
		next_repeat_ = now_ms + kRepeatRateMs;
	}
	prev_ = cur;
	return pressed;
}

// ---- drawing ------------------------------------------------------------------

static void fill(u8* image, int x, int y, int w, int h, u8 color)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > kGridImageW) w = kGridImageW - x;
	if (y + h > kGridImageH) h = kGridImageH - y;
	for (int j = 0; j < h; j++)
		memset(image + (y + j) * kGridImageW + x, color, w > 0 ? (size_t)w : 0);
}

static void frame(u8* image, int x, int y, int w, int h, int t, u8 color)
{
	fill(image, x, y, w, t, color);
	fill(image, x, y + h - t, w, t, color);
	fill(image, x, y, t, h, color);
	fill(image, x + w - t, y, t, h, color);
}

int grid_text(u8* image, int x, int y, const char* text, u8 color)
{
	for (; *text; text++, x += 8)
	{
		unsigned c = (unsigned char)*text;
		if (c < 32 || c > 126)
			c = '?';
		const u8* g = kFont[c - 32];
		for (int row = 0; row < 16; row++)
		{
			const int py = y + row;
			if (py < 0 || py >= kGridImageH || !g[row])
				continue;
			for (int b = 0; b < 8; b++)
			{
				const int px = x + b;
				if ((g[row] & (0x80 >> b)) && px >= 0 && px < kGridImageW)
					image[py * kGridImageW + px] = color;
			}
		}
	}
	return x;
}

int grid_wrap(const char* text, int cols, int max_lines, char (*out)[32])
{
	if (cols > 31)
		cols = 31;
	int lines = 0;
	const char* p = text;
	while (*p == ' ')
		p++;
	while (*p && lines < max_lines)
	{
		// Take as many whole words as fit; split a word longer than a line.
		int len = 0, fit = 0;
		while (p[len] && len <= cols)
		{
			if (p[len] == ' ')
				fit = len;
			len++;
		}
		if (!p[len] && len <= cols)
			fit = len;      // the rest fits
		else if (fit == 0)
			fit = cols;     // one long word
		memcpy(out[lines], p, (size_t)fit);
		out[lines][fit] = 0;
		p += fit;
		while (*p == ' ')
			p++;
		lines++;
		if (*p && lines == max_lines)
		{
			// More text than room: end the last line with "..".
			char* last = out[lines - 1];
			int n = (int)strlen(last);
			if (n > cols - 2)
				n = cols - 2;
			while (n > 0 && last[n - 1] == ' ')
				n--;
			last[n] = '.';
			last[n + 1] = '.';
			last[n + 2] = 0;
		}
	}
	return lines;
}

void grid_cell_origin(int i, int* x, int* y)
{
	*x = 7 + (i % kGridCols) * (kGridCellW + 6);
	*y = 30 + (i / kGridCols) * (kGridCellH + 6);
}

void grid_render(u8* image, const GridView& v)
{
	memset(image, kGridBack, kGridImageW * kGridImageH);
	frame(image, 0, 0, kGridImageW, kGridImageH, 2, kGridBorder);

	// Title bar: what it is on the left, the page on the right.
	fill(image, 2, 2, kGridImageW - 4, 24, kGridTitle);
	grid_text(image, 10, 6, v.title, kGridText);
	grid_text(image, kGridImageW - 10 - 8 * (int)strlen(v.page), 6, v.page, kGridText);

	for (int i = 0; i < v.cell_count && i < kGridPerPage; i++)
	{
		int x, y;
		grid_cell_origin(i, &x, &y);
		const GridCell& c = v.cells[i];
		fill(image, x, y, kGridCellW, kGridCellH, c.placed ? kGridCellOn : kGridCell);
		if (c.picture)
		{
			// The picture is full colour, so it is copied over the top part
			// separately; this image leaves that part alone, so the two never
			// take turns there. The square's colour stays round it and on the
			// strip, so a placed figure still shows green.
			fill(image, x + kGridPicX, y + kGridPicY, kGridPicW, kGridPicH, kGridSkip);
			char line[1][32];
			if (grid_wrap(c.name, kGridLineChars - (c.placed ? 1 : 0), 1, line))
				grid_text(image, x + 4, y + kGridStripY + 1, line[0], kGridText);
			if (c.placed)
				fill(image, x + kGridCellW - 10, y + kGridStripY + 6, 6, 6, kGridText);
		}
		else
		{
			if (c.placed) // a white mark in the corner as well as the colour
				fill(image, x + kGridCellW - 10, y + 5, 6, 6, kGridText);
			char lines[kGridLines][32];
			const int n = grid_wrap(c.name, kGridLineChars, kGridLines, lines);
			const int top = y + (kGridCellH - 16 * kGridLines) / 2 + (kGridLines - n) * 8;
			for (int l = 0; l < n; l++)
				grid_text(image, x + 5, top + l * 16, lines[l], kGridText);
		}
		if (i == v.cursor)
			frame(image, x - 2, y - 2, kGridCellW + 4, kGridCellH + 4, 4, kGridCursor);
	}

	if (v.choosing)
	{
		// "Batman:  [Left 1/3] [Centre Purple] [Right 0/3] [Off]"; a lit pad
		// gets a border in its light's colour.
		const int n = v.choose_count == 4 ? 4 : 3;
		const int y = kGridImageH - 25;
		const int bw = n == 4 ? 106 : 116, gap = n == 4 ? 6 : 8;
		int x = kGridImageW - 10 - n * bw - (n - 1) * gap;
		char text[24];
		int max_chars = (x - 16) / 8;
		if (max_chars > (int)sizeof(text) - 1)
			max_chars = (int)sizeof(text) - 1;
		strncpy(text, v.choose_text, max_chars);
		text[max_chars] = 0;
		grid_text(image, 10, y + 3, text, kGridText);
		for (int i = 0; i < n; i++, x += bw + gap)
		{
			const u8 inner = i == v.choose ? kGridCellOn : kGridCell;
			if (i < 3 && v.choose_lit[i])
			{
				fill(image, x, y, bw, 22, (u8)(kGridLight1 + i));
				fill(image, x + 3, y + 3, bw - 6, 16, inner);
			}
			else
				fill(image, x, y, bw, 22, inner);
			const int tw = 8 * (int)strlen(v.choose_names[i]);
			grid_text(image, x + (bw - tw) / 2, y + 3, v.choose_names[i], kGridText);
			if (i == v.choose)
				frame(image, x - 2, y - 2, bw + 4, 26, 2, kGridCursor);
		}
		return;
	}
	grid_text(image, 10, kGridImageH - 24, v.footer, kGridDim);
	if (v.show_lights)
	{
		// "Pad [L][C][R]" at the right, each square in its light's colour.
		static const char* const letters[3] = {"L", "C", "R"};
		int x = kGridImageW - 10 - 3 * 28 - 2 * 4;
		grid_text(image, x - 8 * 4, kGridImageH - 24, "Pad", kGridDim);
		for (int i = 0; i < 3; i++, x += 32)
		{
			fill(image, x, kGridImageH - 25, 28, 20, v.lit[i] ? (u8)(kGridLight1 + i) : kGridCell);
			grid_text(image, x + 10, kGridImageH - 23, letters[i], v.lit[i] ? kGridBack : kGridDim);
		}
	}
}

// ---- the user's pictures -------------------------------------------------------

const char* tpi_check(const u8* file, u32 size, u32* w, u32* h)
{
	*w = *h = 0;
	if (!file || size < (u32)kTpiHeader)
		return "too short";
	if (memcmp(file, "TPI1", 4) != 0)
		return "not a .tpi file (make it with make_pictures.py)";
	const u32 pw = rd_be16(file + 4), ph = rd_be16(file + 6);
	if (pw == 0 || ph == 0)
		return "empty picture";
	if (pw > (u32)kGridPicW || ph > (u32)kGridPicH)
		return "too big (largest is 128x38)";
	if (size < kTpiHeader + pw * ph * 4)
		return "cut short";
	if (size > kTpiHeader + pw * ph * 4)
		return "wrong size";
	*w = pw;
	*h = ph;
	return NULL;
}

void grid_picture_origin(int i, u32 w, u32 h, int* x, int* y)
{
	grid_cell_origin(i, x, y);
	*x += kGridPicX + (int)((kGridPicW - (w < (u32)kGridPicW ? w : (u32)kGridPicW)) / 2);
	*y += kGridPicY + (int)((kGridPicH - (h < (u32)kGridPicH ? h : (u32)kGridPicH)) / 2);
}
