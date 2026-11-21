// ToyPortal360 - emulated Disney Infinity Base.
// Protocol and figure crypto ported from RPCS3 (rpcs3/Emu/Io/Infinity.cpp and
// rpcs3qt/infinity_dialog.cpp, GPL-2.0).
#include "infinity_base.h"
#include "tiny_crypto.h"

// 16 bytes then "(c) Disney 2013". RPCS3 declares this as a 32-byte array with
// 31 values (so a hidden trailing zero) and hashes all but that last zero.
static const u8 SHA1_CONSTANT[31] = {
	0xAF, 0x62, 0xD2, 0xEC, 0x04, 0x91, 0x96, 0x8C, 0xC5, 0x2A, 0x1A, 0x71, 0x65, 0xF8, 0x65, 0xFE,
	0x28, 0x63, 0x29, 0x20, 0x44, 0x69, 0x73, 0x6e, 0x65, 0x79, 0x20, 0x32, 0x30, 0x31, 0x33};

static const u32 kScrambleMaskHi = 0x8E55AA1B;
static const u32 kScrambleMaskLo = 0x3999E8AA;

u8 infinity_group_for_slot(int slot)
{
	if (slot >= 0 && slot <= 2)
		return 1;
	if (slot >= 3 && slot <= 5)
		return 2;
	if (slot >= 6 && slot <= 8)
		return 3;
	return 0;
}

InfPieceKind infinity_piece_kind(u32 n)
{
	if (n > 0 && n < 0x1E847F)
		return kInfCharacter;
	if (n > 0x1E8480 && n < 0x2DC6BF)
		return kInfPlaySet;
	if (n > 0x2DC6C0 && n < 0x3D08FF)
		return kInfAbilityDisc;
	if (n > 0x3D0900 && n < 0x4C4B3F)
		return kInfHexDisc;
	return kInfUnknown;
}

// ---- figure files -----------------------------------------------------------

void InfinityBase::figure_key(const u8* uid7, u8* key)
{
	u8 buf[31 + 7], digest[20];
	memcpy(buf, SHA1_CONSTANT, 31);
	memcpy(buf + 31, uid7, 7);
	sha1(buf, sizeof(buf), digest);
	// Each 4-byte word of the digest, byte-reversed.
	for (int i = 0; i < 4; i++)
		for (int x = 0; x < 4; x++)
			key[x + i * 4] = digest[(3 - x) + i * 4];
}

u32 InfinityBase::figure_number_of(const u8* data)
{
	u8 key[16], plain[16];
	figure_key(data, key);
	Aes128 aes;
	aes128_init(&aes, key);
	aes128_decrypt(&aes, data + 16, plain);
	return ((u32)plain[1] << 16) | ((u32)plain[2] << 8) | plain[3];
}

