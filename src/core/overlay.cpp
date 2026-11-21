// ToyPortal360 - drawing on the console's front buffer (see overlay.h).
#include "overlay.h"

static u32 bits(u32 v, unsigned shift, unsigned count)
{
	return (v >> shift) & ((1u << count) - 1);
}

bool overlay_parse_fetch(const u32* f, u32 address, OverlaySurface* out)
{
	memset(out, 0, sizeof(*out));
	out->tiled = bits(f[0], 31, 1) != 0;
	out->pitch = bits(f[0], 22, 9) << 5;
	out->gpu_format = bits(f[1], 0, 6);
	out->endian = bits(f[1], 6, 2);
	out->address = address ? address : bits(f[1], 12, 20) << 12;
	out->width = bits(f[2], 0, 13) + 1;
	out->height = bits(f[2], 13, 13) + 1;
	out->swizzle = bits(f[3], 1, 12);
	switch (out->gpu_format)
	{
	case 6:
	case 50:
		out->format = kOverlay8888;
		break;
	case 7:
	case 54:
		out->format = kOverlay2101010;
		break;
	default:
		out->format = kOverlayUnsupported;
		break;
	}
	if (out->pitch == 0)
		out->pitch = (out->width + 31) & ~31u;
	return out->format != kOverlayUnsupported && out->address != 0 && out->pitch >= out->width &&
		out->width >= 16 && out->height >= 16 && out->width <= 4096 && out->height <= 4096;
}

// Xenia's texture_address::TiledCombine and Tiled2D, for 4-byte blocks.
u32 overlay_tiled_offset(u32 x, u32 y, u32 pitch)
{
	const u32 outer_blocks = ((y >> 5) * (pitch >> 5) + (x >> 5)) << 6;
	const u32 inner_blocks = (((y >> 1) & 7) << 3) | (x & 7);
	const u32 outer_inner_bytes = (outer_blocks | inner_blocks) << 2;
	const u32 bank = (y >> 4) & 1;
	const u32 pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
	return ((y & 1) << 4) | (pipe << 6) | (bank << 11) |
		(outer_inner_bytes & 15) |
		(((outer_inner_bytes >> 4) & 1) << 5) |
		(((outer_inner_bytes >> 5) & 7) << 8) |
		(outer_inner_bytes >> 8 << 12);
}

u32 overlay_offset(const OverlaySurface& s, u32 x, u32 y)
{
	return s.tiled ? overlay_tiled_offset(x, y, s.pitch) : (y * s.pitch + x) * 4;
}

u32 overlay_span(const OverlaySurface& s)
{
	return s.tiled ? ((s.height + 31) & ~31u) * s.pitch * 4 : s.height * s.pitch * 4;
}

void overlay_row_range(const OverlaySurface& s, u32 y0, u32 y1, u32* first, u32* end)
{
	if (y1 <= y0)
	{
		*first = *end = 0;
		return;
	}
	if (s.tiled)
	{
		// A row of 32x32 tiles is a contiguous run of (pitch / 32) * 4 KB.
		const u32 row_bytes = (s.pitch >> 5) * 4096;
		*first = (y0 >> 5) * row_bytes;
		*end = (((y1 - 1) >> 5) + 1) * row_bytes;
	}
	else
	{
		*first = y0 * s.pitch * 4;
		*end = y1 * s.pitch * 4;
	}
}

