// Host tests for the portable core. Build for x86 and for big-endian PowerPC
// (see tests/run_tests.sh). With --vectors, prints crypto test vectors for
// tests/rpcs3_reference.py to check against RPCS3's maths.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "../src/core/toy_router.h"
#include "../src/core/figure_db.h"
#include "../src/core/tiny_crypto.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static u32 rng_state = 12345;
static u8 rnd8() { rng_state = rng_state * 1103515245u + 12345u; return (u8)(rng_state >> 16); }
static void rnd(u8* p, int n) { for (int i = 0; i < n; i++) p[i] = rnd8(); }
static void hex(const u8* p, int n) { for (int i = 0; i < n; i++) printf("%02x", p[i]); }

static void print_vectors()
{
	for (int t = 0; t < 200; t++)
	{
		u8 in[8], key[16], out[8], buf[kDimFigureSize], k[16], uid[7], pwd[4];
		rnd(in, 8); rnd(key, 16);
		DimensionsToypad::encrypt(in, key, out);
		printf("enc "); hex(in, 8); printf(" "); hex(key, 16); printf(" "); hex(out, 8); printf("\n");
		DimensionsToypad::encrypt(in, NULL, out);
		printf("encc "); hex(in, 8); printf(" "); hex(out, 8); printf("\n");
		DimensionsToypad::decrypt(in, key, out);
		printf("dec "); hex(in, 8); printf(" "); hex(key, 16); printf(" "); hex(out, 8); printf("\n");
		rnd(buf, kDimFigureSize);
		DimensionsToypad::figure_key(buf, k);
		printf("key "); hex(buf, kDimFigureSize); printf(" "); hex(k, 16); printf("\n");
		rnd(uid, 7);
		DimensionsToypad::pwd_generate(uid, pwd);
		printf("pwd "); hex(uid, 7); printf(" "); hex(pwd, 4); printf("\n");
		const u16 id = (u16)(t == 0 ? 0 : (rnd8() | (rnd8() << 8)) % 1400);
		DimensionsToypad::create_blank(buf, id, uid);
		u8 uid8[8] = {0, uid[0], uid[1], uid[2], 0, uid[3], uid[4], uid[5]};
		uid8[0] = uid[0]; uid8[1] = uid[1]; uid8[2] = uid[2]; uid8[3] = 0;
		uid8[4] = uid[3]; uid8[5] = uid[4]; uid8[6] = uid[5]; uid8[7] = uid[6];
		printf("dimblank "); hex(uid8, 8); printf(" %u ", id); hex(buf, kDimFigureSize); printf("\n");
		u8 sky[kSkyFigureSize];
		const u16 sid = (u16)(rnd8() | (rnd8() << 8)), svar = (u16)(rnd8() | (rnd8() << 8));
		skylander_create_blank(sky, sid, svar);
		printf("skyblank %u %u ", sid, svar); hex(sky, kSkyFigureSize); printf("\n");

		// Disney Infinity
		u8 msg[200], digest[20], blk[16], akey[16], aout[16];
		const int mlen = t % 140; // crosses the 55/56/64-byte padding edges
		rnd(msg, mlen);
		sha1(msg, (u32)mlen, digest);
		printf("sha1 "); if (mlen) hex(msg, mlen); else printf("-"); printf(" "); hex(digest, 20); printf("\n");
		rnd(blk, 16); rnd(akey, 16);
		Aes128 aes;
		aes128_init(&aes, akey);
		aes128_encrypt(&aes, blk, aout);
		printf("aesenc "); hex(blk, 16); printf(" "); hex(akey, 16); printf(" "); hex(aout, 16); printf("\n");
		aes128_decrypt(&aes, blk, aout);
		printf("aesdec "); hex(blk, 16); printf(" "); hex(akey, 16); printf(" "); hex(aout, 16); printf("\n");
		u8 c4[4];
		wr_be32(c4, crc32_raw(0, msg, 12));
		printf("crc "); hex(msg, 12); printf(" "); hex(c4, 4); printf("\n");
		u8 ikey[16];
		InfinityBase::figure_key(uid, ikey);
		printf("infkey "); hex(uid, 7); printf(" "); hex(ikey, 16); printf("\n");
		int ni = 0;
		const InfName* inames = inf_names(&ni);
		const InfName& pick = inames[t % ni];
		u8 inf[kInfFigureSize];
		CHECK(InfinityBase::create_blank(inf, pick.number, pick.series, uid));
		printf("infblank "); hex(uid, 7); printf(" %06x %u ", pick.number, pick.series); hex(inf, kInfFigureSize); printf("\n");
		rnd(inf + 16, 16); // random encrypted block: number decode must still match
		u8 n4[4];
		wr_be32(n4, InfinityBase::figure_number_of(inf));
		printf("infnum "); hex(inf, kInfFigureSize); printf(" "); hex(n4, 4); printf("\n");
		u8 s8[8];
		rnd(s8, 8);
		wr_be32(c4, InfinityBase::descramble(s8));
		printf("descr "); hex(s8, 8); printf(" "); hex(c4, 4); printf("\n");
		const u32 sv = rd_be32(s8), sg = rd_be32(s8 + 4);
		InfinityBase::scramble(sv, sg, s8);
		printf("scr %08x %08x ", sv, sg); hex(s8, 8); printf("\n");
		InfinityBase ib;
		ib.seed(sv);
		u8 r32[32];
		for (int i = 0; i < 8; i++) wr_be32(r32 + i * 4, ib.next());
		printf("rng %08x ", sv); hex(r32, 32); printf("\n");
	}
}