bool InfinityBase::create_blank(u8* file, u32 number, u8 series, const u8* uid7)
{
	if (((number >> 16) & 0xFF) == 0)
		return false; // RPCS3 refuses these too

	memset(file, 0, kInfFigureSize);
	// NFC sector trailers with standard read/write permissions.
	const u32 first_block = 0x17878E, other_blocks = 0x778788;
	for (int i = 0; i < 3; i++)
		file[0x36 + i] = (u8)((first_block >> (2 - i) * 8) & 0xFF);
	for (int index = 1; index < 5; index++)
		for (int i = 0; i < 3; i++)
			file[index * 0x40 + 0x36 + i] = (u8)((other_blocks >> (2 - i) * 8) & 0xFF);

	// Block 0: 7-byte UID then fixed manufacturer bytes.
	u8 uid_block[16] = {0, 0, 0, 0, 0, 0, 0, 0x89, 0x44, 0x00, 0xC2, 0, 0, 0, 0, 0};
	memcpy(uid_block, uid7, 7);

	// Block 1 (encrypted): figure number, manufacture date, series, CRC.
	u8 fig[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, series, 0xD1, 0x1F, 0, 0, 0, 0};
	fig[1] = (u8)((number >> 16) & 0xFF);
	fig[2] = (u8)((number >> 8) & 0xFF);
	fig[3] = (u8)(number & 0xFF);
	if (series == 1)      { fig[4] = 0x0D; fig[5] = 0x08; fig[6] = 0x12; }
	else if (series == 2) { fig[4] = 0x0E; fig[5] = 0x09; fig[6] = 0x12; }
	else if (series == 3) { fig[4] = 0x0F; fig[5] = 0x08; fig[6] = 0x1C; }
	wr_be32(fig + 12, crc32_raw(0, fig, 12));

	u8 key[16], enc_fig[16], enc_blank[16];
	const u8 blank[16] = {0};
	figure_key(uid7, key);
	Aes128 aes;
	aes128_init(&aes, key);
	aes128_encrypt(&aes, fig, enc_fig);
	aes128_encrypt(&aes, blank, enc_blank);

	memcpy(file, uid_block, 16);
	memcpy(file + 16, enc_fig, 16);
	memcpy(file + 16 * 0x04, enc_blank, 16);
	memcpy(file + 16 * 0x08, enc_blank, 16);
	memcpy(file + 16 * 0x0C, enc_blank, 16);
	memcpy(file + 16 * 0x0D, enc_blank, 16);
	return true;
}

// ---- challenge / response ---------------------------------------------------

// 64-bit maths without relying on the compiler's 64-bit shifts: the scramble
// mask selects which of the 64 bits carry the 32-bit value.
static bool mask_bit(int i) // bit i counted from the most significant end
{
	return i < 32 ? ((kScrambleMaskHi >> (31 - i)) & 1) != 0 : ((kScrambleMaskLo >> (63 - i)) & 1) != 0;
}

u32 InfinityBase::descramble(const u8* be8)
{
	// RPCS3: walk the value from its least significant bit while the mask is
	// walked from its most significant bit.
	u32 ret = 0;
	for (int i = 0; i < 64; i++)
	{
		const int bit_from_lsb = i;
		const u8 byte = be8[7 - bit_from_lsb / 8];
		const u32 v = (byte >> (bit_from_lsb % 8)) & 1;
		if (mask_bit(i))
			ret = (ret << 1) | v;
	}
	return ret;
}

void InfinityBase::scramble(u32 value, u32 garbage, u8* be8)
{
	// RPCS3: build the result from its most significant bit while the mask is
	// walked from its least significant bit.
	memset(be8, 0, 8);
	for (int i = 0; i < 64; i++)
	{
		const bool take = mask_bit(63 - i);
		u32 bit;
		if (take)
		{
			bit = value & 1;
			value >>= 1;
		}
		else
		{
			bit = garbage & 1;
			garbage >>= 1;
		}
		if (bit)
			be8[i / 8] |= (u8)(0x80 >> (i % 8));
	}
}

void InfinityBase::seed(u32 s)
{
	ra_ = 0xF1EA5EED;
	rb_ = rc_ = rd_ = s;
	for (int i = 0; i < 23; i++)
		next();
}

u32 InfinityBase::next()
{
	u32 a = ra_, b = rb_, c = rc_;
	u32 ret = rotl32(rb_, 27);
	const u32 temp = a + ((ret ^ 0xFFFFFFFF) + 1);
	b ^= rotl32(c, 17);
	a = rd_;
	c += a;
	ret = b + temp;
	a += temp;
	rc_ = a;
	ra_ = b;
	rb_ = c;
	rd_ = ret;
	return ret;
}

// ---- base state -------------------------------------------------------------

InfinityBase::InfinityBase()
{
	reset();
}

void InfinityBase::reset()
{
	for (int i = 0; i < kInfSlots; i++)
	{
		memset(&figures_[i], 0, sizeof(InfFigure));
		figures_[i].order_added = kInfNoOrder;
	}
	changes_.clear();
	replies_.clear();
	ra_ = rb_ = rc_ = rd_ = 0;
	next_order_ = 0;
}

