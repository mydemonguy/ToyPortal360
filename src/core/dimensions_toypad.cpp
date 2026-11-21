// ToyPortal360 - emulated LEGO Dimensions Toy Pad.
// Protocol and tag crypto ported from RPCS3 (rpcs3/Emu/Io/Dimensions.cpp, GPL-2.0).
#include "dimensions_toypad.h"

static const u8 COMMAND_KEY[16] = {0x55, 0xFE, 0xF6, 0xB0, 0x62, 0xBF, 0x0B, 0x41,
	0xC9, 0xB3, 0x7C, 0xB4, 0x97, 0x3E, 0x29, 0x7B};

static const u8 CHAR_CONSTANT[17] = {0xB7, 0xD5, 0xD7, 0xE6, 0xE7, 0xBA, 0x3C,
	0xA8, 0xD8, 0x75, 0x47, 0x68, 0xCF, 0x23, 0xE9, 0xFE, 0xAA};

static const u8 PWD_CONSTANT[25] = {0x28, 0x63, 0x29, 0x20, 0x43, 0x6F, 0x70, 0x79,
	0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x4C, 0x45, 0x47, 0x4F, 0x20, 0x32, 0x30, 0x31, 0x34, 0xAA, 0xAA};

u8 dimensions_pad_for_slot(int slot)
{
	if (slot == 1)
		return kPadCenter;
	if (slot == 0 || slot == 3 || slot == 4)
		return kPadLeft;
	return kPadRight;
}

// ---- tag crypto (TEA, little-endian words) ---------------------------------

void DimensionsToypad::encrypt(const u8* in, const u8* key, u8* out)
{
	const u8* k = key ? key : COMMAND_KEY;
	u32 d1 = rd_le32(in), d2 = rd_le32(in + 4);
	const u32 k1 = rd_le32(k), k2 = rd_le32(k + 4), k3 = rd_le32(k + 8), k4 = rd_le32(k + 12);
	u32 sum = 0;
	const u32 delta = 0x9E3779B9;
	for (int i = 0; i < 32; i++)
	{
		sum += delta;
		d1 += (((d2 << 4) + k1) ^ (d2 + sum) ^ ((d2 >> 5) + k2));
		d2 += (((d1 << 4) + k3) ^ (d1 + sum) ^ ((d1 >> 5) + k4));
	}
	wr_le32(out, d1);
	wr_le32(out + 4, d2);
}

void DimensionsToypad::decrypt(const u8* in, const u8* key, u8* out)
{
	const u8* k = key ? key : COMMAND_KEY;
	u32 d1 = rd_le32(in), d2 = rd_le32(in + 4);
	const u32 k1 = rd_le32(k), k2 = rd_le32(k + 4), k3 = rd_le32(k + 8), k4 = rd_le32(k + 12);
	u32 sum = 0xC6EF3720;
	const u32 delta = 0x9E3779B9;
	for (int i = 0; i < 32; i++)
	{
		d2 -= (((d1 << 4) + k3) ^ (d1 + sum) ^ ((d1 >> 5) + k4));
		d1 -= (((d2 << 4) + k1) ^ (d2 + sum) ^ ((d2 >> 5) + k2));
		sum -= delta;
	}
	wr_le32(out, d1);
	wr_le32(out + 4, d2);
}

// Scrambles `count` little-endian words of `key`.
static u32 dim_randomize(const u8* key, u8 count)
{
	u32 scrambled = 0;
	for (u8 i = 0; i < count; i++)
	{
		const u32 v4 = rotr32(scrambled, 25);
		const u32 v5 = rotr32(scrambled, 10);
		const u32 b = rd_le32(key + i * 4);
		scrambled = b + v4 + v5 - scrambled;
	}
	return scrambled;
}