static void test_bytes_and_crc()
{
	u8 b[4];
	wr_le32(b, 0x11223344);
	CHECK(b[0] == 0x44 && b[3] == 0x11);
	CHECK(rd_le32(b) == 0x11223344);
	CHECK(rd_be32(b) == 0x44332211);
	const char* s = "123456789";
	CHECK(skylander_crc16(0xFFFF, (const u8*)s, 9) == 0x29B1); // CRC-16/CCITT-FALSE check value
}

static void test_tea_roundtrip()
{
	for (int t = 0; t < 1000; t++)
	{
		u8 in[8], key[16], enc[8], dec[8];
		rnd(in, 8); rnd(key, 16);
		DimensionsToypad::encrypt(in, key, enc);
		DimensionsToypad::decrypt(enc, key, dec);
		CHECK(memcmp(in, dec, 8) == 0);
	}
}

static void test_dim_create_all_characters()
{
	int n = 0;
	const DimName* names = dim_names(&n);
	int ok = 0;
	for (int i = 0; i < n; i++)
	{
		u8 uid[7], buf[kDimFigureSize];
		rnd(uid, 7);
		DimensionsToypad::create_blank(buf, (u16)names[i].id, uid);
		if (DimensionsToypad::figure_id_of(buf) == names[i].id)
			ok++;
		else
			printf("  id %u (%s) read back as %u\n", names[i].id, names[i].name, DimensionsToypad::figure_id_of(buf));
	}
	CHECK(ok == n);
	printf("  %d/%d Dimensions characters round-trip through create -> read id\n", ok, n);
}

