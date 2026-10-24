#!/usr/bin/env python3
"""Reads the text tables of the PSP firmware's RCO files (flash0:/vsh/resource/*.rco).

An RCO keeps its texts per language: a text table (header word 6) with one entry per language, whose records are
(label offset, length in bytes, offset) triples; the text itself sits in the RLZ-packed text blocks that start at header word 14
(each block: language, 1, next, packed length, unpacked length, then the data). RLZ comes from tools/rlz.py.

  python tools/lang/fwtext.py <file.rco> [count]       prints the English and German texts
"""
import os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from rlz import rlz_decompress

LANG_IDS = 'ja en fr es de it nl pt ru ko zht zhs fi sv da no'.split()      # the PSP's language numbers (RCOMage's RCO_LANG_*)


def load(path):
    """{language code: {label: text}} for one RCO file"""
    d = open(path, 'rb').read()
    cs, ds, _ = struct.unpack_from('<3I', d, 0xA4)
    comp = struct.unpack_from('<I', d, 12)[0] >> 4
    t = d[:0xA4] + (rlz_decompress(d[0xB0:0xB0 + cs], ds) if comp == 2 else d[0xA4:])
    H = struct.unpack_from('<41I', t, 0)
    p_text, p_data, l_data, p_label = H[6], H[14], H[15], H[16]
    if p_text == 0xFFFFFFFF or p_data == 0xFFFFFFFF:
        return {}
    blocks = {}
    p, end = p_data, p_data + l_data
    while p + 16 <= end:
        lang, _, nxt, packed, unpacked = struct.unpack_from('<HHIII', d, p)
        raw = d[p + 16:p + 16 + packed]
        blocks[lang] = rlz_decompress(raw, unpacked) if comp == 2 else raw
        if nxt == 0:
            break
        p += nxt
    out = {}
    count = struct.unpack_from('<I', t, p_text + 16)[0]
    q = p_text + 0x28
    for _ in range(count):
        nxt = struct.unpack_from('<I', t, q + 20)[0]
        lang, fmt, n = struct.unpack_from('<HHI', t, q + 0x28)
        buf = blocks.get(lang, b'')
        enc = {0: 'utf-8', 1: 'utf-16-le', 2: 'utf-32-le'}[fmt]
        texts = {}
        for k in range(n):
            lab, ln, off = struct.unpack_from('<3I', t, q + 0x30 + 12 * k)
            label = t[p_label + lab:t.index(b'\0', p_label + lab)].decode()
            texts[label] = buf[off:off + ln].decode(enc, 'replace').rstrip('\0')
        out[LANG_IDS[lang]] = texts
        q += nxt or 0
    return out


if __name__ == '__main__':
    o = load(sys.argv[1])
    print({k: len(v) for k, v in o.items()})
    for lang in ('en', 'de'):
        for label, text in list(o.get(lang, {}).items())[:int(sys.argv[2]) if len(sys.argv) > 2 else 10]:
            print(lang, label, repr(text))
