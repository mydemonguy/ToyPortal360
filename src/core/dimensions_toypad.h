// ToyPortal360 - emulated LEGO Dimensions Toy Pad.
// Protocol and tag crypto ported from RPCS3 (rpcs3/Emu/Io/Dimensions.cpp, GPL-2.0).
#ifndef TOYPORTAL_DIMENSIONS_TOYPAD_H
#define TOYPORTAL_DIMENSIONS_TOYPAD_H

#include "bytes.h"

enum
{
	kDimFigureSize = 0x2D * 0x04, // NTAG213: 45 pages of 4 bytes
	kDimSlots = 7,
	kDimNoIndex = 255
};

// Pads, as the game numbers them.
enum
{
	kPadCenter = 1,
	kPadLeft = 2,
	kPadRight = 3
};

// Seven spots: index 0 = left top, 1 = centre, 2 = right top,
// 3/4 = left bottom pair, 5/6 = right bottom pair (same layout as RPCS3).
u8 dimensions_pad_for_slot(int slot);

struct DimFigure
{
	u8 data[kDimFigureSize];
	u32 id;
	u8 index; // 1-based index the game sees, or kDimNoIndex if the spot is empty
	u8 pad;
	bool dirty;
	char path[260];
};

class DimensionsToypad
{
public:
	DimensionsToypad();

	void reset();

	void handle_command(const u8* buf, u32 len);
	bool next_packet(u8* out);

	// Menu side.
	u32 load(const u8* data_180, const char* path, int slot); // returns figure id
	bool remove(int slot);
	bool move(int from_slot, int to_slot);
	bool occupied(int slot) const { return figures_[slot].index != kDimNoIndex; }
	DimFigure& figure(int slot) { return figures_[slot]; }
	int count() const;
	int first_free_on_pad(u8 pad) const;

	// Tag helpers (also used by the creator and the self-tests).
	static void create_blank(u8* buf_180, u16 id, const u8* uid7_or_null);
	static u32 figure_id_of(const u8* buf_180);
	static void encrypt(const u8* in8, const u8* key16_or_null, u8* out8);
	static void decrypt(const u8* in8, const u8* key16_or_null, u8* out8);
	static void figure_key(const u8* buf_180, u8* key16);
	static void pwd_generate(const u8* uid7, u8* out4);

private:
	static u8 checksum(const u8* data, u32 n);
	void blank_response(u8 type, u8 sequence, u8* reply);
	void seed_rng(const u8* payload, u8 sequence, u8* reply);
	void challenge(const u8* payload, u8 sequence, u8* reply);
	void query_block(u8 index, u8 page, u8* reply, u8 sequence);
	void write_block(u8 index, u8 page, const u8* to_write, u8* reply, u8 sequence);
	void get_model(const u8* payload, u8 sequence, u8* reply);
	void push_change(const DimFigure& f, u8 pad, bool removed);
	void init_rng(u32 seed);
	u32 next_rng();
	DimFigure* by_game_index(u8 index);

	DimFigure figures_[kDimSlots];
	PacketQueue<16> changes_;
	PacketQueue<16> replies_;
	u32 ra_, rb_, rc_, rd_;
};

#endif