static u32 dim_scramble(const u8* uid7, u8 count)
{
	u8 buf[7 + 17];
	memcpy(buf, uid7, 7);
	memcpy(buf + 7, CHAR_CONSTANT, 17);
	buf[count * 4 - 1] = 0xAA;
	// RPCS3 reads the scrambled word as LE bytes then re-reads them big-endian.
	u8 tmp[4];
	wr_le32(tmp, dim_randomize(buf, count));
	return rd_be32(tmp);
}

static void uid_of(const u8* buf, u8* uid7)
{
	uid7[0] = buf[0];
	uid7[1] = buf[1];
	uid7[2] = buf[2];
	uid7[3] = buf[4];
	uid7[4] = buf[5];
	uid7[5] = buf[6];
	uid7[6] = buf[7];
}

void DimensionsToypad::figure_key(const u8* buf, u8* key)
{
	u8 uid[7];
	uid_of(buf, uid);
	wr_be32(key + 0, dim_scramble(uid, 3));
	wr_be32(key + 4, dim_scramble(uid, 4));
	wr_be32(key + 8, dim_scramble(uid, 5));
	wr_be32(key + 12, dim_scramble(uid, 6));
}

void DimensionsToypad::pwd_generate(const u8* uid7, u8* out4)
{
	u8 buf[7 + 25];
	memcpy(buf, uid7, 7);
	memcpy(buf + 7, PWD_CONSTANT, 25);
	wr_le32(out4, dim_randomize(buf, 8));
}

// Page 38 says what a tag is: 00 00 00 00 a character, 00 01 00 00 a vehicle
// or gadget (a blank tag is a vehicle tag with id 0).
static bool is_vehicle_tag(const u8* buf)
{
	const u8* p = buf + 38 * 4;
	return p[0] == 0 && p[1] == 1 && p[2] == 0 && p[3] == 0;
}

u32 DimensionsToypad::figure_id_of(const u8* buf)
{
	if (is_vehicle_tag(buf))
		return rd_le16(buf + 36 * 4); // vehicles/gadgets: id stored plain in page 36
	u8 key[16], dec[8];
	figure_key(buf, key);
	decrypt(buf + 36 * 4, key, dec);
	return rd_le32(dec); // characters: id encrypted in page 36
}

bool DimensionsToypad::repair_vehicle(u8* buf)
{
	// Toy Portal 360 up to the grid builds made vehicles and gadgets like
	// characters: id encrypted in page 36, page 38 empty. The game doesn't
	// take those as vehicles. Rewrite them the way real vehicle tags are.
	if (is_vehicle_tag(buf))
		return false;
	u8 key[16], dec[8];
	figure_key(buf, key);
	decrypt(buf + 36 * 4, key, dec);
	const u32 id = rd_le32(dec);
	if (id < 1000 || id > 0xFFFF || rd_le32(dec + 4) != id)
		return false;
	memset(buf + 36 * 4, 0, 8);
	wr_le16(buf + 36 * 4, (u16)id);
	memset(buf + 38 * 4, 0, 4);
	buf[38 * 4 + 1] = 1;
	return true;
}

void DimensionsToypad::create_blank(u8* buf, u16 id, const u8* uid7)
{
	memset(buf, 0, kDimFigureSize);
	// NTAG213 UID lives in bytes 0,1,2,4,5,6,7. Byte 0 is the NXP manufacturer code.
	if (uid7)
	{
		buf[0] = uid7[0]; buf[1] = uid7[1]; buf[2] = uid7[2];
		buf[4] = uid7[3]; buf[5] = uid7[4]; buf[6] = uid7[5]; buf[7] = uid7[6];
	}
	buf[0] = 0x04;
	buf[7] = 0x80;
	buf[3] = (u8)(id & 0xFF);

	if (id >= 1000)
	{
		// Vehicles and gadgets: id plain in page 36, page 38 marks a vehicle.
		u8 uid[7];
		uid_of(buf, uid);
		wr_le16(buf + 36 * 4, id);
		wr_be16(buf + 38 * 4, 1);
		pwd_generate(uid, buf + 43 * 4);
	}
	else if (id != 0)
	{
		u8 uid[7], key[16], plain[8], enc[8];
		uid_of(buf, uid);
		figure_key(buf, key);
		wr_le32(plain, id);
		wr_le32(plain + 4, id);
		encrypt(plain, key, enc);
		memcpy(buf + 36 * 4, enc, 4);
		memcpy(buf + 37 * 4, enc + 4, 4);
		pwd_generate(uid, buf + 43 * 4);
	}
	else
	{
		wr_be16(buf + 38 * 4, 1); // page 38 marks a blank tag
	}
}