// Plays the game's side of the toy pad handshake.
static void test_dim_lights()
{
	DimensionsToypad d;
	u8 cmd[32], rgb[3];
	const u32 start = d.light_changes();
	// C0: one pad.
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x06; cmd[2] = 0xC0; cmd[3] = 0x05;
	cmd[4] = 2; cmd[5] = 0x80; cmd[6] = 0x00; cmd[7] = 0xFF;
	d.handle_command(cmd, 32);
	d.light(kPadLeft, rgb);
	CHECK(rgb[0] == 0x80 && rgb[1] == 0 && rgb[2] == 0xFF);
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
	CHECK(d.light_changes() == start + 1);
	// The same colour again isn't a change.
	d.handle_command(cmd, 32);
	CHECK(d.light_changes() == start + 1);
	// Every command still gets an answer.
	u8 out[32];
	CHECK(d.next_packet(out) && out[0] == 0x55 && out[2] == 0x05);
	// C0 pad 0: all three.
	cmd[4] = 0; cmd[5] = 1; cmd[6] = 2; cmd[7] = 3;
	d.handle_command(cmd, 32);
	for (u8 p = 1; p <= 3; p++)
	{
		d.light(p, rgb);
		CHECK(rgb[0] == 1 && rgb[1] == 2 && rgb[2] == 3);
	}
	// C2 fade: pad speed count r g b.
	memset(cmd + 4, 0, 28);
	cmd[2] = 0xC2; cmd[4] = 1; cmd[5] = 9; cmd[6] = 9; cmd[7] = 10; cmd[8] = 20; cmd[9] = 30;
	d.handle_command(cmd, 32);
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30);
	// C3 flash: pad on off count r g b.
	memset(cmd + 4, 0, 28);
	cmd[2] = 0xC3; cmd[4] = 3; cmd[5] = 5; cmd[6] = 5; cmd[7] = 0xFF; cmd[8] = 40; cmd[9] = 50; cmd[10] = 60;
	d.handle_command(cmd, 32);
	d.light(kPadRight, rgb);
	CHECK(rgb[0] == 40 && rgb[1] == 50 && rgb[2] == 60);
	// C8 colour all: (on r g b) for centre, left, right; off leaves a pad.
	memset(cmd + 4, 0, 28);
	cmd[2] = 0xC8;
	const u8 all[12] = {1, 0xAA, 0, 0xAA, 0, 9, 9, 9, 1, 0, 0xBB, 0};
	memcpy(cmd + 4, all, 12);
	d.handle_command(cmd, 32);
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 0xAA && rgb[1] == 0 && rgb[2] == 0xAA);
	d.light(kPadLeft, rgb);
	CHECK(rgb[0] == 1 && rgb[1] == 2 && rgb[2] == 3);
	d.light(kPadRight, rgb);
	CHECK(rgb[0] == 0 && rgb[1] == 0xBB && rgb[2] == 0);
	// C6 fade all: (on speed count r g b) x 3.
	memset(cmd + 4, 0, 28);
	cmd[2] = 0xC6;
	const u8 fade[18] = {0, 1, 1, 7, 7, 7, 1, 1, 1, 0x11, 0x22, 0x33, 0, 1, 1, 7, 7, 7};
	memcpy(cmd + 4, fade, 18);
	d.handle_command(cmd, 32);
	d.light(kPadLeft, rgb);
	CHECK(rgb[0] == 0x11 && rgb[1] == 0x22 && rgb[2] == 0x33);
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 0xAA);
	// C7 flash all: (on on_t off_t count r g b) x 3.
	memset(cmd + 4, 0, 28);
	cmd[2] = 0xC7;
	const u8 flash[21] = {1, 2, 2, 9, 0x80, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 9, 0x40, 0x41, 0x42};
	memcpy(cmd + 4, flash, 21);
	d.handle_command(cmd, 32);
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 0x80 && rgb[1] == 0 && rgb[2] == 0x80);
	d.light(kPadRight, rgb);
	CHECK(rgb[0] == 0x40 && rgb[1] == 0x41 && rgb[2] == 0x42);
	d.reset();
	d.light(kPadCenter, rgb);
	CHECK(rgb[0] == 0 && d.light_changes() == 0);
}

