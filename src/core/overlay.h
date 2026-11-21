// ToyPortal360 - drawing on the console's front buffer.
//
// Every frame a game hands the kernel's VdSwap the D3D texture header
// ("fetch constant") of the picture about to be shown. From it we know
// where that picture is, its size, pixel format, byte order and whether
// it is tiled, so a small palette image (the character grid) can be copied
// over it with the CPU. The layout follows Xenia's reading of the same
// header (src/xenia/gpu/xenos.h, xe_gpu_texture_fetch_t) and its tiling
// formula (src/xenia/gpu/texture_address.h, Tiled2D).
#ifndef TOYPORTAL_OVERLAY_H
#define TOYPORTAL_OVERLAY_H

#include "bytes.h"

enum OverlayFormat
{
	kOverlay8888 = 0,       // k_8_8_8_8 (6) or k_8_8_8_8_AS_16_16_16_16 (50)
	kOverlay2101010 = 1,    // k_2_10_10_10 (7) or k_2_10_10_10_AS_16_16_16_16 (54)
	kOverlayUnsupported = 2
};

struct OverlaySurface
{
	u32 address;   // CPU address of pixel (0, 0)
	u32 width;
	u32 height;
	u32 pitch;     // pixels per row of storage (a multiple of 32)
	bool tiled;
	u32 format;    // OverlayFormat
	u32 gpu_format; // the raw TextureFormat number, for the log
	u32 endian;    // 0 none, 1 8in16, 2 8in32, 3 16in32
	u32 swizzle;   // 4 x 3 bits: which stored component feeds R, G, B, A
};

// Reads the six words VdSwap gets as its fetch pointer (as the CPU sees
// them, i.e. already in native order). `address` is the front buffer
// address VdSwap is given (0 = take it from the header). Returns false if
// it is not a 2D 32-bit picture we can draw on.
bool overlay_parse_fetch(const u32* fetch6, u32 address, OverlaySurface* out);

// Byte offset of 32-bit pixel (x, y) in a tiled surface of `pitch` pixels
// (Xenia's Tiled2D with 4-byte blocks).
u32 overlay_tiled_offset(u32 x, u32 y, u32 pitch);
// Byte offset of pixel (x, y) in the surface, tiled or linear.
u32 overlay_offset(const OverlaySurface& s, u32 x, u32 y);
// Bytes from the surface address to the end of its last row of tiles.
u32 overlay_span(const OverlaySurface& s);
// [first, end) bytes touched by rows y0..y1-1 (for cache flushing).
void overlay_row_range(const OverlaySurface& s, u32 y0, u32 y1, u32* first, u32* end);

// The 32-bit value to store (big-endian, as the CPU stores words) so the
// GPU reads colour r, g, b at full alpha.
u32 overlay_pack(const OverlaySurface& s, u8 r, u8 g, u8 b);

// Copies a palette image (one byte per pixel, `iw` x `ih`) to the surface
// at (ox, oy), each image pixel drawn as a `scale` x `scale` block. `mem` is
// where the surface's bytes are (the surface address on a console).
// `packed` holds overlay_pack() of each palette entry. ox and iw * scale
// must be multiples of 4. Pixels of value kOverlaySkip are left as they are
// (another blit draws there). Returns false if it would not fit.
enum { kOverlaySkip = 0xFF };
bool overlay_blit(const OverlaySurface& s, u8* mem, const u8* image, u32 iw, u32 ih, u32 scale,
	u32 ox, u32 oy, const u32* packed);

// overlay_pack() for any colour, from three tables: the R, G and B fields
// never share bits, so pack(r, g, b) == r_[r] | g_[g] | b_[b].
struct OverlayPacker
{
	u32 r_[256], g_[256], b_[256];
	u32 pack(u8 r, u8 g, u8 b) const { return r_[r] | g_[g] | b_[b]; }
};
void overlay_packer_init(const OverlaySurface& s, OverlayPacker* p);

// Copies a full-colour picture (`w` x `h` big-endian ARGB words, as in a
// .tpi file) to the surface at (ox, oy), each pixel as a `scale` x `scale`
// block. Pixels with alpha under 128 get `*background`, or are left as
// they are if it is NULL. Any position works. Returns false if it would
// not fit.
bool overlay_blit_argb(const OverlaySurface& s, u8* mem, const u8* argb, u32 w, u32 h, u32 scale,
	u32 ox, u32 oy, const OverlayPacker& p, const u32* background);

#endif