// ---- toy pad state ----------------------------------------------------------

DimensionsToypad::DimensionsToypad()
{
	reset();
}

void DimensionsToypad::reset()
{
	for (int i = 0; i < kDimSlots; i++)
	{
		memset(&figures_[i], 0, sizeof(DimFigure));
		figures_[i].index = kDimNoIndex;
		figures_[i].pad = kDimNoIndex;
	}
	changes_.clear();
	replies_.clear();
	ra_ = rb_ = rc_ = rd_ = 0;
	memset(lights_, 0, sizeof(lights_));
	light_changes_ = 0;
}

int DimensionsToypad::count() const
{
	int n = 0;
	for (int i = 0; i < kDimSlots; i++)
		if (figures_[i].index != kDimNoIndex)
			n++;
	return n;
}

int DimensionsToypad::first_free_on_pad(u8 pad) const
{
	for (int i = 0; i < kDimSlots; i++)
		if (dimensions_pad_for_slot(i) == pad && figures_[i].index == kDimNoIndex)
			return i;
	return -1;
}

u8 DimensionsToypad::checksum(const u8* data, u32 n)
{
	u32 sum = 0;
	for (u32 i = 0; i < n; i++)
		sum += data[i];
	return (u8)(sum & 0xFF);
}

void DimensionsToypad::init_rng(u32 seed)
{
	ra_ = 0xF1EA5EED;
	rb_ = rc_ = rd_ = seed;
	for (int i = 0; i < 42; i++)
		next_rng();
}

u32 DimensionsToypad::next_rng()
{
	const u32 e = ra_ - rotl32(rb_, 21);
	ra_ = rb_ ^ rotl32(rc_, 19);
	rb_ = rc_ + rotl32(rd_, 6);
	rc_ = rd_ + e;
	rd_ = e + ra_;
	return rd_;
}

DimFigure* DimensionsToypad::by_game_index(u8 index)
{
	// The game's index starts at 1.
	const u8 i = (u8)(index - 1);
	if (i >= kDimSlots)
		return NULL;
	return &figures_[i];
}

void DimensionsToypad::blank_response(u8 type, u8 sequence, u8* reply)
{
	reply[0] = 0x55;
	reply[1] = type;
	reply[2] = sequence;
	reply[3] = checksum(reply, 3);
}

void DimensionsToypad::seed_rng(const u8* payload, u8 sequence, u8* reply)
{
	u8 value[8], plain[8], enc[8];
	decrypt(payload, NULL, value);
	const u32 seed = rd_le32(value);
	const u32 conf = rd_be32(value + 4);
	init_rng(seed);
	memset(plain, 0, 8);
	wr_be32(plain, conf);
	encrypt(plain, NULL, enc);
	reply[0] = 0x55;
	reply[1] = 0x09;
	reply[2] = sequence;
	memcpy(reply + 3, enc, 8);
	reply[11] = checksum(reply, 11);
}

void DimensionsToypad::challenge(const u8* payload, u8 sequence, u8* reply)
{
	u8 value[8], plain[8], enc[8];
	decrypt(payload, NULL, value);
	const u32 conf = rd_be32(value);
	wr_le32(plain, next_rng());
	wr_be32(plain + 4, conf);
	encrypt(plain, NULL, enc);
	reply[0] = 0x55;
	reply[1] = 0x09;
	reply[2] = sequence;
	memcpy(reply + 3, enc, 8);
	reply[11] = checksum(reply, 11);
}