static u32 bswap32(u32 v)
{
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

u32 overlay_pack(const OverlaySurface& s, u8 r, u8 g, u8 b)
{
	// Which stored component feeds each of R, G, B and A. Front buffers
	// are normally A8R8G8B8 (.zyxw); anything odd falls back to that.
	u32 sel[4];
	for (int c = 0; c < 4; c++)
		sel[c] = bits(s.swizzle, (unsigned)(3 * c), 3);
	const bool distinct = sel[0] < 4 && sel[1] < 4 && sel[2] < 4 &&
		sel[0] != sel[1] && sel[0] != sel[2] && sel[1] != sel[2];
	if (!distinct)
	{
		sel[0] = 2; sel[1] = 1; sel[2] = 0; sel[3] = 3;
	}
	else if (sel[3] >= 4)
		sel[3] = 6 - sel[0] - sel[1] - sel[2]; // the one left over

	const bool wide = s.format == kOverlay2101010;
	const u32 rgb[3] = {r, g, b};
	u32 comp[4] = {0, 0, 0, 0};
	for (int c = 0; c < 3; c++)
		comp[sel[c]] = wide ? (rgb[c] << 2) | (rgb[c] >> 6) : rgb[c];
	if (sel[3] < 4)
		comp[sel[3]] = wide ? (sel[3] == 3 ? 3u : 1023u) : 255u;

	u32 w = wide ? (comp[0] & 1023) | ((comp[1] & 1023) << 10) | ((comp[2] & 1023) << 20) | ((comp[3] & 3) << 30)
	             : comp[0] | (comp[1] << 8) | (comp[2] << 16) | (comp[3] << 24);

	// The GPU reads the bytes as a little-endian word, then applies the
	// endian swap. Undo that swap, then turn the little-endian word into
	// the value a big-endian store must write.
	switch (s.endian)
	{
	case 1: w = ((w & 0x00FF00FF) << 8) | ((w >> 8) & 0x00FF00FF); break;
	case 2: w = bswap32(w); break;
	case 3: w = (w << 16) | (w >> 16); break;
	default: break;
	}
	return bswap32(w);
}

// A tiled offset splits into a part from y and a part from x:
// offset = row_high + col_high + (col_low ^ row_low), the low parts sharing
// only the "pipe" bit they XOR on (see overlay_tiled_offset). The tests
// check the blit against overlay_offset for whole pictures.
static void tiled_row(u32 y, u32 pitch, u32* high, u32* low)
{
	*high = ((y >> 5) * (pitch >> 5)) << 12;
	*low = (((y >> 1) & 7) << 8) | ((y & 1) << 4) | (((y >> 3) & 1) << 7) | (((y >> 4) & 1) << 11);
}

static u32 tiled_col(u32 x, u32* low)
{
	*low = ((x & 3) << 2) | (((x >> 2) & 1) << 5) | (((x >> 3) & 3) << 6);
	return (x >> 5) << 12;
}

static inline void store_word(u8* p, u32 v)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	__builtin_memcpy(p, &v, 4); // the console: one plain store (p is 4-byte aligned)
#else
	wr_be32(p, v);
#endif
}

bool overlay_blit(const OverlaySurface& s, u8* mem, const u8* image, u32 iw, u32 ih, u32 scale,
	u32 ox, u32 oy, const u32* packed)
{
	static u32 line[4096]; // one row of the image, scaled and in final colours
	const u32 w = iw * scale, h = ih * scale;
	if (!mem || !scale || (ox & 3) || (w & 3) || w > 4096 || ox + w > s.width || oy + h > s.height)
		return false;
	for (u32 sy = 0; sy < ih; sy++)
	{
		const u8* src = image + sy * iw;
		u32* out = line;
		for (u32 sx = 0; sx < iw; sx++)
			for (u32 k = 0; k < scale; k++)
				*out++ = packed[src[sx]];
		for (u32 k = 0; k < scale; k++)
		{
			const u32 y = oy + sy * scale + k;
			u32 row_high = y * s.pitch * 4, row_low = 0;
			if (s.tiled)
				tiled_row(y, s.pitch, &row_high, &row_low);
			// Four pixels from a multiple of 4 are 16 contiguous bytes in
			// both layouts.
			for (u32 dx = 0; dx < w; dx += 4)
			{
				const u32 x = ox + dx;
				u32 off;
				if (s.tiled)
				{
					u32 col_low;
					const u32 col_high = tiled_col(x, &col_low);
					off = row_high + col_high + (col_low ^ row_low);
				}
				else
					off = row_high + x * 4;
				u8* p = mem + off;
				store_word(p, line[dx]);
				store_word(p + 4, line[dx + 1]);
				store_word(p + 8, line[dx + 2]);
				store_word(p + 12, line[dx + 3]);
			}
		}
	}
	return true;
}
