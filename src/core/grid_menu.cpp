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
	{30, 70, 140}    // title bar
};

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
		if (c.placed) // a white mark in the corner as well as the colour
			fill(image, x + kGridCellW - 10, y + 5, 6, 6, kGridText);
		char lines[kGridLines][32];
		const int n = grid_wrap(c.name, kGridLineChars, kGridLines, lines);
		const int top = y + 4 + (kGridLines - n) * 8;
		for (int l = 0; l < n; l++)
			grid_text(image, x + 5, top + l * 16, lines[l], kGridText);
		if (i == v.cursor)
			frame(image, x - 2, y - 2, kGridCellW + 4, kGridCellH + 4, 4, kGridCursor);
	}

	grid_text(image, 10, kGridImageH - 24, v.footer, kGridDim);
}
