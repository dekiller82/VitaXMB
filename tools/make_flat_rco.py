#!/usr/bin/env python3
"""Turns a PSP firmware RCO (RLZ-compressed tables, images and models) into the flat layout the app reads:
the same header and tables, with every image and model stored uncompressed after them, like the .rco files inside PSP themes.

  make_flat_rco.py <original.rco> <tables.rco> <out.rco>

<original.rco> is the file from the firmware (its pointers are physical); <tables.rco> is the same file with the
table block already RLZ-decoded (header + tables at the positions the header names; see rlz.py).
"""
import struct, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
from rlz import rlz_decompress

def main():
    o = open(sys.argv[1], 'rb').read()
    v = bytearray(open(sys.argv[2], 'rb').read())
    ho = list(struct.unpack_from('<41I', o, 0))
    hv = list(struct.unpack_from('<41I', v, 0))
    img_base, mdl_base = ho[32], ho[36]
    blocks = {32: bytearray(), 36: bytearray()}

    def convert(table_ptr, base, blk):
        if table_ptr in (0xffffffff, 0) or base in (0xffffffff, 0):
            return
        count = struct.unpack_from('<I', v, table_ptr + 16)[0]
        p = table_ptr + 0x28
        for _ in range(count):
            w = struct.unpack_from('<20I', v, p)
            fmt, comp = w[10] & 0xffff, w[10] >> 16
            packed, off, unp = w[11], w[12], w[13]
            raw = o[base + off: base + off + packed]
            if comp == 2:
                raw = rlz_decompress(raw, unp)
            elif comp != 0:
                raise SystemExit('unsupported compression %d' % comp)
            new_off = len(blk)
            blk += raw
            while len(blk) % 16: blk.append(0)
            struct.pack_into('<IIII', v, p + 0x28, fmt, len(raw), new_off, len(raw))
            nxt = w[5]
            p += nxt if nxt else 0x38
    convert(hv[9], img_base, blocks[32])
    convert(hv[8], mdl_base, blocks[36])
    while len(v) % 16: v.append(0)
    hv[32] = len(v); hv[33] = len(blocks[32])
    v += blocks[32]
    hv[36] = len(v); hv[37] = len(blocks[36])
    v += blocks[36]
    hv[3] = 0
    struct.pack_into('<41I', v, 0, *hv)
    open(sys.argv[3], 'wb').write(bytes(v))
    print('flat rco', sys.argv[3], len(v), 'bytes; images', len(blocks[32]), 'models', len(blocks[36]))

main()
