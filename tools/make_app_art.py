#!/usr/bin/env python3
"""Draws VitaXMB's own artwork: the bubble icon, the LiveArea background and the start image.

Everything is made from the app's own data, so it looks like the app:
  * the sky is the firmware's picture for the month (src/render/sky_tex.h), stretched over the screen with the same 18 % white veil;
  * the wave is the firmware's own wave model (src/render/wave_stock.h, a GMO: a cubic B-spline patch with two morph targets and a motion),
    evaluated and shaded like src/render/wave.h does (a white ribbon whose alpha follows how edge-on each part of the surface is);
  * the text is set in the PSP's own font, assets/psp/ltn0.pgf, with the glyph rules of src/theme/psptheme.h and a soft shadow;
  * the icons are the app's PSP-style category icons (assets/icons).

  make_app_art.py <outdir> [month 1-12] [seconds into the wave's motion]
writes icon0.png (128 x 128), bg.png (840 x 500) and startup.png (280 x 158) into <outdir>.
  make_app_art.py --sheet <file.png> [month]     a contact sheet of the wave at several times, to pick one.
Needs Pillow and numpy.
"""
import math, os, re, struct, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'assets', 'icons')
SS = 2
SKY_VEIL = 0.18


def c_numbers(path, name):
    s = open(path, encoding='utf-8', errors='replace').read()
    i = s.index(name)
    j = s.index('{', i)
    depth, k = 0, j
    while True:
        if s[k] == '{': depth += 1
        elif s[k] == '}':
            depth -= 1
            if depth == 0: break
        k += 1
    body = re.sub(r'/\*.*?\*/', '', s[j:k + 1], flags=re.S)
    return [int(x, 16) if x.lower().startswith('0x') else int(x) for x in re.findall(r'0[xX][0-9a-fA-F]+|-?\d+', body)]


def sky(month, w, h):
    nums = c_numbers(os.path.join(ROOT, 'src', 'render', 'sky_tex.h'), 'sky_months')
    per = 60 * 34 * 3
    px = np.array(nums[(month - 1) * per:month * per], dtype=np.uint8).reshape(34, 60, 3)
    a = np.asarray(Image.fromarray(px, 'RGB').resize((w, h), Image.BILINEAR), dtype=np.float32)
    return a + (255.0 - a) * SKY_VEIL


def u32(b, p): return struct.unpack_from('<I', b, p)[0]
def f32(b, p): return struct.unpack_from('<f', b, p)[0]


def bspline_basis(kn, n, u):
    nk = len(kn)
    B = [[0.0] * 64 for _ in range(4)]
    last = 0
    for i in range(nk - 1):
        if kn[i] < 1.0: last = i
    for i in range(nk - 1):
        B[0][i] = 1.0 if ((kn[i] <= u < kn[i + 1]) or (u >= 1.0 and i == last)) else 0.0
    for k in range(1, 4):
        for i in range(0, nk - 1 - k):
            a = 0.0
            d1, d2 = kn[i + k] - kn[i], kn[i + k + 1] - kn[i + 1]
            if d1 > 0: a += (u - kn[i]) / d1 * B[k - 1][i]
            if d2 > 0: a += (kn[i + k + 1] - u) / d2 * B[k - 1][i + 1]
            B[k][i] = a
    return B[3][:n]