void DimensionsToypad::query_block(u8 index, u8 page, u8* reply, u8 sequence)
{
	reply[0] = 0x55;
	reply[1] = 0x12;
	reply[2] = sequence;
	reply[3] = 0x00;
	DimFigure* f = by_game_index(index);
	if (f && f->index != kDimNoIndex && (4u * page) < (kDimFigureSize - 16u))
		memcpy(reply + 4, f->data + 4 * page, 16);
	reply[20] = checksum(reply, 20);
}

void DimensionsToypad::write_block(u8 index, u8 page, const u8* to_write, u8* reply, u8 sequence)
{
	reply[0] = 0x55;
	reply[1] = 0x02;
	reply[2] = sequence;
	reply[3] = 0x00;
	DimFigure* f = by_game_index(index);
	if (f && f->index != kDimNoIndex && page < 0x2D)
	{
		if (page == 36)
			f->id = rd_le32(to_write);
		memcpy(f->data + page * 4, to_write, 4);
		f->dirty = true;
	}
	reply[4] = checksum(reply, 4);
}

void DimensionsToypad::get_model(const u8* payload, u8 sequence, u8* reply)
{
	u8 value[8], plain[8], enc[8];
	decrypt(payload, NULL, value);
	const u8 index = value[0];
	const u32 conf = rd_be32(value + 4);
	memset(plain, 0, 8);
	DimFigure* f = by_game_index(index);
	if (f)
		wr_le32(plain, f->id);
	wr_be32(plain + 4, conf);
	encrypt(plain, NULL, enc);
	reply[0] = 0x55;
	reply[1] = 0x0A;
	reply[2] = sequence;
	reply[3] = 0x00;
	memcpy(reply + 4, enc, 8);
	reply[12] = checksum(reply, 12);
}

void DimensionsToypad::handle_command(const u8* buf, u32 len)
{
	if (len < 4)
		return;
	const u8 command = buf[2];
	const u8 sequence = buf[3];
	u8 q[kPacketSize];
	memset(q, 0, sizeof(q));

	switch (command)
	{
	case 0xB0: // Wake: fixed "(c) LEGO 2014" banner
	{
		static const u8 wake[17] = {0x55, 0x0E, 0x01, 0x28, 0x63, 0x29, 0x20, 0x4C, 0x45,
			0x47, 0x4F, 0x20, 0x32, 0x30, 0x31, 0x34, 0x46};
		memcpy(q, wake, sizeof(wake));
		break;
	}
	case 0xB1: // Seed
		seed_rng(buf + 4, sequence, q);
		break;
	case 0xB3: // Challenge
		challenge(buf + 4, sequence, q);
		break;
	case 0xC0: case 0xC1: case 0xC2: case 0xC3: // pad lights
	case 0xC4: case 0xC6: case 0xC7: case 0xC8:
	{
		// Payloads as the community Toy Pad notes give them (pad 0 = all,
		// 1 centre, 2 left, 3 right; the "all" forms list centre, left, right):
		//   C0 pad r g b              C2 pad speed count r g b
		//   C3 pad on off count r g b C6 3 x (on speed count r g b)
		//   C7 3 x (on on_t off_t count r g b)   C8 3 x (on r g b)
		const u8* p = buf + 4;
		const u32 have = len > 4 ? len - 4 : 0;
		if (command == 0xC0 && have >= 4)
			set_light(p[0], p + 1);
		else if (command == 0xC2 && have >= 6)
			set_light(p[0], p + 3);
		else if (command == 0xC3 && have >= 7)
			set_light(p[0], p + 4);
		else if ((command == 0xC6 && have >= 18) || (command == 0xC7 && have >= 21) || (command == 0xC8 && have >= 12))
		{
			const u32 step = command == 0xC6 ? 6 : command == 0xC7 ? 7 : 4;
			for (u8 i = 0; i < 3; i++)
				if (p[i * step])
					set_light((u8)(i + 1), p + i * step + step - 3);
		}
		blank_response(0x01, sequence, q);
		break;
	}
	case 0xD2: // Read 4 pages
		query_block(buf[4], buf[5], q, sequence);
		break;
	case 0xD3: // Write 1 page
		write_block(buf[4], buf[5], buf + 6, q, sequence);
		break;
	case 0xD4: // Model id
		get_model(buf + 4, sequence, q);
		break;
	default:
		// 0xD0 tag list, 0xE1 pwd, 0xE5 active, 0xFF LED query: RPCS3 leaves
		// these unimplemented too and answers with an empty packet.
		break;
	}
	replies_.push(q);
}

