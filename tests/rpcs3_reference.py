#!/usr/bin/env python3
"""Independent reference: a line-by-line Python translation of RPCS3's
Dimensions.cpp / Skylander.cpp maths. Reads test vectors (hex) on stdin as
produced by test_core --vectors and checks every output against this model."""
import hashlib, struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

M = 0xFFFFFFFF
COMMAND_KEY = bytes([0x55,0xFE,0xF6,0xB0,0x62,0xBF,0x0B,0x41,0xC9,0xB3,0x7C,0xB4,0x97,0x3E,0x29,0x7B])
CHAR_CONSTANT = bytes([0xB7,0xD5,0xD7,0xE6,0xE7,0xBA,0x3C,0xA8,0xD8,0x75,0x47,0x68,0xCF,0x23,0xE9,0xFE,0xAA])
PWD_CONSTANT = bytes([0x28,0x63,0x29,0x20,0x43,0x6F,0x70,0x79,0x72,0x69,0x67,0x68,0x74,0x20,0x4C,0x45,0x47,0x4F,0x20,0x32,0x30,0x31,0x34,0xAA,0xAA])

le32 = lambda b, o=0: struct.unpack_from("<I", b, o)[0]
be32 = lambda b, o=0: struct.unpack_from(">I", b, o)[0]
rotr = lambda v, n: ((v >> n) | (v << (32 - n))) & M

def encrypt(buf, key=None):
    k = key or COMMAND_KEY
    d1, d2 = le32(buf), le32(buf, 4)
    k1, k2, k3, k4 = (le32(k, i) for i in (0, 4, 8, 12))
    s = 0
    for _ in range(32):
        s = (s + 0x9E3779B9) & M
        d1 = (d1 + ((((d2 << 4) + k1) & M) ^ ((d2 + s) & M) ^ (((d2 >> 5) + k2) & M))) & M
        d2 = (d2 + ((((d1 << 4) + k3) & M) ^ ((d1 + s) & M) ^ (((d1 >> 5) + k4) & M))) & M
    return struct.pack("<II", d1, d2)

def decrypt(buf, key=None):
    k = key or COMMAND_KEY
    d1, d2 = le32(buf), le32(buf, 4)
    k1, k2, k3, k4 = (le32(k, i) for i in (0, 4, 8, 12))
    s = 0xC6EF3720
    for _ in range(32):
        d2 = (d2 - ((((d1 << 4) + k3) & M) ^ ((d1 + s) & M) ^ (((d1 >> 5) + k4) & M))) & M
        d1 = (d1 - ((((d2 << 4) + k1) & M) ^ ((d2 + s) & M) ^ (((d2 >> 5) + k2) & M))) & M
        s = (s - 0x9E3779B9) & M
    assert s == 0
    return struct.pack("<II", d1, d2)

def randomize(key, count):
    sc = 0
    for i in range(count):
        sc = (le32(key, i * 4) + rotr(sc, 25) + rotr(sc, 10) - sc) & M
    return struct.pack("<I", sc)

def scramble(uid, count):
    t = bytearray(uid + CHAR_CONSTANT)
    t[count * 4 - 1] = 0xAA
    return be32(randomize(bytes(t), count))

def figure_key(buf):
    uid = bytes([buf[0], buf[1], buf[2], buf[4], buf[5], buf[6], buf[7]])
    return b"".join(struct.pack(">I", scramble(uid, c)) for c in (3, 4, 5, 6))

def pwd_generate(uid):
    return randomize(uid + PWD_CONSTANT, 8)

