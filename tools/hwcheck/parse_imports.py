#!/usr/bin/env python3
"""Reads TP-IMPORTS.bin (written by the TOYPORTAL_BREADCRUMBS build) and lists
the game's imports, naming XAM functions from the dumped export addresses.
usage: parse_imports.py TP-IMPORTS.bin [xam_table.inc [xboxkrnl_table.inc] from Xenia]"""
import re, struct, sys

d = open(sys.argv[1], 'rb').read()
names = {}
if len(sys.argv) > 2:
    for m in re.finditer(r'XE_EXPORT\(\s*\w+\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)', open(sys.argv[2]).read()):
        names[int(m.group(1), 16)] = m.group(2)
knames = {}
if len(sys.argv) > 3:
    for m in re.finditer(r'XE_EXPORT\(\s*\w+\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)', open(sys.argv[3]).read()):
        knames[int(m.group(1), 16)] = m.group(2)
be = lambda o: struct.unpack('>I', d[o:o + 4])[0]
assert d[:4] == b'TPIM', 'not a TP-IMPORTS.bin'
wanted = [be(4 + 4 * i) for i in range(4)]
print('looking for GetRaw %08X SetRaw %08X GetRawEx %08X SetRawEx %08X' % tuple(wanted))
p = 20
size = be(p)
if size == 0:
    print('no import table found'); sys.exit()
desc = d[p:p + size]
name_size, mod_count = be(p + 4), be(p + 8)
mods = [n.decode() for n in desc[12:12 + name_size].split(b'\0') if n]
tables, t = [], p + 12 + name_size
for m in range(mod_count):
    tsize = be(t)
    count = struct.unpack('>H', d[t + 0x26:t + 0x28])[0]
    idx = d[t + 0x25]
    tables.append((mods[idx] if idx < len(mods) else '?', count))
    t += tsize
p += size
recs = []
for mod, count in tables:
    for i in range(count):
        recs.append((mod,) + struct.unpack('>5I', d[p:p + 20])); p += 20
assert d[p:p + 4] == b'XAM!', 'export table marker missing'
exports = struct.unpack('>3072I', d[p + 4:p + 4 + 3072 * 4])
by_addr = {a: o for o, a in enumerate(exports) if a}
p += 4 + 3072 * 4
kby_addr = {}
if d[p:p + 4] == b'KRN!':
    kexports = struct.unpack('>1024I', d[p + 4:p + 4 + 4096])
    kby_addr = {a: o for o, a in enumerate(kexports) if a}

def target(w):
    if w[0] >> 16 != 0x3D60 or w[2] != 0x7D6903A6 or w[3] != 0x4E800420: return None
    hi = (w[0] & 0xFFFF) << 16
    if w[1] >> 16 == 0x616B: return hi | (w[1] & 0xFFFF)
    if w[1] >> 16 == 0x396B: return (hi + struct.unpack('>h', struct.pack('>H', w[1] & 0xFFFF))[0]) & 0xFFFFFFFF
    return None

for mod, count in tables:
    print('%s: %d records' % (mod, count))
for mod, addr, *w in recs:
    t = target(w)
    v = t if t is not None else w[0]
    kind = 'stub' if t is not None else 'slot'
    tag = ''
    if v in wanted: tag = '  <== TOY PAD ' + ['GetRaw', 'SetRaw', 'GetRawEx', 'SetRawEx'][wanted.index(v)]
    elif mod == 'xam.xex' and v in by_addr:
        o = by_addr[v]; tag = '  xam %#05x %s' % (o, names.get(o, ''))
    elif mod == 'xboxkrnl.exe' and v in kby_addr:
        o = kby_addr[v]; tag = '  krnl %#05x %s' % (o, knames.get(o, ''))
    elif 0x80000000 <= v < 0x90000000 and mod == 'xboxkrnl.exe':
        tag = '  (redirected: points into a loaded image)'
    print('  %-12s %08X %s -> %08X%s' % (mod, addr, kind, v, tag))
