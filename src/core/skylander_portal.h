// ToyPortal360 - emulated Skylanders Portal of Power.
// Protocol logic ported from RPCS3 (rpcs3/Emu/Io/Skylander.cpp, GPL-2.0).
#ifndef TOYPORTAL_SKYLANDER_PORTAL_H
#define TOYPORTAL_SKYLANDER_PORTAL_H

#include "bytes.h"

enum { kSkyFigureSize = 0x40 * 0x10, kSkySlots = 8 };

u16 skylander_crc16(u16 init_value, const u8* buffer, u32 size);

// Builds a fresh, never-played figure image (same layout RPCS3's creator writes).
void skylander_create_blank(u8* out_1024, u16 id, u16 variant);

struct SkyFigure
{
	u8 status;             // bit0 = on portal; 2/3 = transient removed/added
	u8 queued[4];          // pending status transitions reported via 'S' packets
	int queued_count;
	u8 data[kSkyFigureSize];
	u32 last_id;
	bool dirty;            // game wrote a block; platform layer should save
	char path[260];        // where the figure came from (owned by platform layer)
};

class SkylanderPortal
{
public:
	SkylanderPortal();

	void reset();

	// Game -> portal (one 32-byte command). Replies are queued internally.
	void handle_command(const u8* buf, u32 len);
	// Portal -> game. Returns true and fills 32 bytes if a packet is ready.
	// status_due: the caller decides when an idle 'S' status packet is due.
	bool next_packet(u8* out, bool status_due);

	// Menu side.
	int load(const u8* data_1024, const char* path); // returns slot or -1 if full
	bool remove(int slot);
	bool occupied(int slot) const { return (figures_[slot].status & 1) != 0; }
	u16 figure_id(int slot) const { return rd_le16(figures_[slot].data + 0x10); }
	u16 figure_variant(int slot) const { return rd_le16(figures_[slot].data + 0x1C); }
	SkyFigure& figure(int slot) { return figures_[slot]; }
	int count() const;

	u8 led_r, led_g, led_b;

private:
	void activate();
	void deactivate();
	void get_status(u8* reply);
	void push_status(SkyFigure& f, u8 s);

	SkyFigure figures_[kSkySlots];
	PacketQueue<16> replies_;
	bool activated_;
	u8 interrupt_counter_;
};

#endif