def create_blank_character(uid_buf, ident):
    buf = bytearray(0x2D * 4)
    buf[0:8] = uid_buf[0:8]
    buf[0], buf[7] = 0x04, 0x80
    buf[3] = ident & 0xFF
    if ident >= 1000:
        # Vehicles and gadgets differ from RPCS3 (which encrypts them like
        # characters, and the game doesn't take those): as on real tags and in
        # Ellerbach/LegoDimensions LegoTag.EncryptVehicleId, the id is plain
        # little-endian in page 0x24 and page 0x26 is 00 01 00 00.
        uid = bytes([buf[0], buf[1], buf[2], buf[4], buf[5], buf[6], buf[7]])
        struct.pack_into("<H", buf, 36 * 4, ident)
        struct.pack_into(">H", buf, 38 * 4, 1)
        buf[43*4:43*4+4] = pwd_generate(uid)
    elif ident:
        uid = bytes([buf[0], buf[1], buf[2], buf[4], buf[5], buf[6], buf[7]])
        enc = encrypt(struct.pack("<II", ident, ident), figure_key(buf))
        buf[36*4:36*4+4] = enc[0:4]
        buf[37*4:37*4+4] = enc[4:8]
        buf[43*4:43*4+4] = pwd_generate(uid)
    else:
        struct.pack_into(">H", buf, 38 * 4, 1)
    return bytes(buf)

def crc16(init, data):
    crc = init
    for b in data:
        x = ((crc >> 8) ^ b) & 0xFF
        # CCITT table entry computed on the fly
        t = x << 8
        for _ in range(8):
            t = ((t << 1) ^ 0x1021) if t & 0x8000 else (t << 1)
        crc = ((crc << 8) ^ t) & 0xFFFF
    return crc

def sky_blank(ident, var):
    buf = bytearray(0x400)
    struct.pack_into("<I", buf, 0x36, 0x690F0F0F)
    for i in range(1, 0x10):
        struct.pack_into("<I", buf, i * 0x40 + 0x36, 0x69080F7F)
    struct.pack_into("<H", buf, 0, ((ident | var) + 1) & 0xFFFF)
    struct.pack_into("<H", buf, 0x10, ident)
    struct.pack_into("<H", buf, 0x1C, var)
    struct.pack_into("<H", buf, 0x1E, crc16(0xFFFF, bytes(buf[:0x1E])))
    return bytes(buf)

# ---- Disney Infinity (Infinity.cpp / infinity_dialog.cpp) -------------------
# std::array<u8, 32> initialised with 31 values -> trailing zero byte.
INF_SHA1_CONSTANT = bytes([0xAF,0x62,0xD2,0xEC,0x04,0x91,0x96,0x8C,0xC5,0x2A,0x1A,0x71,0x65,0xF8,0x65,0xFE,
    0x28,0x63,0x29,0x20,0x44,0x69,0x73,0x6e,0x65,0x79,0x20,0x32,0x30,0x31,0x33]) + b"\x00"
assert len(INF_SHA1_CONSTANT) == 32
M64 = (1 << 64) - 1

def aes_enc(key, block):
    e = Cipher(algorithms.AES(key), modes.ECB()).encryptor(); return e.update(block) + e.finalize()

def aes_dec(key, block):
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor(); return d.update(block) + d.finalize()

def inf_key(uid7):
    out = hashlib.sha1(INF_SHA1_CONSTANT[:-1] + uid7).digest()   # {begin, end - 1}
    return bytes(out[(3 - x) + i * 4] for i in range(4) for x in range(4))

def inf_crc32(init, data):
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    crc = init
    for b in data:
        crc = (crc >> 8) ^ table[(crc & 0xFF) ^ b]
    return crc

def inf_number(buf):
    d = aes_dec(inf_key(buf[0:7]), buf[16:32])
    return d[1] << 16 | d[2] << 8 | d[3]