class Wave:
    GU, GV = 48, 24

    def __init__(self):
        g = bytes(c_numbers(os.path.join(ROOT, 'src', 'render', 'wave_stock.h'), 'wave_stock_gmo'))
        assert g[:11] == b'OMG.00.1PSP'
        arr = patch = ku = kv = tex = None
        arr_n = tex_n = 0
        end, fps = 1500.0, 30.0
        fc = []
        stack = []
        pos, end_pos = 16, len(g)
        while True:
            if pos + 8 > end_pos:
                if not stack: break
                pos, end_pos = stack.pop()
                continue
            tsh, sz = u32(g, pos), u32(g, pos + 4)
            t, sh = tsh & 0xFFFF, tsh >> 16
            if sz < 8 or pos + sz > end_pos: break
            body = pos + 8
            if t == 7: arr, arr_n = pos, sz
            elif t == 0x8068: patch = pos
            elif t == 0x8064: ku = pos
            elif t == 0x8065: kv = pos
            elif t == 0x8013: tex, tex_n = pos, sz
            elif t == 0x80b1 and sz >= 16: end = f32(g, body + 4)
            elif t == 0x80b2 and sz >= 12: fps = f32(g, body)
            elif t == 0xc and len(fc) < 4: fc.append((pos, sz))
            nxt = pos + sz
            if t in (2, 3, 4, 5, 6, 8, 9, 0xa, 0xb, 0xf) and sz > 8 and len(stack) < 31:
                stack.append((nxt, end_pos))
                end_pos = nxt
                pos += sh if sh > 8 else 8
                continue
            pos = nxt
        nu, nv = u32(g, patch + 16), u32(g, patch + 20)
        count = u32(g, arr + 32)
        nt = (arr_n - 44) // (count * 12)
        self.nu, self.nv, self.nt = nu, nv, nt
        vd = arr + 44
        ctrl = np.zeros((nt, nu * nv, 3), dtype=np.float64)
        for i in range(nu * nv):
            ix = struct.unpack_from('<H', g, patch + 24 + 2 * i)[0]
            for m in range(nt):
                for c in range(3):
                    ctrl[m, i, c] = f32(g, vd + ix * nt * 12 + m * 12 + c * 4)
        self.ctrl = ctrl
        nku, nkv = (g[ku + 8] - 48) * 10 + (g[ku + 9] - 48), (g[kv + 8] - 48) * 10 + (g[kv + 9] - 48)
        knu = [f32(g, ku + 10 + 4 * i) for i in range(nku)]
        knv = [f32(g, kv + 10 + 4 * i) for i in range(nkv)]
        self.bu = np.array([bspline_basis(knu, nu, min(i / self.GU, 0.99999)) for i in range(self.GU + 1)])
        self.bv = np.array([bspline_basis(knv, nv, min(j / self.GV, 0.99999)) for j in range(self.GV + 1)])
        self.mat = self.mor = None
        for pos, sz in fc:
            dims, keys = u32(g, pos + 8 + 24), u32(g, pos + 8 + 28)
            if (dims != 16 and dims != nt) or keys < 2: continue
            a = np.frombuffer(g, dtype='<f4', count=keys * (dims + 1), offset=pos + 8 + 36).reshape(keys, dims + 1).astype(np.float64)
            if dims == 16: self.mat = a
            else: self.mor = a
        self.tex = np.frombuffer(g, dtype=np.uint8, count=(tex_n - 8 - 24) // 256 * 256, offset=tex + 8 + 24).reshape(-1, 256)
        self.end, self.fps = end, fps

    @staticmethod
    def interp(a, t):
        n = len(a)
        i = 0
        while i + 2 < n and a[i + 1, 0] <= t: i += 1
        t0, t1 = a[i, 0], a[i + 1, 0]
        f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
        f = min(max(f, 0.0), 1.0)
        return a[i, 1:] * (1 - f) + a[i + 1, 1:] * f

    def surface(self, seconds):
        frame = math.fmod(seconds * self.fps, self.end)
        m = self.interp(self.mat, frame).reshape(4, 4)
        w = self.interp(self.mor, frame)
        P = np.tensordot(w, self.ctrl, axes=(0, 0)).reshape(self.nv, self.nu, 3)
        row = np.einsum('iu,vuc->vic', self.bu, P)
        S = np.einsum('jv,vic->ijc', self.bv, row)
        Q = S @ m[:3, :3].T
        GU, GV = self.GU, self.GV
        i0 = np.maximum(np.arange(GU + 1) - 1, 0); i1 = np.minimum(np.arange(GU + 1) + 1, GU)
        j0 = np.maximum(np.arange(GV + 1) - 1, 0); j1 = np.minimum(np.arange(GV + 1) + 1, GV)
        du = Q[i1] - Q[i0]
        dv = Q[:, j1, :] - Q[:, j0, :]
        n = np.cross(du, dv)
        ln = np.linalg.norm(n, axis=2) + 1e-9
        tu = np.clip(((n[..., 0] / ln * 0.5 + 0.5) * 255).astype(int), 0, 255)
        tv = np.clip(((n[..., 1] / ln * 0.5 + 0.5) * (len(self.tex) - 1)).astype(int), 0, len(self.tex) - 1)
        g = self.tex[tv, tu] / 255.0
        edge = 1.0 - np.abs(n[..., 2]) / ln
        a = np.floor(16.0 + 54.0 * edge + 8.0 * (1.0 - g))
        z = 500.0 - Q[..., 2]
        s = 15.0 * 500.0 / z
        return 480.0 + Q[..., 0] * s, 272.0 - Q[..., 1] * s, a


def draw_wave(canvas, wave, seconds):
    H, W = canvas.shape[:2]
    k = W / 960.0
    X, Y, A = wave.surface(seconds)
    X, Y = X * k, Y * k
    for i in range(wave.GU):
        for j in range(wave.GV):
            for tri in (((i, j), (i + 1, j), (i + 1, j + 1)), ((i, j), (i + 1, j + 1), (i, j + 1))):
                px = [X[a, b] for a, b in tri]; py = [Y[a, b] for a, b in tri]; pa = [A[a, b] / 255.0 for a, b in tri]
                x0, x1 = int(max(math.floor(min(px)), 0)), int(min(math.ceil(max(px)) + 1, W))
                y0, y1 = int(max(math.floor(min(py)), 0)), int(min(math.ceil(max(py)) + 1, H))
                if x1 <= x0 or y1 <= y0: continue
                den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
                if abs(den) < 1e-9: continue
                gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
                w0 = ((py[1] - py[2]) * (gx - px[2]) + (px[2] - px[1]) * (gy - py[2])) / den
                w1 = ((py[2] - py[0]) * (gx - px[2]) + (px[0] - px[2]) * (gy - py[2])) / den
                w2 = 1.0 - w0 - w1
                inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
                if not inside.any(): continue
                al = np.where(inside, w0 * pa[0] + w1 * pa[1] + w2 * pa[2], 0.0)[..., None]
                reg = canvas[y0:y1, x0:x1]
                reg[:] = reg * (1 - al) + 255.0 * al


_wave = None


def background(month, seconds, w=960 * SS, h=544 * SS):
    global _wave
    if _wave is None: _wave = Wave()
    canvas = sky(month, w, h)
    draw_wave(canvas, _wave, seconds)
    return Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), 'RGB')


