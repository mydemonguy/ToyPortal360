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
	kGridImageH = 320, // 2x and 3x must fit in 90% of 720 and 1080 lines
	kGridCellW = 136,
	kGridCellH = 60,
	kGridLineChars = 15,
	kGridLines = 3,
	// A square with a picture: the picture in the top part, the name on a
	// one-line strip under it.
	kGridPicX = 4,     // where the picture area starts in the square
	kGridPicY = 4,
	kGridPicW = 128,   // the largest picture, at 1x
	kGridPicH = 38,
	kGridStripY = 42   // the name strip, to the bottom of the square
};

// The user's own pictures (.tpi, made by tools/make_pictures.py): "TPI1",
// width and height as big-endian 16-bit numbers, then width * height
// big-endian ARGB pixels, row by row. Pixels with alpha under 128 are not
// drawn, so the square shows through. Toy Portal 360 ships no pictures.
enum
{
	kTpiHeader = 8,
	kTpiMaxBytes = kTpiHeader + kGridPicW * kGridPicH * 4
};
// Checks a .tpi file of `size` bytes. Returns NULL if it can be drawn
// (setting *w and *h), or why not.
const char* tpi_check(const u8* file, u32 size, u32* w, u32* h);
// Where a w x h picture goes in square i (centred in the picture area,
// which grid_render leaves as kGridSkip for the picture to be drawn in).
void grid_picture_origin(int i, u32 w, u32 h, int* x, int* y);

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
	kGridLight1, // the toy pad's lights (left, centre, right); the console
	kGridLight2, // sets these from the game's light commands
	kGridLight3,
	kGridColors,
	kGridSkip = 0xFF // left for the picture (overlay.h kOverlaySkip)
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
	bool picture; // the user has a picture for it: name on one line at the bottom
};

struct GridView
{
	char title[48];
	char page[24];
	char footer[72];
	GridCell cells[kGridPerPage];
	int cell_count;  // cells on this page
	int cursor;      // index within the page, or -1
	// Picking a toy pad spot (LEGO Dimensions): drawn instead of the footer.
	bool choosing;
	int choose_count;          // 3 (left, centre, right) or 4 (+ "Off")
	char choose_text[24];      // e.g. "Batman:"
	char choose_names[4][16];  // e.g. "Left 1/3", "Left Purple", "Off"
	bool choose_lit[3];        // that pad is lit (kGridLight1 + i)
	int choose;                // the highlighted one
	// The pad lights as three small squares in the footer (LEGO Dimensions).
	bool show_lights;
	bool lit[3];               // left, centre, right
};

// A rough name for a light colour ("Purple"), or 0 when it's off.
const char* grid_colour_name(u8 r, u8 g, u8 b);

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
