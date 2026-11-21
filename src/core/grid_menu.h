// ToyPortal360 - the 4x4 character grid: cursor and paging, controller
// input with key repeat, and drawing the grid into a small palette image
// (overlay.h copies that image onto the game's picture).
#ifndef TOYPORTAL_GRID_MENU_H
#define TOYPORTAL_GRID_MENU_H

#include "bytes.h"

// Controller buttons (the XINPUT_GAMEPAD_* values).
enum
{
	kBtnUp = 0x0001,
	kBtnDown = 0x0002,
	kBtnLeft = 0x0004,
	kBtnRight = 0x0008,
	kBtnStart = 0x0010,
	kBtnBack = 0x0020,
	kBtnLB = 0x0100,
	kBtnRB = 0x0200,
	kBtnA = 0x1000,
	kBtnB = 0x2000,
	kBtnX = 0x4000,
	kBtnY = 0x8000
};

enum
{
	kGridCols = 4,
	kGridRows = 4,
	kGridPerPage = kGridCols * kGridRows,
	kGridImageW = 576, // the image at 1x; drawn 2x at 720p, 3x at 1080p
	kGridImageH = 304,
	kGridCellW = 136,
	kGridCellH = 56,
	kGridLineChars = 15,
	kGridLines = 3
};

enum GridColor
{
	kGridBack,
	kGridCell,
	kGridCellOn,
	kGridCursor,
	kGridText,
	kGridDim,
	kGridBorder,
	kGridTitle,
	kGridColors
};
extern const u8 kGridPalette[kGridColors][3]; // RGB

enum GridAction
{
	kGridNothing,
	kGridMoved,  // the cursor or page changed
	kGridToggle, // A: place or remove the figure under the cursor
	kGridClose   // B
};

class GridMenu
{
public:
	GridMenu();
	void reset(int count);
	int count() const { return count_; }
	int cursor() const { return cursor_; }
	int page() const { return cursor_ / kGridPerPage; }
	int pages() const { return count_ ? (count_ + kGridPerPage - 1) / kGridPerPage : 1; }
	// `pressed`: buttons that went down (or repeated) since the last call.
	GridAction press(u16 pressed);

private:
	void go(int page, int row, int col);
	int count_;
	int cursor_;
};

// Turns the controller state into button presses: new presses, plus key
// repeat for the d-pad, left stick and bumpers while they are held.
class GridInput
{
public:
	GridInput();
	// Ignores buttons already held (the combo that opened the grid).
	void reset(u16 held_now);
	u16 update(u16 buttons, short stick_x, short stick_y, u32 now_ms);
	static u16 with_stick(u16 buttons, short stick_x, short stick_y);

private:
	u16 prev_;
	u16 ignore_;
	u16 repeating_;
	u32 next_repeat_;
};

struct GridCell
{
	char name[64];
	bool placed;
};

struct GridView
{
	char title[48];
	char page[24];
	char footer[72];
	GridCell cells[kGridPerPage];
	int cell_count;  // cells on this page
	int cursor;      // index within the page, or -1
};

// Word-wraps `text` into at most `max_lines` lines of `cols` characters
// (cols <= 31). A line that does not fit ends in "..". Returns the lines used.
int grid_wrap(const char* text, int cols, int max_lines, char (*out)[32]);

// Top-left corner of cell i (0-15) in the image.
void grid_cell_origin(int i, int* x, int* y);

// Draws the whole grid into `image` (kGridImageW x kGridImageH bytes of
// GridColor values).
void grid_render(u8* image, const GridView& v);

// Draws text in the 8x16 font. Returns the x after the last character.
int grid_text(u8* image, int x, int y, const char* text, u8 color);

#endif