int InfinityBase::count() const
{
	int n = 0;
	for (int i = 0; i < kInfSlots; i++)
		if (figures_[i].present)
			n++;
	return n;
}

int InfinityBase::free_slot_for(InfPieceKind kind, int side) const
{
	static const int hex_slots[3] = {0, 1, 2};
	int candidates[3];
	int n = 0;
	switch (kind)
	{
	case kInfCharacter:
		candidates[n++] = side == 2 ? 6 : 3;
		break;
	case kInfAbilityDisc:
		candidates[n++] = side == 2 ? 7 : 4;
		candidates[n++] = side == 2 ? 8 : 5;
		break;
	case kInfPlaySet:
		candidates[n++] = 0;
		break;
	case kInfHexDisc:
		for (int i = 0; i < 3; i++)
			candidates[n++] = hex_slots[i];
		break;
	default:
		return -1;
	}
	for (int i = 0; i < n; i++)
		if (!figures_[candidates[i]].present)
			return candidates[i];
	return -1;
}

u8 InfinityBase::checksum(const u8* data, int n)
{
	u32 sum = 0;
	for (int i = 0; i < n; i++)
		sum += data[i];
	return (u8)(sum & 0xFF);
}

InfFigure& InfinityBase::by_order(u8 order)
{
	for (int i = 0; i < kInfSlots; i++)
		if (figures_[i].order_added == order)
			return figures_[i];
	return figures_[0]; // RPCS3 falls back to the first spot
}

void InfinityBase::blank_response(u8 sequence, u8* reply)
{
	reply[0] = 0xAA;
	reply[1] = 0x01;
	reply[2] = sequence;
	reply[3] = checksum(reply, 3);
}

void InfinityBase::present_figures(u8 sequence, u8* reply)
{
	int x = 3;
	for (int i = 0; i < kInfSlots; i++)
	{
		const u8 base = i == 0 ? 0x10 : (i < 4 ? 0x20 : 0x30);
		if (figures_[i].present)
		{
			reply[x] = (u8)(base + figures_[i].order_added);
			reply[x + 1] = 0x09;
			x += 2;
		}
	}
	reply[0] = 0xAA;
	reply[1] = (u8)(x - 2);
	reply[2] = sequence;
	reply[x] = checksum(reply, x);
}

// Reads and writes name a sector and a block within it, as on the figure's
// chip (4 blocks per sector): file block = sector * 4 + block. RPCS3 ignores
// the block byte (block 0 -> file block 1, others -> sector * 4), so on a
// console Disney Infinity 3.0's reads of sector 1-3 block 1 came back as block
// 0 again. Sector 0 with block 0 keeps RPCS3's meaning (file block 1, the
// figure id), since block 0 itself is the chip's serial number.
static u32 inf_file_block(u8 sector, u8 block)
{
	if (sector == 0 && block == 0)
		return 1;
	return sector * 4u + block;
}

void InfinityBase::query_block(u8 order, u8 sector, u8 block, u8* reply, u8 sequence)
{
	const InfFigure& f = by_order(order);
	reply[0] = 0xAA;
	reply[1] = 0x12;
	reply[2] = sequence;
	reply[3] = 0x00;
	const u32 file_block = inf_file_block(sector, block);
	if (f.present && file_block < 20)
		memcpy(reply + 4, f.data + 16 * file_block, 16);
	reply[20] = checksum(reply, 20);
}

void InfinityBase::write_block(u8 order, u8 sector, u8 block, const u8* to_write, u8* reply, u8 sequence)
{
	InfFigure& f = by_order(order);
	reply[0] = 0xAA;
	reply[1] = 0x02;
	reply[2] = sequence;
	reply[3] = 0x00;
	const u32 file_block = inf_file_block(sector, block);
	if (f.present && file_block < 20 && (file_block & 3) != 3) // never a sector trailer
	{
		memcpy(f.data + file_block * 16, to_write, 16);
		f.dirty = true;
	}
	reply[4] = checksum(reply, 4);
}