def inf_create(number, series, uid7):
    f = bytearray(0x14 * 0x10)
    for i in range(3):
        f[0x36 + i] = (0x17878E >> (2 - i) * 8) & 0xFF
    for index in range(1, 5):
        for i in range(3):
            f[index * 0x40 + 0x36 + i] = (0x778788 >> (2 - i) * 8) & 0xFF
    uid_data = bytearray(16); uid_data[7:11] = bytes([0x89, 0x44, 0x00, 0xC2]); uid_data[0:7] = uid7
    fd = bytearray([0] * 9 + [series, 0xD1, 0x1F] + [0] * 4)
    fd[1], fd[2], fd[3] = (number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF
    fd[4:7] = {1: b"\x0d\x08\x12", 2: b"\x0e\x09\x12", 3: b"\x0f\x08\x1c"}.get(series, bytes(fd[4:7]))
    struct.pack_into(">I", fd, 12, inf_crc32(0, bytes(fd[:12])))
    key = inf_key(bytes(uid7))
    enc, blank = aes_enc(key, bytes(fd)), aes_enc(key, bytes(16))
    f[0:16] = uid_data; f[16:32] = enc
    for b in (4, 8, 12, 13):
        f[16 * b:16 * b + 16] = blank
    return bytes(f)

def inf_descramble(num):
    mask, ret = 0x8E55AA1B3999E8AA, 0
    for _ in range(64):
        if mask & 0x8000000000000000:
            ret = ((ret << 1) | (num & 1)) & M
        num >>= 1; mask = (mask << 1) & M64
    return ret

def inf_scramble(num, garbage):
    mask, ret = 0x8E55AA1B3999E8AA, 0
    for _ in range(64):
        ret = (ret << 1) & M64
        if mask & 1:
            ret |= num & 1; num >>= 1
        else:
            ret |= garbage & 1; garbage >>= 1
        mask >>= 1
    return ret

class InfRng:
    def __init__(self, seed):
        self.a, self.b, self.c, self.d = 0xF1EA5EED, seed, seed, seed
        for _ in range(23): self.next()
    def next(self):
        a, b, c = self.a, self.b, self.c
        ret = ((self.b << 27) | (self.b >> 5)) & M
        temp = (a + (((ret ^ 0xFFFFFFFF) + 1) & M)) & M
        b ^= ((c << 17) | (c >> 15)) & M
        a = self.d
        c = (c + a) & M
        ret = (b + temp) & M
        a = (a + temp) & M
        self.c, self.a, self.b, self.d = a, b, c, ret
        return ret

def rng_vector(seed_hex):
    r = InfRng(int(seed_hex, 16))
    return b"".join(struct.pack(">I", r.next()) for _ in range(8))

fails = n = 0
for line in sys.stdin:
    parts = line.split()
    if not parts:
        continue
    kind, args, got = parts[0], [bytes.fromhex(p) if all(c in "0123456789abcdef" for c in p) and len(p) % 2 == 0 else p for p in parts[1:-1]], bytes.fromhex(parts[-1])
    if kind == "enc":    want = encrypt(args[0], args[1])
    elif kind == "encc": want = encrypt(args[0])
    elif kind == "dec":  want = decrypt(args[0], args[1])
    elif kind == "key":  want = figure_key(args[0])
    elif kind == "pwd":  want = pwd_generate(args[0])
    elif kind == "dimblank": want = create_blank_character(args[0], int(parts[2]))
    elif kind == "skyblank": want = sky_blank(int(parts[1]), int(parts[2]))
    elif kind == "sha1": want = hashlib.sha1(b"" if parts[1] == "-" else args[0]).digest()
    elif kind == "aesenc": want = aes_enc(args[1], args[0])
    elif kind == "aesdec": want = aes_dec(args[1], args[0])
    elif kind == "crc": want = struct.pack(">I", inf_crc32(0, args[0]))
    elif kind == "infkey": want = inf_key(args[0])
    elif kind == "infblank": want = inf_create(int(parts[2], 16), int(parts[3]), args[0])
    elif kind == "infnum": want = struct.pack(">I", inf_number(args[0]))
    elif kind == "descr": want = struct.pack(">I", inf_descramble(int(parts[1], 16)))
    elif kind == "scr": want = struct.pack(">Q", inf_scramble(int(parts[1], 16), int(parts[2], 16)))
    elif kind == "rng": want = rng_vector(parts[1])
    else: raise SystemExit("unknown vector " + kind)
    n += 1
    if want != got:
        fails += 1
        print("MISMATCH", kind, parts[1][:32], "\n got ", got.hex(), "\n want", want.hex())
print(f"reference check: {n} vectors, {fails} mismatches")
sys.exit(1 if fails or n == 0 else 0)