static void test_dim_session()
{
	ToyRouter r;
	r.reset(kToyUnknown);
	u8 cmd[32], reply[32];
	u32 n;

	// Nothing to read before the game talks.
	CHECK(r.on_read(reply, 32, 0) == 0);

	// Wake (0xB0)
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x0F; cmd[2] = 0xB0; cmd[3] = 0x01;
	CHECK(r.on_write(cmd, 32));
	CHECK(r.kind() == kToyDimensions);
	n = r.on_read(reply, 32, 0);
	CHECK(n == 32 && reply[0] == 0x55 && reply[1] == 0x0E && memcmp(reply + 3, "(c) LEGO 2014", 13) == 0);

	// Seed (0xB1): game encrypts seed + confirmation; pad must echo confirmation.
	u8 plain[8], enc[8], dec[8];
	wr_le32(plain, 0xDEADBEEF);      // seed
	wr_be32(plain + 4, 0x12345678);  // confirmation
	DimensionsToypad::encrypt(plain, NULL, enc);
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x0A; cmd[2] = 0xB1; cmd[3] = 0x02;
	memcpy(cmd + 4, enc, 8);
	CHECK(r.on_write(cmd, 32));
	n = r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0x55 && reply[1] == 0x09 && reply[2] == 0x02);
	DimensionsToypad::decrypt(reply + 3, NULL, dec);
	CHECK(rd_be32(dec) == 0x12345678);
	u32 sum = 0; for (int i = 0; i < 11; i++) sum += reply[i];
	CHECK(reply[11] == (u8)sum);

	// Challenge (0xB3): reply carries the confirmation in the second word.
	wr_be32(plain, 0xCAFEF00D);
	wr_le32(plain + 4, 0);
	DimensionsToypad::encrypt(plain, NULL, enc);
	cmd[2] = 0xB3; cmd[3] = 0x03;
	memcpy(cmd + 4, enc, 8);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	DimensionsToypad::decrypt(reply + 3, NULL, dec);
	CHECK(rd_be32(dec + 4) == 0xCAFEF00D);

	// Place Gandalf (id 2) on the left pad, slot 0.
	u8 fig[kDimFigureSize], uid[7] = {4, 1, 2, 3, 4, 5, 6};
	DimensionsToypad::create_blank(fig, 2, uid);
	CHECK(r.dim.load(fig, "Hdd:\\Gandalf.bin", 0) == 2);
	n = r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0x56 && reply[1] == 0x0B && reply[2] == kPadLeft && reply[4] == 1 && reply[5] == 0);

	// Read pages 0-3 of index 1.
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x04; cmd[2] = 0xD2; cmd[3] = 0x04; cmd[4] = 1; cmd[5] = 0;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[1] == 0x12 && memcmp(reply + 4, fig, 16) == 0);

	// Model id (0xD4).
	plain[0] = 1; plain[1] = plain[2] = plain[3] = 0;
	wr_be32(plain + 4, 0x0BADF00D);
	DimensionsToypad::encrypt(plain, NULL, enc);
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x0A; cmd[2] = 0xD4; cmd[3] = 0x05;
	memcpy(cmd + 4, enc, 8);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	DimensionsToypad::decrypt(reply + 4, NULL, dec);
	CHECK(reply[1] == 0x0A && rd_le32(dec) == 2 && rd_be32(dec + 4) == 0x0BADF00D);

	// Write page 0x10 -> figure marked dirty for saving.
	memset(cmd, 0, 32);
	cmd[0] = 0x55; cmd[1] = 0x08; cmd[2] = 0xD3; cmd[3] = 0x06; cmd[4] = 1; cmd[5] = 0x10;
	cmd[6] = 0xAA; cmd[7] = 0xBB; cmd[8] = 0xCC; cmd[9] = 0xDD;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(r.dim.figure(0).dirty && r.dim.figure(0).data[0x40] == 0xAA);

	// Move to centre pad (slot 1): removed event then added event.
	CHECK(r.dim.move(0, 1));
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0x56 && reply[2] == kPadLeft && reply[5] == 1);
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0x56 && reply[2] == kPadCenter && reply[4] == 2 && reply[5] == 0);
	CHECK(!r.dim.occupied(0) && r.dim.occupied(1) && r.dim.figure(1).dirty);

	CHECK(r.dim.remove(1));
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0x56 && reply[5] == 1 && r.dim.count() == 0);
}

// Dolphin-style figure: NUID/BCC/ATQA/SAK, ID and variant, checksum, and
// key A per sector (expected keys worked out in Python from Dolphin's code;
// sector 0's is the well-known 4B0B20107CCB).
static void test_sky_create_figure()
{
	u8 fig[kSkyFigureSize];
	const u8 nuid[4] = {0x12, 0x34, 0x56, 0x78};
	skylander_create_figure(fig, 630, 0x0000, nuid); // Crash Bandicoot
	CHECK(fig[0] == 0x12 && fig[1] == 0x34 && fig[2] == 0x56 && fig[3] == 0x78);
	CHECK(fig[4] == (0x12 ^ 0x34 ^ 0x56 ^ 0x78));
	CHECK(fig[5] == 0x81 && fig[6] == 0x01 && fig[7] == 0x0F);
	CHECK(rd_le16(fig + 0x10) == 630 && rd_le16(fig + 0x1C) == 0);
	CHECK(rd_le16(fig + 0x1E) == skylander_crc16(0xFFFF, fig, 0x1E));
	const u8 key0[6] = {0x4B, 0x0B, 0x20, 0x10, 0x7C, 0xCB};
	const u8 key1[6] = {0x23, 0x15, 0x50, 0xB9, 0xAF, 0x28};
	const u8 key15[6] = {0x28, 0x97, 0xEB, 0x5F, 0x19, 0xC6};
	CHECK(memcmp(fig + 0 * 64 + 48, key0, 6) == 0);
	CHECK(memcmp(fig + 1 * 64 + 48, key1, 6) == 0);
	CHECK(memcmp(fig + 15 * 64 + 48, key15, 6) == 0);
	CHECK(rd_le32(fig + 0x36) == 0x690F0F0F && rd_le32(fig + 0x76) == 0x69080F7F);
	// It still loads and answers like any other figure.
	SkylanderPortal portal;
	CHECK(portal.load(fig, "Crash Bandicoot.sky") >= 0);
}

