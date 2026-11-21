// ToyPortal360 - byte helpers.
// The Xbox 360 CPU is big-endian and figure data is little-endian, so every
// multi-byte read/write goes through these helpers instead of pointer casts.
// Written for the VS2010 compiler that ships with the Xbox 360 SDK (C++03).
#ifndef TOYPORTAL_BYTES_H
#define TOYPORTAL_BYTES_H

#include <stddef.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

inline u16 rd_le16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd_le32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline u16 rd_be16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }
inline u32 rd_be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }

inline void wr_le16(u8* p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wr_be16(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
inline void wr_le32(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline void wr_be32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

inline u32 rotl32(u32 v, unsigned n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
inline u32 rotr32(u32 v, unsigned n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

// Every portal/toy pad packet is 32 bytes.
enum { kPacketSize = 0x20 };

struct Packet
{
	u8 b[kPacketSize];
};

// Fixed-size FIFO of packets (no heap use inside game calls).
template <int N>
class PacketQueue
{
public:
	PacketQueue() : head_(0), count_(0) {}
	bool empty() const { return count_ == 0; }
	void clear() { head_ = 0; count_ = 0; }
	void push(const u8* data)
	{
		if (count_ == N) // drop oldest rather than block the game
		{
			head_ = (head_ + 1) % N;
			count_--;
		}
		memcpy(items_[(head_ + count_) % N].b, data, kPacketSize);
		count_++;
	}
	bool pop(u8* out)
	{
		if (count_ == 0)
			return false;
		memcpy(out, items_[head_].b, kPacketSize);
		head_ = (head_ + 1) % N;
		count_--;
		return true;
	}

private:
	Packet items_[N];
	int head_;
	int count_;
};

#endif