class Pgf:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[4:8] == b'PGF0'
        rev = u32(d, 8)
        cml, cpl, cmb, cpb = u32(d, 16), u32(d, 20), u32(d, 24), u32(d, 28)
        shl, shb = u32(d, 364), u32(d, 368)
        pos, c1, c2 = 392, 0, 0
        if rev == 3:
            c1, c2 = u32(d, 396) & 0xFFFF, u32(d, 404) & 0xFFFF
            pos += 20
        nd, nx, ny, na = d[258], d[259], d[260], d[261]
        sh_sz = (((shl * shb + 31) & ~31) // 8)
        cm_sz = (((cml * cmb + 31) & ~31) // 8)
        cp_sz = (((cpl * cpb + 31) & ~31) // 8)
        self.first = struct.unpack_from('<H', d, 182)[0]
        self.adv = [struct.unpack_from('<i', d, pos + (nd + nx + ny) * 8 + i * 8)[0] for i in range(na)]
        p = pos + (nd + nx + ny + na) * 8 + sh_sz + (c1 + c2) * 4
        self.cm = int.from_bytes(d[p:p + cm_sz], 'little'); p += cm_sz
        self.cp = int.from_bytes(d[p:p + cp_sz], 'little'); p += cp_sz
        self.fd = d[p:]
        self.fdi = int.from_bytes(self.fd, 'little')
        self.cml, self.cpl, self.cmb, self.cpb = cml, cpl, cmb, cpb
        self.cache = {}

    def bits(self, pos, n): return (self.fdi >> pos) & ((1 << n) - 1)

    def glyph(self, cp):
        if cp in self.cache: return self.cache[cp]
        r = None
        if cp >= self.first:
            c = cp - self.first
            if c < self.cml: c = (self.cm >> (c * self.cmb)) & ((1 << self.cmb) - 1)
            if c < self.cpl:
                bp = ((self.cp >> (c * self.cpb)) & ((1 << self.cpb) - 1)) * 32
                bp += 14
                def take(n):
                    nonlocal bp
                    v = self.bits(bp, n); bp += n; return v
                w, h = take(7), take(7)
                l, t = take(7), take(7)
                left = l - 128 if l >= 64 else l
                top = t - 128 if t >= 64 else t
                fl = take(6); take(2); take(2); take(3); take(9)
                if (fl & 4) == 4: take(8)
                else: take(32); take(32)
                if (fl & 8) == 8: take(8)
                else: take(32); take(32)
                if (fl & 0x10) == 0x10: take(8)
                else: take(32); take(32)
                if (fl & 0x20) == 0x20:
                    i = take(8); adv = self.adv[i] if i < len(self.adv) else 0
                else:
                    adv = struct.unpack('<i', struct.pack('<I', take(32)))[0]; take(32)
                r = dict(w=w, h=h, left=left, top=top, flags=fl, adv=adv, ptr=bp // 8)
                self.cache[cp] = r
        return r

    def bitmap(self, g):
        w, h = g['w'], g['h']
        if w <= 0 or h <= 0: return None
        total = w * h
        raw = []
        bp = g['ptr'] * 8
        while len(raw) < total and bp + 8 < len(self.fd) * 8:
            nib = self.bits(bp, 4); bp += 4
            if nib < 8:
                v = self.bits(bp, 4); bp += 4; raw += [v * 17] * (nib + 1)
            else:
                for _ in range(16 - nib):
                    raw.append(self.bits(bp, 4) * 17); bp += 4
        raw = (raw + [0] * total)[:total]
        a = np.array(raw, dtype=np.uint8)
        return a.reshape(h, w) if (g['flags'] & 3) == 1 else a.reshape(w, h).T


def text_layer(font, s, size):
    sc = size / 20.0
    glyphs, pen = [], 0.0
    for ch in s:
        g = font.glyph(ord(ch))
        if g is None: continue
        glyphs.append((g, pen))
        pen += g['adv'] / 64.0 * sc
    pad = 8
    W, H = int(pen) + pad * 2, int(40 * sc) + pad * 2
    base = int(26 * sc) + pad
    out = np.zeros((H, W), dtype=np.float32)
    for g, x in glyphs:
        bm = font.bitmap(g)
        if bm is None: continue
        im = Image.fromarray(bm, 'L').resize((max(1, int(round(g['w'] * sc))), max(1, int(round(g['h'] * sc)))), Image.BILINEAR)
        gx, gy = int(round(pad + x + g['left'] * sc)), int(round(base - g['top'] * sc))
        a = np.asarray(im, dtype=np.float32) / 255.0
        y0, x0 = max(gy, 0), max(gx, 0)
        y1, x1 = min(gy + a.shape[0], H), min(gx + a.shape[1], W)
        if y1 > y0 and x1 > x0:
            out[y0:y1, x0:x1] = np.maximum(out[y0:y1, x0:x1], a[y0 - gy:y1 - gy, x0 - gx:x1 - gx])
    return out, pad, base, pen


def draw_text(canvas, font, s, cx, baseline, size):
    a, pad, base, pen = text_layer(font, s, size)
    H, W = a.shape
    x0, y0 = int(round(cx - pen / 2 - pad)), int(round(baseline - base))
    blur = Image.fromarray((a * 255).astype(np.uint8), 'L').filter(ImageFilter.GaussianBlur(max(1.0, size * 0.045)))
    sh = (np.asarray(blur, dtype=np.float32) * 0.32).astype(np.uint8)
    layer = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
    s_img = Image.new('RGBA', (W, H), (12, 24, 90, 255)); s_img.putalpha(Image.fromarray(sh, 'L'))
    layer.alpha_composite(s_img, (x0 + max(1, int(size * 0.05)), y0 + max(1, int(size * 0.08))))
    t_img = Image.new('RGBA', (W, H), (255, 255, 255, 255)); t_img.putalpha(Image.fromarray((a * 255).astype(np.uint8), 'L'))
    layer.alpha_composite(t_img, (x0, y0))
    canvas.alpha_composite(layer)


def load_icon(name): return Image.open(os.path.join(ICONS, name + '.png')).convert('RGBA')


def paste_icon(canvas, im, cx, cy, size, glow=0.0, shadow=0.55):
    w = size
    h = int(round(im.height * size / im.width))
    ic = im.resize((w, h), Image.LANCZOS)
    a = ic.getchannel('A')
    if shadow:
        sh = Image.new('RGBA', ic.size, (10, 20, 70, 255)); sh.putalpha(a.point(lambda v: int(v * shadow)))
        sl = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
        sl.alpha_composite(sh, (int(cx - w / 2 + size * 0.05), int(cy - h / 2 + size * 0.07)))
        canvas.alpha_composite(sl.filter(ImageFilter.GaussianBlur(size * 0.05)))
    if glow:
        gl = Image.new('RGBA', ic.size, (255, 255, 255, 255)); gl.putalpha(a.point(lambda v: int(v * glow)))
        gll = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
        gll.alpha_composite(gl, (int(cx - w / 2), int(cy - h / 2)))
        canvas.alpha_composite(gll.filter(ImageFilter.GaussianBlur(size * 0.22)))
    layer = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
    layer.alpha_composite(ic, (int(cx - w / 2), int(cy - h / 2)))
    canvas.alpha_composite(layer)


def bar(canvas, cx, cy, unit, gap=1.32):
    paste_icon(canvas, load_icon('tex_video'), cx - unit * gap, cy, int(unit * 0.80), shadow=0.45)
    paste_icon(canvas, load_icon('tex_network'), cx + unit * gap, cy, int(unit * 0.80), shadow=0.45)
    paste_icon(canvas, load_icon('tex_game'), cx, cy, int(unit * 1.30), glow=0.8)


def crop_to(img, aspect, cx_frac=0.5):
    w, h = img.size
    nw = min(w, int(h * aspect))
    nh = int(nw / aspect)
    x0 = int((w - nw) * cx_frac)
    return img.crop((x0, 0, x0 + nw, nh))


def make_bg(base):
    return crop_to(base, 840 / 500).resize((840, 500), Image.LANCZOS)


def make_icon(base, font):
    x0 = int(base.width * 0.12)
    sq = base.crop((x0, 0, x0 + base.height, base.height)).resize((128 * SS, 128 * SS), Image.LANCZOS).convert('RGBA')
    bar(sq, 64 * SS, 46 * SS, 31 * SS, 1.38)
    draw_text(sq, font, 'VitaXMB', 64 * SS, 100 * SS, 21 * SS)
    return sq.resize((128, 128), Image.LANCZOS).convert('RGB')


def make_start(base, font):
    img = base.resize((280 * SS, 158 * SS), Image.LANCZOS).convert('RGBA')
    bar(img, 140 * SS, 56 * SS, 36 * SS)
    draw_text(img, font, 'VitaXMB', 140 * SS, 132 * SS, 28 * SS)
    return img.resize((280, 158), Image.LANCZOS).convert('RGB')


def save_p(im, path):
    """the Vita's installer only accepts 8-bit palette PNGs for LiveArea pictures (error 0x8010113D otherwise), like pngquant makes them"""
    im.quantize(256, Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)


def main():
    if sys.argv[1] == '--sheet':
        month = int(sys.argv[3]) if len(sys.argv) > 3 else 8
        times = [2, 6, 10, 14, 18, 24, 30, 36, 42]
        sheet = Image.new('RGB', (3 * 485 + 5, 3 * 277 + 5), (20, 20, 20))
        for n, t in enumerate(times):
            sheet.paste(background(month, t, 480, 272), (5 + (n % 3) * 485, 5 + (n // 3) * 277))
        sheet.save(sys.argv[2])
        print('sheet', times)
        return
    out = sys.argv[1]
    month = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    os.makedirs(out, exist_ok=True)
    base = background(month, seconds)
    font = Pgf(os.path.join(ROOT, 'assets', 'psp', 'ltn0.pgf'))
    save_p(make_bg(base), os.path.join(out, 'bg.png'))
    save_p(make_icon(base, font), os.path.join(out, 'icon0.png'))
    save_p(make_start(base, font), os.path.join(out, 'startup.png'))
    print('wrote icon0.png, bg.png, startup.png to', out)


if __name__ == '__main__':
    main()
