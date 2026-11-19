// ToyPortal360 - packet router and 360 frame wrapping.
#include "toy_router.h"

const char* toy_kind_name(ToyKind k)
{
	switch (k)
	{
	case kToySkylanders: return "Skylanders";
	case kToyDimensions: return "Dimensions";
	case kToyInfinity: return "Infinity";
	default: return "Unknown";
	}
}

static bool is_skylander_command(u8 c)
{
	switch (c)
	{
	case 'A': case 'C': case 'J': case 'L': case 'M':
	case 'Q': case 'R': case 'S': case 'V': case 'W':
		return true;
	default:
		return false;
	}
}

ToyRouter::ToyRouter()
	: status_interval_ms(20), sky_wrap_len(0)
{
	reset(kToyUnknown);
}

void ToyRouter::reset(ToyKind forced)
{
	sky.reset();
	dim.reset();
	inf.reset();
	kind_ = forced;
	wrap_offset_ = -1;
	wrap_prefix_ = 0;
	game_len_byte_ = -1;
	last_status_ms_ = 0;
}

// First byte of a command frame for each device.
static bool starts_frame(ToyKind k, u8 b)
{
	switch (k)
	{
	case kToySkylanders: return is_skylander_command(b);
	case kToyDimensions: return b == 0x55;
	case kToyInfinity: return b == 0xFF;
	default: return false;
	}
}

// Works out the device (if still unknown) and whether frames are wrapped.
// Bare frames are checked for every device before any wrapped form, so a
// payload byte can never be mistaken for a wrapped frame's start.
bool ToyRouter::detect(const u8* buf, u32 len)
{
	static const ToyKind kinds[3] = {kToyDimensions, kToyInfinity, kToySkylanders};
	ToyKind found = kToyUnknown;
	int offset = -1;
	for (int pass = 0; pass < 2 && offset < 0; pass++)
	{
		const int off = pass == 0 ? 0 : 2;
		if (off && (len < 3 || buf[0] != 0x0B))
			break; // the 360's wrapper always starts with 0x0B
		for (int i = 0; i < 3; i++)
		{
			const ToyKind k = kinds[i];
			if (kind_ != kToyUnknown && k != kind_)
				continue;
			if (starts_frame(k, buf[off]))
			{
				found = k;
				offset = off;
				break;
			}
		}
	}
	if (offset < 0)
		return false;

	kind_ = found;
	if (offset != wrap_offset_)
	{
		wrap_offset_ = offset;
		wrap_prefix_ = offset ? buf[0] : 0;
	}
	return true;
}

bool ToyRouter::on_write(const u8* buf, u32 len)
{
	if (!buf || len == 0)
		return false;
	if (len > kPacketSize)
		len = kPacketSize;
	if (!detect(buf, len))
		return false;

	if (wrap_offset_ > 0)
	{
		const int lb = buf[1];
		if (game_len_byte_ == -1)
			game_len_byte_ = lb;
		else if (game_len_byte_ != lb)
			game_len_byte_ = -2;
	}

	// Commands are handled as full 32-byte packets (zero padded),
	// which is what the real hardware receives.
	u8 cmd[kPacketSize];
	memset(cmd, 0, sizeof(cmd));
	memcpy(cmd, buf + wrap_offset_, len - wrap_offset_);

	if (kind_ == kToySkylanders)
		sky.handle_command(cmd, kPacketSize);
	else if (kind_ == kToyDimensions)
		dim.handle_command(cmd, kPacketSize);
	else
		inf.handle_command(cmd, kPacketSize);
	return true;
}

u8 ToyRouter::sky_reply_len(const u8* pkt) const
{
	if (sky_wrap_len)
		return (u8)sky_wrap_len;
	if (game_len_byte_ >= 0)
		return (u8)game_len_byte_;
	// Reply sizes as produced by the portal protocol (zeros can be meaningful,
	// e.g. an empty slot's 'Q' reply, so this can't be worked out from content).
	switch (pkt[0])
	{
	case 'Q': return 19; // Q, slot, block, 16 data bytes
	case 'S': return 7;  // S, 4 status bytes, counter, 0x01
	case 'A': return 4;
	case 'M': return 4;
	case 'R': return 3;
	case 'W': return 3;
	case 'J': return 1;
	default:
	{
		int n = kPacketSize;
		while (n > 1 && pkt[n - 1] == 0)
			n--;
		return (u8)n;
	}
	}
}

u32 ToyRouter::on_read(u8* out, u32 max_len, u32 now_ms)
{
	if (!out || max_len == 0)
		return 0;

	u8 pkt[kPacketSize];
	bool have = false;

	if (kind_ == kToySkylanders)
	{
		const bool due = (u32)(now_ms - last_status_ms_) >= status_interval_ms;
		have = sky.next_packet(pkt, due);
		if (have && pkt[0] == 'S')
			last_status_ms_ = now_ms;
	}
	else if (kind_ == kToyDimensions)
	{
		have = dim.next_packet(pkt);
	}
	else if (kind_ == kToyInfinity)
	{
		have = inf.next_packet(pkt);
	}
	if (!have)
		return 0;

	u8 frame[kPacketSize];
	memset(frame, 0, sizeof(frame));
	if (wrap_offset_ > 0)
	{
		// <prefix> <length> <frame...>, mirroring how the game writes.
		u32 flen;
		if (kind_ != kToySkylanders)
		{
			flen = (u32)pkt[1] + 3; // type, length, payload, checksum
			if (flen > kPacketSize)
				flen = kPacketSize;
		}
		else
		{
			flen = sky_reply_len(pkt);
		}
		frame[0] = wrap_prefix_;
		frame[1] = (u8)flen;
		u32 copy = flen < kPacketSize - 2 ? flen : kPacketSize - 2;
		if (kind_ == kToySkylanders)
			copy = kPacketSize - 2; // keep every byte the portal produced
		memcpy(frame + 2, pkt, copy);
	}
	else
	{
		memcpy(frame, pkt, kPacketSize);
	}

	const u32 n = max_len < (u32)kPacketSize ? max_len : (u32)kPacketSize;
	memcpy(out, frame, n);
	return n;
}