static void test_sky_session()
{
	ToyRouter r;
	r.reset(kToyUnknown);
	u8 cmd[32], reply[32];

	memset(cmd, 0, 32);
	cmd[0] = 'R';
	CHECK(r.on_write(cmd, 32));
	CHECK(r.kind() == kToySkylanders);
	CHECK(r.on_read(reply, 32, 100) == 32 && reply[0] == 'R');

	cmd[0] = 'A'; cmd[1] = 0x01;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 101);
	CHECK(reply[0] == 'A' && reply[1] == 0x01 && reply[2] == 0xFF && reply[3] == 0x77);

	// Idle status is rate limited.
	CHECK(r.on_read(reply, 32, 200) == 32 && reply[0] == 'S');
	CHECK(r.on_read(reply, 32, 205) == 0);
	CHECK(r.on_read(reply, 32, 221) == 32 && reply[0] == 'S');

	// Place Spyro (id 404 in variant 0 is "Legendary Bash"? we just use id 1).
	u8 fig[kSkyFigureSize];
	skylander_create_blank(fig, 1, 0);
	const int slot = r.sky.load(fig, "Hdd:\\Sonic Boom.sky");
	CHECK(slot == 0);
	r.on_read(reply, 32, 300);
	CHECK(reply[0] == 'S' && (rd_le16(reply + 1) & 3) == 3); // "arriving"
	r.on_read(reply, 32, 400);
	CHECK((rd_le16(reply + 1) & 3) == 1);                    // "present"

	// Query block 1.
	memset(cmd, 0, 32);
	cmd[0] = 'Q'; cmd[1] = 0x10; cmd[2] = 0x01;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 401);
	CHECK(reply[0] == 'Q' && reply[1] == 0x10 && reply[2] == 1 && memcmp(reply + 3, fig + 16, 16) == 0);

	// Write block 8.
	memset(cmd, 0, 32);
	cmd[0] = 'W'; cmd[1] = 0x10; cmd[2] = 0x08;
	for (int i = 0; i < 16; i++) cmd[3 + i] = (u8)(0xA0 + i);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 402);
	CHECK(reply[0] == 'W' && r.sky.figure(0).dirty && r.sky.figure(0).data[0x80] == 0xA0);

	// Second figure goes to the next slot; first returns to its slot after removal.
	u8 fig2[kSkyFigureSize];
	skylander_create_blank(fig2, 2, 0);
	CHECK(r.sky.load(fig2, "b") == 1);
	CHECK(r.sky.remove(0));
	r.on_read(reply, 32, 500);
	CHECK(((rd_le16(reply + 1)) & 3) == 2);
	r.on_read(reply, 32, 600);
	CHECK(((rd_le16(reply + 1)) & 3) == 0);
	CHECK(r.sky.load(fig, "a") == 0);
	CHECK(r.sky.count() == 2);

	CHECK(sky_name_for(1, 0) && strcmp(sky_name_for(1, 0), "Sonic Boom") == 0);
}