bool DimensionsToypad::next_packet(u8* out)
{
	// Figure placed/removed events take priority, as on RPCS3.
	if (changes_.pop(out))
		return true;
	return replies_.pop(out);
}

void DimensionsToypad::push_change(const DimFigure& f, u8 pad, bool removed)
{
	u8 r[kPacketSize];
	memset(r, 0, sizeof(r));
	r[0] = 0x56;
	r[1] = 0x0B;
	r[2] = pad;
	r[3] = 0x00;
	r[4] = f.index;
	r[5] = removed ? 0x01 : 0x00;
	r[6] = f.data[0]; r[7] = f.data[1]; r[8] = f.data[2];
	r[9] = f.data[4]; r[10] = f.data[5]; r[11] = f.data[6]; r[12] = f.data[7];
	r[13] = checksum(r, 13);
	changes_.push(r);
}

u32 DimensionsToypad::load(const u8* data, const char* path, int slot)
{
	if (slot < 0 || slot >= kDimSlots)
		return 0;
	if (figures_[slot].index != kDimNoIndex)
		remove(slot);
	DimFigure& f = figures_[slot];
	memcpy(f.data, data, kDimFigureSize);
	// A vehicle file made by an older Toy Portal 360 is fixed here and saved
	// back (dirty) the next time figures are saved.
	f.dirty = repair_vehicle(f.data);
	f.id = figure_id_of(f.data);
	f.pad = dimensions_pad_for_slot(slot);
	f.index = (u8)(slot + 1);
	strncpy(f.path, path ? path : "", sizeof(f.path) - 1);
	f.path[sizeof(f.path) - 1] = 0;
	push_change(f, f.pad, false);
	return f.id;
}

bool DimensionsToypad::remove(int slot)
{
	if (slot < 0 || slot >= kDimSlots)
		return false;
	DimFigure& f = figures_[slot];
	if (f.index == kDimNoIndex)
		return false;
	push_change(f, f.pad, true);
	f.index = kDimNoIndex;
	f.pad = kDimNoIndex;
	f.id = 0;
	return true;
}

void DimensionsToypad::set_light(u8 pad, const u8* rgb)
{
	for (u8 i = 1; i <= 3; i++)
		if (pad == 0 || pad == i)
		{
			if (memcmp(lights_[i - 1], rgb, 3) != 0)
				light_changes_++;
			memcpy(lights_[i - 1], rgb, 3);
		}
}

void DimensionsToypad::light(u8 pad, u8* rgb) const
{
	if (pad >= 1 && pad <= 3)
		memcpy(rgb, lights_[pad - 1], 3);
	else
		memset(rgb, 0, 3);
}

bool DimensionsToypad::move(int from, int to)
{
	if (from < 0 || from >= kDimSlots || to < 0 || to >= kDimSlots)
		return false;
	if (figures_[from].index == kDimNoIndex)
		return false;
	if (from == to)
	{
		// Lift and put back down on the same spot.
		push_change(figures_[from], figures_[from].pad, true);
		push_change(figures_[from], figures_[from].pad, false);
		return true;
	}
	if (figures_[to].index != kDimNoIndex)
		remove(to);

	DimFigure moving = figures_[from];
	remove(from);
	figures_[from].dirty = false; // unsaved changes travel with the figure
	load(moving.data, moving.path, to);
	figures_[to].dirty = moving.dirty;
	return true;
}
