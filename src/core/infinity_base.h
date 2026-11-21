// ToyPortal360 - emulated Disney Infinity Base.
// Protocol and figure crypto ported from RPCS3 (rpcs3/Emu/Io/Infinity.cpp and
// rpcs3qt/infinity_dialog.cpp, GPL-2.0).
#ifndef TOYPORTAL_INFINITY_BASE_H
#define TOYPORTAL_INFINITY_BASE_H

#include "bytes.h"

enum
{
	kInfFigureSize = 0x14 * 0x10, // 320 bytes: 20 blocks of 16
	kInfSlots = 9,
	kInfNoOrder = 255
};

// The nine spots, as RPCS3 numbers them:
//   0 = play set / power disc (hexagon), 1-2 = more power discs,
//   3 = player 1, 4-5 = player 1 abilities, 6 = player 2, 7-8 = player 2 abilities.
// The base reports them in three groups: 1 = hexagon, 2 = player 1, 3 = player 2.
u8 infinity_group_for_slot(int slot);

// What a figure number is, from the number ranges RPCS3's creator uses.
enum InfPieceKind
{
	kInfCharacter,    // goes on a player spot
	kInfPlaySet,      // play set / toy box piece: hexagon spot 0
	kInfAbilityDisc,  // round power disc: ability spots
	kInfHexDisc,      // hexagonal power disc: hexagon spots
	kInfUnknown
};
InfPieceKind infinity_piece_kind(u32 number);

struct InfFigure
{
	u8 data[kInfFigureSize];
	bool present;
	u8 order_added;
	bool dirty;
	char path[260];
};

class InfinityBase
{
public:
	InfinityBase();

	void reset();

	void handle_command(const u8* buf, u32 len);
	bool next_packet(u8* out);

	// Menu side.
	u32 load(const u8* data_320, const char* path, int slot); // returns figure number
	bool remove(int slot);
	bool occupied(int slot) const { return figures_[slot].present; }
	InfFigure& figure(int slot) { return figures_[slot]; }
	int count() const;
	// Free spot for a piece (side: 1 = player 1, 2 = player 2), or -1.
	int free_slot_for(InfPieceKind kind, int side) const;

	// Figure file helpers (also used by the creator and the self-tests).
	static void figure_key(const u8* uid7, u8* key16);
	static u32 figure_number_of(const u8* data_320);
	static bool create_blank(u8* data_320, u32 number, u8 series, const u8* uid7);

	// Exposed for tests.
	static u32 descramble(const u8* be8);
	static void scramble(u32 value, u32 garbage, u8* be8);
	void seed(u32 s);
	u32 next();

private:
	static u8 checksum(const u8* data, int n);
	void blank_response(u8 sequence, u8* reply);
	void present_figures(u8 sequence, u8* reply);
	void query_block(u8 order, u8 sector, u8 block, u8* reply, u8 sequence);
	void write_block(u8 order, u8 sector, u8 block, const u8* to_write, u8* reply, u8 sequence);
	void figure_identifier(u8 order, u8 sequence, u8* reply);
	InfFigure& by_order(u8 order);
	void push_change(int slot, bool removed);

	InfFigure figures_[kInfSlots];
	PacketQueue<16> changes_;
	PacketQueue<16> replies_;
	u32 ra_, rb_, rc_, rd_;
	u8 next_order_;
};

#endif