static void test_wrapped_framing()
{
	u8 cmd[32], reply[32];

	// Dimensions on the 360: the game writes 0B <len> 55 ..., and expects
	// replies in the same shape with <len> = frame length (type+len+payload+sum).
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0x0B; cmd[1] = 0x11; cmd[2] = 0x55; cmd[3] = 0x0F; cmd[4] = 0xB0; cmd[5] = 0x01;
		CHECK(r.on_write(cmd, 32));
		CHECK(r.kind() == kToyDimensions && r.wrapped() && r.wrap_prefix() == 0x0B);
		CHECK(r.on_read(reply, 32, 0) == 32);
		CHECK(reply[0] == 0x0B && reply[1] == 0x11 && reply[2] == 0x55 && reply[3] == 0x0E);
		CHECK(memcmp(reply + 5, "(c) LEGO 2014", 13) == 0);
		// Figure-added events are 0x56 0x0B ... -> length 14.
		u8 fig[kDimFigureSize], uid[7] = {4, 9, 8, 7, 6, 5, 4};
		DimensionsToypad::create_blank(fig, 1, uid);
		r.dim.load(fig, "x", 2);
		r.on_read(reply, 32, 0);
		CHECK(reply[0] == 0x0B && reply[1] == 14 && reply[2] == 0x56 && reply[4] == kPadRight);
	}

	// Skylanders with a constant length byte: mirror it.
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0x0B; cmd[1] = 0x14; cmd[2] = 'A'; cmd[3] = 0x01;
		CHECK(r.on_write(cmd, 32));
		CHECK(r.kind() == kToySkylanders && r.wrapped());
		CHECK(r.on_read(reply, 32, 0) == 32);
		CHECK(reply[0] == 0x0B && reply[1] == 0x14 && reply[2] == 'A' && reply[4] == 0xFF && reply[5] == 0x77);
	}

	// Skylanders with a varying length byte: use the reply's meaningful length.
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0x0B; cmd[1] = 0x02; cmd[2] = 'A'; cmd[3] = 0x01;
		r.on_write(cmd, 32);
		r.on_read(reply, 32, 0);
		memset(cmd, 0, 32);
		cmd[0] = 0x0B; cmd[1] = 0x03; cmd[2] = 'Q'; cmd[3] = 0x00; cmd[4] = 0x00;
		r.on_write(cmd, 32);
		r.on_read(reply, 32, 0);
		CHECK(reply[2] == 'Q' && reply[1] == 19); // fixed reply size, even for an empty slot
		r.sky_wrap_len = 0x1E;                     // explicit override
		r.on_read(reply, 32, 100);
		CHECK(reply[2] == 'S' && reply[1] == 0x1E);
	}

	// Bare frames stay bare.
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 'R';
		r.on_write(cmd, 32);
		CHECK(!r.wrapped());
		r.on_read(reply, 32, 0);
		CHECK(reply[0] == 'R');
	}

	// Unknown protocol is refused.
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0x42; cmd[1] = 0x11; cmd[2] = 0x80;
		CHECK(!r.on_write(cmd, 32) && r.kind() == kToyUnknown);
	}

	// Infinity wrapped like Dimensions: 0B <len> FF ...
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0x0B; cmd[1] = 0x06; cmd[2] = 0xFF; cmd[3] = 0x03; cmd[4] = 0x80; cmd[5] = 0x01;
		CHECK(r.on_write(cmd, 32) && r.kind() == kToyInfinity && r.wrapped());
		r.on_read(reply, 32, 0);
		CHECK(reply[0] == 0x0B && reply[1] == 0x18 && reply[2] == 0xAA && reply[3] == 0x15);
	}

	// A bare Skylanders colour command whose green value is 0x55 must not be
	// taken for a wrapped Dimensions frame.
	{
		ToyRouter r;
		r.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 'C'; cmd[1] = 0x10; cmd[2] = 0x55; cmd[3] = 0x20;
		CHECK(r.on_write(cmd, 32) && r.kind() == kToySkylanders && !r.wrapped());
	}
}

static void test_inf_create_all()
{
	int n = 0;
	const InfName* names = inf_names(&n);
	int ok = 0, refused = 0;
	for (int i = 0; i < n; i++)
	{
		u8 uid[7], buf[kInfFigureSize];
		rnd(uid, 7);
		if (!InfinityBase::create_blank(buf, names[i].number, names[i].series, uid))
		{
			refused++; // numbers with a zero top byte, which RPCS3 refuses too
			continue;
		}
		if (InfinityBase::figure_number_of(buf) == names[i].number && infinity_piece_kind(names[i].number) != kInfUnknown)
			ok++;
		else
			printf("  %06X (%s) -> %06X kind %d\n", names[i].number, names[i].name, InfinityBase::figure_number_of(buf), (int)infinity_piece_kind(names[i].number));
	}
	CHECK(ok + refused == n);
	printf("  %d/%d Infinity pieces round-trip through create -> read number (%d refused, as in RPCS3)\n", ok, n, refused);
}

