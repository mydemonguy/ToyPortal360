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
	static u8 skip[4096 / 4]; // per group of 4: 0 draw all, 1 skip all, 2 mixed
	static u8 skip_px[4096];
	const u32 w = iw * scale, h = ih * scale;
	if (!mem || !scale || (ox & 3) || (w & 3) || w > 4096 || ox + w > s.width || oy + h > s.height)
		return false;
	for (u32 sy = 0; sy < ih; sy++)
	{
		const u8* src = image + sy * iw;
		u32* out = line;
		u8* sp = skip_px;
		for (u32 sx = 0; sx < iw; sx++)
			for (u32 k = 0; k < scale; k++)
			{
				*sp++ = src[sx] == kOverlaySkip;
				*out++ = packed[src[sx] == kOverlaySkip ? 0 : src[sx]];
			}
		for (u32 g = 0; g < w / 4; g++)
		{
			const u32 n = skip_px[4 * g] + skip_px[4 * g + 1] + skip_px[4 * g + 2] + skip_px[4 * g + 3];
			skip[g] = n == 0 ? 0 : n == 4 ? 1 : 2;
		}
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
				const u8 sk = skip[dx / 4];
				if (sk == 0)
				{
					store_word(p, line[dx]);
					store_word(p + 4, line[dx + 1]);
					store_word(p + 8, line[dx + 2]);
					store_word(p + 12, line[dx + 3]);
				}
				else if (sk == 2)
					for (u32 j = 0; j < 4; j++)
						if (!skip_px[dx + j])
							store_word(p + 4 * j, line[dx + j]);
			}
		}
	}
	return true;
}

// Where each 16-byte chunk of a 4 KB tile lands: x / 4 in the low 3 bits,
// y in the next 5. The same for every tile (see tiled_row / tiled_col).
static u8 s_chunk_x[256], s_chunk_y[256];
static bool s_chunks_ready = false;

static void make_chunk_table()
{
	for (u32 y = 0; y < 32; y++)
	{
		u32 rh, rl;
		tiled_row(y, 32, &rh, &rl);
		for (u32 g = 0; g < 8; g++)
		{
			u32 cl;
			tiled_col(g * 4, &cl);
			const u32 chunk = ((cl ^ rl) >> 4) & 255;
			s_chunk_x[chunk] = (u8)g;
			s_chunk_y[chunk] = (u8)y;
		}
	}
	s_chunks_ready = true;
}

bool overlay_blit_fast(const OverlaySurface& s, u8* mem, const u8* image, u32 iw, u32 ih, u32 scale,
	u32 ox, u32 oy, const u32* packed, OverlaySkipColour skip_colour, OverlayLineDone line_done, void* ctx)
{
	static u16 col_src[4096], row_src[4096];
	const u32 w = iw * scale, h = ih * scale;
	if (!mem || !scale || (ox & 3) || (w & 3) || w > 4096 || h > 4096 || ox + w > s.width || oy + h > s.height)
		return false;
	for (u32 i = 0; i < w; i++)
		col_src[i] = (u16)(i / scale);
	for (u32 i = 0; i < h; i++)
		row_src[i] = (u16)(i / scale);

	if (!s.tiled)
	{
		for (u32 y = oy; y < oy + h; y++)
		{
			const u32 iy = row_src[y - oy];
			const u8* src = image + iy * iw;
			u8* row = mem + y * s.pitch * 4;
			for (u32 x = ox; x < ox + w; x++)
			{
				const u32 ix = col_src[x - ox];
				const u8 c = src[ix];
				if (c != kOverlaySkip)
					store_word(row + x * 4, packed[c]);
				else if (skip_colour)
					store_word(row + x * 4, skip_colour(ix, iy, ctx));
			}
			if (line_done)
				for (u32 off = ((y * s.pitch + ox) * 4) & ~127u; off < (y * s.pitch + ox + w) * 4; off += 128)
					line_done(mem + off, ctx);
		}
		return true;
	}

	if (!s_chunks_ready)
		make_chunk_table();
	const u32 tiles_across = s.pitch >> 5;
	for (u32 ty = oy >> 5; ty <= (oy + h - 1) >> 5; ty++)
	{
		for (u32 tx = ox >> 5; tx <= (ox + w - 1) >> 5; tx++)
		{
			u8* tile = mem + ((ty * tiles_across + tx) << 12);
			for (u32 line = 0; line < 32; line++) // 32 lines of 128 bytes
			{
				bool wrote = false;
				for (u32 c = line * 8; c < line * 8 + 8; c++)
				{
					const u32 y = (ty << 5) + s_chunk_y[c];
					const u32 x = (tx << 5) + s_chunk_x[c] * 4;
					if (y < oy || y >= oy + h || x < ox || x >= ox + w)
						continue;
					const u32 iy = row_src[y - oy];
					const u8* src = image + iy * iw;
					u8* p = tile + (c << 4);
					for (u32 j = 0; j < 4; j++)
					{
						const u32 ix = col_src[x - ox + j];
						const u8 v = src[ix];
						if (v != kOverlaySkip)
							store_word(p + 4 * j, packed[v]);
						else if (skip_colour)
							store_word(p + 4 * j, skip_colour(ix, iy, ctx));
						else
							continue;
						wrote = true;
					}
				}
				if (wrote && line_done)
					line_done(tile + (line << 7), ctx);
			}
		}
	}
	return true;
}

void overlay_packer_init(const OverlaySurface& s, OverlayPacker* p)
{
	for (u32 i = 0; i < 256; i++)
	{
		p->r_[i] = overlay_pack(s, (u8)i, 0, 0);
		p->g_[i] = overlay_pack(s, 0, (u8)i, 0);
		p->b_[i] = overlay_pack(s, 0, 0, (u8)i);
	}
}

bool overlay_blit_argb(const OverlaySurface& s, u8* mem, const u8* argb, u32 w, u32 h, u32 scale,
	u32 ox, u32 oy, const OverlayPacker& p, const u32* background)
{
	if (!mem || !argb || !scale || !w || !h || ox + w * scale > s.width || oy + h * scale > s.height)
		return false;
	for (u32 sy = 0; sy < h; sy++)
	{
		const u8* src = argb + sy * w * 4;
		for (u32 k = 0; k < scale; k++)
		{
			const u32 y = oy + sy * scale + k;
			u32 row_high = y * s.pitch * 4, row_low = 0;
			if (s.tiled)
				tiled_row(y, s.pitch, &row_high, &row_low);
			for (u32 sx = 0; sx < w; sx++)
			{
				const u8* px = src + sx * 4;
				if (px[0] < 128 && !background)
					continue;
				const u32 v = px[0] < 128 ? *background : p.pack(px[1], px[2], px[3]);
				for (u32 j = 0; j < scale; j++)
				{
					const u32 x = ox + sx * scale + j;
					u32 off;
					if (s.tiled)
					{
						u32 col_low;
						const u32 col_high = tiled_col(x, &col_low);
						off = row_high + col_high + (col_low ^ row_low);
					}
					else
						off = row_high + x * 4;
					store_word(mem + off, v);
				}
			}
		}
	}
	return true;
}
