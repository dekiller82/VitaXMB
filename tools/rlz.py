"""RLZ decoder: the exact inverse of RCOMage's rlzcompress() (adaptive binary range coder + LZ)."""

class RC:
    def __init__(self, data):
        self.d = data
        self.pos = 1                      # byte 0 is the mode byte (it replaced the encoder's leading zero)
        self.code = 0
        self.range = 0xFFFFFFFF
        for _ in range(4):
            self.code = (self.code << 8) | self.next()
        self.code &= 0xFFFFFFFF

    def next(self):
        b = self.d[self.pos] if self.pos < len(self.d) else 0
        self.pos += 1
        return b

    def norm(self):
        # the encoder shifts out at most ONE byte after each coded bit, so the decoder must too
        if (self.range >> 24) == 0:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self.code = ((self.code << 8) | self.next()) & 0xFFFFFFFF

    def bit(self, arr, idx):
        p = arr[idx]
        bound = (self.range >> 8) * p
        if self.code < bound:
            self.range = bound
            b = 1
        else:
            self.code -= bound
            self.range -= bound
            b = 0
        q = p - (p >> 3)
        arr[idx] = (q + 31) & 0xFF if b else q
        self.norm()
        return b

    def direct(self, nbits):
        """equiprobable bits, high to low (encoder: bit 0 -> upper half)"""
        v = 0
        for _ in range(nbits):
            self.range >>= 1
            if self.code >= self.range:
                self.code -= self.range
                b = 0
            else:
                b = 1
            v = (v << 1) | b
        self.norm()
        return v

    def special(self, arr, base, edx):
        """inverse of write_special: returns the value of bits edx..0 (the caller adds any implied top bit)"""
        num = 0
        if edx >= 3:
            num |= self.bit(arr, base) << edx
            edx -= 1
            if edx >= 3:
                num |= self.bit(arr, base) << edx
                edx -= 1
            base += 1
            if edx >= 3:
                nd = edx - 2                         # direct bits at positions edx .. 3
                v = self.direct(nd)
                num |= v << 3
                edx = 2
        while edx >= 0:
            num |= self.bit(arr, base) << edx
            base += 1
            edx -= 1
        return num


# every table gets its own room (the RCOMage encoder packs them back to back and lets big values spill over; the real format does not)
LIT = 0
DL = LIT + 8 * 0xFF
DS = DL + 32 * 8 + 8
DIST = DS + 8 * 8 + 8
LEND = DIST + 40 * 0x20
ML = LEND + 8 * 8 + 8
TOTAL = ML + 16 * 8 * 4 + 64


def rlz_decompress(data, out_len):
    mode = data[0]
    real = mode & 7
    byteset = (4 - ((mode >> 3) & 3)) << 5
    mem = [byteset] * TOTAL            # the encoder keeps these tables back to back, and tree indexes spill into the next one
    rc = RC(data)
    out = bytearray()
    var_c = 0
    last = 0
    while len(out) < out_len:
        n = 0
        while n < 8:
            if rc.bit(mem, LEND + n * 8 + var_c):
                n += 1
            else:
                break
        h = n - 1                                     # -1 -> a literal
        if h < 0:
            length = 0
        elif h == 0:
            length = 1
        else:
            i = h - 1
            base = ML + ((var_c & 7) + i * 8) * 4 + ((len(out) << i) & 3)
            low = rc.special(mem, base, i)
            length = (1 << h) | low
        if length == 0:                               # literal
            ctx = (((len(out) & 7) << 8 | last) >> real) & 7
            node = 1
            for _ in range(8):
                node = (node << 1) | rc.bit(mem, LIT + ctx * 0xFF + node - 1)
            out.append(node & 0xFF)
            if var_c > 0:
                var_c -= 1
        else:
            match_len = length + 1
            if match_len < 4:
                tree, nbits, add = DS, 3, 0
            else:
                tree, nbits, add = DL, 5, 0xC
            node = 1
            s = 0
            for _ in range(nbits):
                b = rc.bit(mem, tree + node * 8 + h)
                s = (s << 1) | b
                node = node * 2 + b
            msb = s - add if add else s
            if msb < 0:
                raise ValueError('bad distance class %d' % s)
            if msb == 0:
                sbp1 = 1
            else:
                i = msb - 1
                low = rc.special(mem, DIST + i * 0x20, i)
                sbp1 = (1 << msb) | low
            src = len(out) - sbp1
            if src < 0:
                raise ValueError('distance before start (%d at %d)' % (sbp1, len(out)))
            for k in range(match_len):
                out.append(out[src + k])
            var_c = 7 - (len(out) & 1)
        last = out[-1]
    return bytes(out[:out_len])