static void test_inf_session()
{
	ToyRouter r;
	r.reset(kToyUnknown);
	u8 cmd[32], reply[32];

	// Activate (0x80)
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x03; cmd[2] = 0x80; cmd[3] = 0x01;
	CHECK(r.on_write(cmd, 32) && r.kind() == kToyInfinity);
	CHECK(r.on_read(reply, 32, 0) == 32 && reply[0] == 0xAA && reply[1] == 0x15);
	CHECK(reply[2] == 0x01); // the reply carries the request's sequence
	{
		u32 sum = 0;
		for (int i = 0; i < 23; i++) sum += reply[i];
		CHECK(reply[23] == (u8)sum);
	}
	// Sequence 0 gives exactly RPCS3's and Dolphin's fixed reply.
	{
		ToyRouter r0;
		r0.reset(kToyUnknown);
		memset(cmd, 0, 32);
		cmd[0] = 0xFF; cmd[1] = 0x03; cmd[2] = 0x80; cmd[3] = 0x00;
		r0.on_write(cmd, 32);
		r0.on_read(reply, 32, 0);
		CHECK(reply[2] == 0x00 && reply[23] == 0x8C);
	}

	// Seed (0x81) then challenge (0x83): the reply descrambles to the RNG's next value.
	u8 sc[8];
	InfinityBase::scramble(0x12345678, 0xABCDEF01, sc);
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x0A; cmd[2] = 0x81; cmd[3] = 0x02;
	memcpy(cmd + 4, sc, 8);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0xAA && reply[1] == 0x01 && reply[2] == 0x02);
	InfinityBase model;
	model.seed(0x12345678);
	const u32 expect = model.next();
	cmd[2] = 0x83; cmd[3] = 0x03;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[1] == 0x09 && InfinityBase::descramble(reply + 3) == expect);

	// Place Mr. Incredible on player 1 and Sulley on player 2.
	u8 a[kInfFigureSize], b[kInfFigureSize], uid[7] = {4, 1, 2, 3, 4, 5, 6}, uid2[7] = {4, 9, 9, 9, 9, 9, 9};
	InfinityBase::create_blank(a, 0x0F4241, 1, uid);
	InfinityBase::create_blank(b, 0x0F4242, 1, uid2);
	CHECK(r.inf.free_slot_for(kInfCharacter, 1) == 3);
	CHECK(r.inf.load(a, "a", 3) == 0x0F4241);
	CHECK(r.inf.load(b, "b", r.inf.free_slot_for(kInfCharacter, 2)) == 0x0F4242);
	CHECK(r.inf.free_slot_for(kInfCharacter, 1) == -1);
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0xAB && reply[1] == 0x04 && reply[2] == 2 && reply[4] == 0 && reply[5] == 0);
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0xAB && reply[2] == 3 && reply[4] == 1);

	// Present figures (0xA1): two entries, player-side base 0x20/0x30 + order.
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x02; cmd[2] = 0xA1; cmd[3] = 0x04;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[1] == 5 && reply[3] == 0x20 && reply[4] == 0x09 && reply[5] == 0x31 && reply[6] == 0x09);

	// UID (0xB4) and block read (0xA2, block 0 = file block 1, the encrypted id).
	cmd[2] = 0xB4; cmd[3] = 0x05; cmd[4] = 0;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(memcmp(reply + 4, uid, 7) == 0);
	cmd[2] = 0xA2; cmd[3] = 0x06; cmd[4] = 1; cmd[5] = 0;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[1] == 0x12 && memcmp(reply + 4, b + 16, 16) == 0);

	// Write (0xA3) block 1 -> file block 4, figure marked dirty.
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x14; cmd[2] = 0xA3; cmd[3] = 0x07; cmd[4] = 0; cmd[5] = 1; cmd[6] = 0;
	for (int i = 0; i < 16; i++) cmd[7 + i] = (u8)(0xC0 + i);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[1] == 0x02 && r.inf.figure(3).dirty && r.inf.figure(3).data[64] == 0xC0);

	// Sector and block, as Disney Infinity 3.0 sent them on a console: write
	// sector 1 block 1 lands in file block 5 (not on top of block 4), reads of
	// (1,0) and (1,1) return different blocks, and (0,1) is the figure id.
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x14; cmd[2] = 0xA3; cmd[3] = 0x08; cmd[4] = 0; cmd[5] = 1; cmd[6] = 1;
	for (int i = 0; i < 16; i++) cmd[7 + i] = (u8)(0xD0 + i);
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(r.inf.figure(3).data[80] == 0xD0 && r.inf.figure(3).data[64] == 0xC0);
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x05; cmd[2] = 0xA2; cmd[3] = 0x09; cmd[4] = 0; cmd[5] = 1; cmd[6] = 1;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[4] == 0xD0);
	cmd[3] = 0x0A; cmd[6] = 0;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(reply[4] == 0xC0);
	cmd[3] = 0x0B; cmd[5] = 0; cmd[6] = 1;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(memcmp(reply + 4, r.inf.figure(3).data + 16, 16) == 0);
	// A write that names a sector trailer (block 3) is ignored.
	const u8 trailer0 = r.inf.figure(3).data[7 * 16];
	memset(cmd, 0, 32);
	cmd[0] = 0xFF; cmd[1] = 0x14; cmd[2] = 0xA3; cmd[3] = 0x0C; cmd[4] = 0; cmd[5] = 1; cmd[6] = 3;
	for (int i = 0; i < 16; i++) cmd[7 + i] = 0xEE;
	r.on_write(cmd, 32);
	r.on_read(reply, 32, 0);
	CHECK(r.inf.figure(3).data[7 * 16] == trailer0);

	// Remove and re-place: the spot keeps its order number.
	CHECK(r.inf.remove(3));
	r.on_read(reply, 32, 0);
	CHECK(reply[0] == 0xAB && reply[2] == 2 && reply[5] == 1);
	r.inf.load(a, "a", 3);
	r.on_read(reply, 32, 0);
	CHECK(reply[4] == 0);

	// Kinds and spots.
	CHECK(infinity_piece_kind(0x0F4241) == kInfCharacter);
	CHECK(r.inf.free_slot_for(kInfPlaySet, 1) == 0);
	CHECK(r.inf.free_slot_for(kInfAbilityDisc, 2) == 7);
	CHECK(infinity_group_for_slot(0) == 1 && infinity_group_for_slot(5) == 2 && infinity_group_for_slot(8) == 3);
	CHECK(inf_name_for(0x0F4242) && strcmp(inf_name_for(0x0F4242), "Sulley") == 0);
}

