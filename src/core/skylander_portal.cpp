// ToyPortal360 - emulated Skylanders Portal of Power.
// Protocol logic ported from RPCS3 (rpcs3/Emu/Io/Skylander.cpp, GPL-2.0).
#include "skylander_portal.h"

u16 skylander_crc16(u16 init_value, const u8* buffer, u32 size)
{
	static const unsigned short CRC_CCITT_TABLE[256] = {
	    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF, 0x1231, 0x0210, 0x3273,
	    0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B, 0x8528,
	    0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4, 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC, 0x48C4, 0x58E5, 0x6886,
	    0x78A7, 0x0840, 0x1861, 0x2802, 0x3823, 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC, 0xFBBF,
	    0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49, 0x7E97, 0x6EB6, 0x5ED5,
	    0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F, 0x1080, 0x00A1, 0x30C2,
	    0x20E3, 0x5004, 0x4025, 0x7046, 0x6067, 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB, 0x95A8,
	    0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D, 0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691,
	    0x16B0, 0x6657, 0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3, 0xCB7D, 0xDB5C, 0xEB3F,
	    0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A, 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92, 0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07, 0x5C64,
	    0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

	u16 crc = init_value;
	for (u32 i = 0; i < size; i++)
	{
		const u16 tmp = (u16)((crc >> 8) ^ buffer[i]);
		crc = (u16)((crc << 8) ^ CRC_CCITT_TABLE[tmp]);
	}
	return crc;
}

void skylander_create_blank(u8* buf, u16 id, u16 variant)
{
	memset(buf, 0, kSkyFigureSize);
	// Sector trailer access bits, as written by RPCS3's figure creator.
	wr_le32(buf + 0x36, 0x690F0F0F);
	for (u32 index = 1; index < 0x10; index++)
		wr_le32(buf + (index * 0x40) + 0x36, 0x69080F7F);
	wr_le16(buf + 0x00, (u16)((id | variant) + 1));
	wr_le16(buf + 0x10, id);
	wr_le16(buf + 0x1C, variant);
	wr_le16(buf + 0x1E, skylander_crc16(0xFFFF, buf, 0x1E));
}

SkylanderPortal::SkylanderPortal()
{
	reset();
}

// ---- Figure creation as Dolphin does it (SkylanderFigure::Create) -----------
// Dolphin's figures carry what a real tag has: a random NUID with its BCC, the
// ATQA/SAK bytes, and each sector's key A worked out from the NUID. The
// minimal RPCS3-style figure above has none of these, and Skylanders
// Imaginators on a console called it corrupted.

// CRC-64 limited to 48 bits per step (Dolphin's ComputeCRC48).
static unsigned long long sky_crc48(const u8* data, int n)
{
	const unsigned long long poly = 0x42F0E1EBA9EA3693ULL;
	unsigned long long crc = 2ULL * 2ULL * 3ULL * 1103ULL * 12868356821ULL;
	for (int i = 0; i < n; i++)
	{
		crc ^= (unsigned long long)data[i] << 40;
		for (int j = 0; j < 8; j++)
			crc = (crc >> 47) & 1 ? (crc << 1) ^ poly : crc << 1;
	}
	return crc & 0xFFFFFFFFFFFFULL;
}

unsigned long long skylander_key_a(u8 sector, const u8* nuid)
{
	if (sector == 0)
		return 73ULL * 2017ULL * 560381651ULL;
	const u8 data[5] = {nuid[0], nuid[1], nuid[2], nuid[3], sector};
	const unsigned long long crc = sky_crc48(data, 5);
	unsigned long long swapped = 0;
	for (int i = 0; i < 8; i++)
		swapped = (swapped << 8) | ((crc >> (i * 8)) & 0xFF);
	return swapped >> 16;
}

void skylander_create_figure(u8* buf, u16 id, u16 variant, const u8* nuid)
{
	memset(buf, 0, kSkyFigureSize);
	wr_le32(buf + 0x36, 0x690F0F0F);
	for (u32 index = 1; index < 0x10; index++)
		wr_le32(buf + (index * 0x40) + 0x36, 0x69080F7F);
	memcpy(buf, nuid, 4);
	buf[4] = (u8)(nuid[0] ^ nuid[1] ^ nuid[2] ^ nuid[3]); // BCC
	buf[5] = 0x81; buf[6] = 0x01;                          // ATQA
	buf[7] = 0x0F;                                         // SAK
	wr_le16(buf + 0x10, id);
	wr_le16(buf + 0x1C, variant);
	wr_le16(buf + 0x1E, skylander_crc16(0xFFFF, buf, 0x1E));
	for (u8 sector = 0; sector < 0x10; sector++)
	{
		const unsigned long long key = skylander_key_a(sector, buf);
		u8* at = buf + sector * 64 + 48;
		for (int j = 0; j < 6; j++)
			at[5 - j] = (u8)(key >> (j * 8));
	}
}

void SkylanderPortal::reset()
{
	memset(figures_, 0, sizeof(figures_));
	replies_.clear();
	activated_ = true;
	interrupt_counter_ = 0;
	led_r = led_g = led_b = 0;
}

int SkylanderPortal::count() const
{
	int n = 0;
	for (int i = 0; i < kSkySlots; i++)
		if (figures_[i].status & 1)
			n++;
	return n;
}

void SkylanderPortal::push_status(SkyFigure& f, u8 s)
{
	if (f.queued_count < 4)
		f.queued[f.queued_count++] = s;
}

void SkylanderPortal::activate()
{
	if (activated_)
		return;
	// Re-advertise every figure already standing on the portal.
	for (int i = 0; i < kSkySlots; i++)
	{
		if (figures_[i].status & 1)
		{
			push_status(figures_[i], 3);
			push_status(figures_[i], 1);
		}
	}
	activated_ = true;
}

void SkylanderPortal::deactivate()
{
	for (int i = 0; i < kSkySlots; i++)
	{
		SkyFigure& s = figures_[i];
		if (s.queued_count)
		{
			s.status = s.queued[s.queued_count - 1];
			s.queued_count = 0;
		}
		s.status &= 1;
	}
	activated_ = false;
}

void SkylanderPortal::get_status(u8* reply)
{
	u16 status = 0;
	for (int i = kSkySlots - 1; i >= 0; i--)
	{
		SkyFigure& s = figures_[i];
		if (s.queued_count)
		{
			s.status = s.queued[0];
			memmove(s.queued, s.queued + 1, 3);
			s.queued_count--;
		}
		status = (u16)((status << 2) | s.status);
	}
	memset(reply, 0, kPacketSize);
	reply[0] = 'S';
	wr_le16(reply + 1, status);
	reply[5] = interrupt_counter_++;
	reply[6] = 0x01;
}

void SkylanderPortal::handle_command(const u8* buf, u32 len)
{
	if (len == 0)
		return;

	u8 q[kPacketSize];
	memset(q, 0, sizeof(q));

	switch (buf[0])
	{
	case 'A': // Activate
		q[0] = 'A';
		q[1] = len > 1 ? buf[1] : 0;
		q[2] = 0xFF;
		q[3] = 0x77;
		replies_.push(q);
		activate();
		break;
	case 'C': // LED colour
		if (len >= 4)
		{
			led_r = buf[1];
			led_g = buf[2];
			led_b = buf[3];
		}
		break;
	case 'J': // Sync status from game
		q[0] = 'J';
		replies_.push(q);
		break;
	case 'L': // Trap Team side lights
		break;
	case 'M': // Audio firmware version: report 0 so the game never streams audio
		q[0] = 'M';
		q[1] = len > 1 ? buf[1] : 0;
		q[2] = 0x00;
		q[3] = 0x19;
		replies_.push(q);
		break;
	case 'Q': // Read a 16-byte block
	{
		if (len < 3)
			break;
		const u8 slot = buf[1] & 0xF;
		const u8 block = buf[2];
		if (slot >= kSkySlots || block >= 0x40)
			break;
		const SkyFigure& f = figures_[slot];
		q[0] = 'Q';
		q[2] = block;
		if (f.status & 1)
		{
			q[1] = (u8)(0x10 | slot);
			memcpy(q + 3, f.data + 16 * block, 16);
		}
		else
		{
			q[1] = slot;
		}
		replies_.push(q);
		break;
	}
	case 'R': // Shut down / reset
		q[0] = 'R';
		q[1] = 0x02;
		q[2] = 0x18;
		replies_.push(q);
		deactivate();
		break;
	case 'S':
	case 'V':
		break;
	case 'W': // Write a 16-byte block
	{
		if (len < 19)
			break;
		const u8 slot = buf[1] & 0xF;
		const u8 block = buf[2];
		if (slot >= kSkySlots || block >= 0x40)
			break;
		SkyFigure& f = figures_[slot];
		q[0] = 'W';
		q[2] = block;
		if (f.status & 1)
		{
			q[1] = (u8)(0x10 | slot);
			memcpy(f.data + block * 16, buf + 3, 16);
			f.dirty = true;
		}
		else
		{
			q[1] = slot;
		}
		replies_.push(q);
		break;
	}
	default:
		break;
	}
}

bool SkylanderPortal::next_packet(u8* out, bool status_due)
{
	if (replies_.pop(out))
		return true;
	if (!status_due)
		return false;
	get_status(out);
	return true;
}

int SkylanderPortal::load(const u8* data, const char* path)
{
	const u32 serial = rd_le32(data);
	int found = -1;
	// Prefer the slot this figure used last time (mimics the real portal).
	for (int i = 0; i < kSkySlots; i++)
	{
		if ((figures_[i].status & 1) == 0)
		{
			if (figures_[i].last_id == serial)
			{
				found = i;
				break;
			}
			if (found < 0)
				found = i;
		}
	}
	if (found < 0)
		return -1;

	SkyFigure& f = figures_[found];
	memcpy(f.data, data, kSkyFigureSize);
	strncpy(f.path, path ? path : "", sizeof(f.path) - 1);
	f.path[sizeof(f.path) - 1] = 0;
	f.status = 3;
	f.queued_count = 0;
	push_status(f, 3);
	push_status(f, 1);
	f.last_id = serial;
	f.dirty = false;
	return found;
}

bool SkylanderPortal::remove(int slot)
{
	if (slot < 0 || slot >= kSkySlots)
		return false;
	SkyFigure& f = figures_[slot];
	if (!(f.status & 1))
		return false;
	f.status = 2;
	f.queued_count = 0;
	push_status(f, 2);
	push_status(f, 0);
	return true;
}
