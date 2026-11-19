// ToyPortal360 - small, endian-neutral SHA-1, AES-128 (single block, ECB)
// and CRC-32, for Disney Infinity figure files. Not constant-time; it only
// handles toy data.
#ifndef TOYPORTAL_TINY_CRYPTO_H
#define TOYPORTAL_TINY_CRYPTO_H

#include "bytes.h"

void sha1(const u8* data, u32 len, u8 out[20]);

struct Aes128
{
	u8 round_keys[176];
};
void aes128_init(Aes128* ctx, const u8 key[16]);
void aes128_encrypt(const Aes128* ctx, const u8 in[16], u8 out[16]);
void aes128_decrypt(const Aes128* ctx, const u8 in[16], u8 out[16]);

// Reflected CRC-32 (polynomial 0xEDB88320) with no final XOR, as RPCS3's
// infinity_crc32 computes it.
u32 crc32_raw(u32 init, const u8* data, u32 len);

#endif