static void test_search()
{
	int idx[512];
	const int n = sky_search("spyro", idx, 512);
	CHECK(n >= 3);
	int dn = dim_search("batmobile", idx, 512);
	CHECK(dn >= 1 && idx[0] < 0);
	char safe[64];
	safe_file_name("Dragon's Peak: Big/Name", safe, sizeof(safe));
	CHECK(strcmp(safe, "Dragon_s Peak_ Big_Name") == 0);
}

#include "test_grid.inc"

int main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "--vectors") == 0)
	{
		print_vectors();
		return 0;
	}
	if (argc > 2 && strcmp(argv[1], "--tpi") == 0)
	{
		// What the console would say about a .tpi file (for make_pictures.py's test).
		static u8 buf[kTpiMaxBytes + 1];
		FILE* f = fopen(argv[2], "rb");
		if (!f)
			return 2;
		const size_t got = fread(buf, 1, sizeof(buf), f);
		fclose(f);
		u32 w, h;
		const char* why = got > (size_t)kTpiMaxBytes ? "too big" : tpi_check(buf, (u32)got, &w, &h);
		if (why)
			printf("%s\n", why);
		else
			printf("ok %ux%u\n", w, h);
		return why ? 1 : 0;
	}
	test_bytes_and_crc();
	test_tea_roundtrip();
	test_dim_create_all_characters();
	test_dim_lights();
	test_dim_session();
	test_sky_create_figure();
	test_sky_session();
	test_wrapped_framing();
	test_inf_create_all();
	test_inf_session();
	test_search();
	test_grid();
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