void InfinityBase::figure_identifier(u8 order, u8 sequence, u8* reply)
{
	const InfFigure& f = by_order(order);
	reply[0] = 0xAA;
	reply[1] = 0x09;
	reply[2] = sequence;
	reply[3] = 0x00;
	if (f.present)
		memcpy(reply + 4, f.data, 7);
	reply[11] = checksum(reply, 11);
}

void InfinityBase::handle_command(const u8* buf, u32 len)
{
	if (len < 4)
		return;
	const u8 command = buf[2];
	const u8 sequence = buf[3];
	u8 q[kPacketSize];
	memset(q, 0, sizeof(q));

	switch (command)
	{
	case 0x80: // Activate: fixed base info
	{
		static const u8 info[24] = {0xAA, 0x15, 0x00, 0x00, 0x0F, 0x01, 0x00, 0x03, 0x02, 0x09, 0x09, 0x43,
			0x20, 0x32, 0x62, 0x36, 0x36, 0x4B, 0x34, 0x99, 0x67, 0x31, 0x93, 0x8C};
		memcpy(q, info, sizeof(info));
		break;
	}
	case 0x81: // Seed the RNG from a scrambled value
		seed(descramble(buf + 4));
		blank_response(sequence, q);
		break;
	case 0x83: // Challenge: next random number, scrambled
	{
		u8 scr[8];
		scramble(next(), 0, scr);
		q[0] = 0xAA;
		q[1] = 0x09;
		q[2] = sequence;
		memcpy(q + 3, scr, 8);
		q[11] = checksum(q, 11);
		break;
	}
	case 0x90: case 0x92: case 0x93: case 0x95: case 0x96: // lights
	case 0xB5:                                              // status
		blank_response(sequence, q);
		break;
	case 0xA1: // Which figures are present
		present_figures(sequence, q);
		break;
	case 0xA2: // Read a block
		query_block(buf[4], buf[5], buf[6], q, sequence);
		break;
	case 0xA3: // Write a block
		write_block(buf[4], buf[5], buf[6], buf + 7, q, sequence);
		break;
	case 0xB4: // Figure UID
		figure_identifier(buf[4], sequence, q);
		break;
	default:
		break; // RPCS3 answers unknown commands with an empty packet too
	}
	replies_.push(q);
}

bool InfinityBase::next_packet(u8* out)
{
	if (changes_.pop(out))
		return true;
	return replies_.pop(out);
}

void InfinityBase::push_change(int slot, bool removed)
{
	u8 r[kPacketSize];
	memset(r, 0, sizeof(r));
	r[0] = 0xAB;
	r[1] = 0x04;
	r[2] = infinity_group_for_slot(slot);
	r[3] = 0x09;
	r[4] = figures_[slot].order_added;
	r[5] = removed ? 0x01 : 0x00;
	r[6] = checksum(r, 6);
	changes_.push(r);
}

u32 InfinityBase::load(const u8* data, const char* path, int slot)
{
	if (slot < 0 || slot >= kInfSlots)
		return 0;
	if (figures_[slot].present)
		remove(slot);
	InfFigure& f = figures_[slot];
	memcpy(f.data, data, kInfFigureSize);
	f.present = true;
	f.dirty = false;
	strncpy(f.path, path ? path : "", sizeof(f.path) - 1);
	f.path[sizeof(f.path) - 1] = 0;
	if (f.order_added == kInfNoOrder) // a spot keeps its order number, as in RPCS3
		f.order_added = next_order_++;
	push_change(slot, false);
	return figure_number_of(data);
}

bool InfinityBase::remove(int slot)
{
	if (slot < 0 || slot >= kInfSlots || !figures_[slot].present)
		return false;
	figures_[slot].present = false;
	push_change(slot, true);
	return true;
}
