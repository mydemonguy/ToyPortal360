// ToyPortal360 - decides which toy device the game is talking to and
// routes the raw 32-byte packets from the hooked XAM calls to it.
#ifndef TOYPORTAL_TOY_ROUTER_H
#define TOYPORTAL_TOY_ROUTER_H

#include "skylander_portal.h"
#include "dimensions_toypad.h"
#include "infinity_base.h"

enum ToyKind
{
	kToyUnknown = 0,
	kToySkylanders = 1,
	kToyDimensions = 2,
	kToyInfinity = 3
};

class ToyRouter
{
public:
	ToyRouter();

	// forced: kToyUnknown = auto-detect from the first command the game sends.
	void reset(ToyKind forced);

	// Game -> device. Returns false if the packet was not understood.
	bool on_write(const u8* buf, u32 len);
	// Device -> game. Writes up to max_len bytes, returns bytes written (0 = nothing yet).
	u32 on_read(u8* out, u32 max_len, u32 now_ms);

	ToyKind kind() const { return kind_; }
	// Lets the menu pick a device before the game has sent anything.
	void set_kind_if_unknown(ToyKind k)
	{
		if (kind_ == kToyUnknown)
			kind_ = k;
	}

	// The 360 hands frames over as "<prefix> <length> <frame>" (seen as
	// 0B <len> 55 ... for the Dimensions toy pad). wrapped() says whether the
	// game uses that form; replies are wrapped the same way.
	bool wrapped() const { return wrap_offset_ > 0; }
	u8 wrap_prefix() const { return wrap_prefix_; }

	SkylanderPortal sky;
	DimensionsToypad dim;
	InfinityBase inf;

	// Milliseconds between idle Skylanders status packets (real portal: ~20 ms).
	u32 status_interval_ms;
	// Length byte for wrapped Skylanders replies. 0 = automatic: copy the
	// game's own length byte if it never changes, otherwise use the reply's
	// meaningful length. Dimensions and Infinity use the frame length.
	u32 sky_wrap_len;

private:
	bool detect(const u8* buf, u32 len);
	u8 sky_reply_len(const u8* pkt) const;

	ToyKind kind_;
	int wrap_offset_; // -1 = not seen yet, 0 = bare frames, 2 = wrapped
	u8 wrap_prefix_;
	int game_len_byte_; // -1 none yet, -2 varies, else the constant value
	u32 last_status_ms_;
};

const char* toy_kind_name(ToyKind k);

#endif
