#!/usr/bin/env python3
"""Decrypts an Infinity figure file written by the real console binary with
Python's own SHA-1 and AES, using RPCS3's key derivation."""
import hashlib, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
CONST = bytes([0xAF,0x62,0xD2,0xEC,0x04,0x91,0x96,0x8C,0xC5,0x2A,0x1A,0x71,0x65,0xF8,0x65,0xFE,
               0x28,0x63,0x29,0x20,0x44,0x69,0x73,0x6e,0x65,0x79,0x20,0x32,0x30,0x31,0x33])
data = open(sys.argv[1], "rb").read()
want = int(sys.argv[2], 16)
digest = hashlib.sha1(CONST + data[:7]).digest()
key = bytes(digest[(3 - x) + i * 4] for i in range(4) for x in range(4))
plain = Cipher(algorithms.AES(key), modes.ECB()).decryptor().update(data[16:32])
number = plain[1] << 16 | plain[2] << 8 | plain[3]
import zlib
crc_ok = int.from_bytes(plain[12:16], "big") == (zlib.crc32(plain[:12], 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF
print("figure number %06X (want %06X), series %d, crc %s" % (number, want, plain[9], "ok" if crc_ok else "BAD"))
sys.exit(0 if number == want and crc_ok and len(data) == 320 else 1)
