#!/usr/bin/env python3
"""Makes the icons of the retro systems (assets/icons/tex_sys_<name>.png, 128 px) in the style of the PSP's XMB icons.

    python tools/make_system_icons.py

The outlines are the monochrome system icons of libretro's retroarch-assets (xmb/monochrome/png, Creative Commons Attribution 4.0,
https://github.com/libretro/retroarch-assets), downloaded into tools/system_icons_src when they are not there yet. Each outline becomes
the glassy grey body of an icon with a bevelled rim, a gloss and the PSP's drop of transparency; the screens, keyboards and button
groups that were holes in the outline become darker recesses, and small details (d-pads, buttons, single keys) are left out because
the PSP's own icons have none. A few icons are tuned by hand below. Needs numpy, scipy and pillow.
"""
import io
import os
import sys
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage as ndi

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "system_icons_src")
OUT = os.path.join(HERE, "..", "assets", "icons")
FONT = os.path.join(HERE, "SourceSans3.ttf")
BASE_URL = "https://raw.githubusercontent.com/libretro/retroarch-assets/master/xmb/monochrome/png/"

S, SZ = 6, 128
N = SZ * S

COMP = dict(merge=7, keep=0.03)                             # a computer: its keyboard becomes one recess
KEYS = dict(merge=7, keep=0.03, rect=True)                  # ... a plain rounded rectangle
FINE = dict(merge=0, keep=0.010)                            # a small pad: its few big details stay
SMALL = dict(merge=2, keep=0.012)

# (name of the file, libretro name of the system = the source icon, label, tuning, badge (word, x, y, size) in a recess)
SYSTEMS = [
    ("nes", "Nintendo - Nintendo Entertainment System", "NES", FINE, None),
    ("snes", "Nintendo - Super Nintendo Entertainment System", "SNES", {}, None),
    ("gb", "Nintendo - Game Boy", "Game Boy", {}, None),
    ("gbc", "Nintendo - Game Boy Color", "Game Boy Color", {}, None),
    ("gba", "Nintendo - Game Boy Advance", "Game Boy Advance", {}, None),
    ("md", "Sega - Mega Drive - Genesis", "Mega Drive", {}, None),
    ("sms", "Sega - Master System - Mark III", "Master System", {}, None),
    ("gg", "Sega - Game Gear", "Game Gear", {}, None),
    ("scd", "Sega - Mega-CD - Sega CD", "Sega CD", {}, ("CD", 0.50, 0.50, 0.55)),
    ("s32x", "Sega - 32X", "Sega 32X", {}, ("32X", 0.50, 0.50, 0.55)),
    ("pce", "NEC - PC Engine - TurboGrafx 16", "PC Engine", FINE, None),
    ("pcecd", "NEC - PC Engine CD - TurboGrafx-CD", "PC Engine CD", FINE, ("CD", 0.50, 0.80, 1.3)),
    ("neogeo", "SNK - Neo Geo", "Neo Geo", {}, None),
    ("ngp", "SNK - Neo Geo Pocket Color", "Neo Geo Pocket", {}, None),
    ("ws", "Bandai - WonderSwan", "WonderSwan", {}, None),
    ("wsc", "Bandai - WonderSwan Color", "WonderSwan Color", {}, None),
    ("a2600", "Atari - 2600", "Atari 2600", FINE, None),
    ("a5200", "Atari - 5200", "Atari 5200", SMALL, None),
    ("a7800", "Atari - 7800", "Atari 7800", {}, None),
    ("lynx", "Atari - Lynx", "Atari Lynx", {}, None),
    ("atarist", "Atari - ST", "Atari ST", KEYS, None),
    ("amiga", "Commodore - Amiga", "Amiga", KEYS, None),
    ("c64", "Commodore - 64", "Commodore 64", COMP, None),
    ("msx", "Microsoft - MSX", "MSX", KEYS, None),
    ("msx2", "Microsoft - MSX2", "MSX2", KEYS, None),
    ("zx", "Sinclair - ZX Spectrum", "ZX Spectrum", dict(merge=7, keep=0.03, rect=True, box=True), None),
    ("arcade", "FBNeo - Arcade Games", "Arcade", {}, None),
    ("mame", "MAME", "MAME", {}, None),
    ("coleco", "Coleco - ColecoVision", "ColecoVision", SMALL, None),
    ("vectrex", "GCE - Vectrex", "Vectrex", {}, None),
    ("psx", "Sony - PlayStation", "PlayStation", {}, None),
    ("psp", "Sony - PlayStation Portable", "PSP games", {}, None),
    ("psm", "Sony - PlayStation Mobile (PSN)", "PSM games", {}, None),
]


def source(db):
    path = os.path.join(SRC, db + ".png")
    if not os.path.exists(path):
        os.makedirs(SRC, exist_ok=True)
        print("downloading", db)
        with urllib.request.urlopen(BASE_URL + urllib.parse.quote(db + ".png"), timeout=30) as r:
            open(path, "wb").write(r.read())
    return path


class Masks:
    """the body (silhouette) and dent (recessed details) of an icon, supersampled 6x"""

    def __init__(self, body, dent):
        self.body, self.dent = body, dent

    def rr(self, x0, y0, x1, y1, r):
        ImageDraw.Draw(self.dent).rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, fill=255)

    def text_hole(self, s, cx, cy, size):
        f = ImageFont.truetype(FONT, int(size * S))
        try:
            f.set_variation_by_name("Bold")
        except Exception:
            pass
        cut = Image.new("L", (N, N), 0)
        d = ImageDraw.Draw(cut)
        bb = d.textbbox((0, 0), s, font=f)
        d.text((cx * S - (bb[0] + bb[2]) / 2, cy * S - (bb[1] + bb[3]) / 2), s, font=f, fill=255)
        self.dent = ImageChops.subtract(self.dent, cut)


def render(m, fit=(104, 98)):
    """glassy grey body, bevelled rim, gloss, darker recesses -> a 128 px RGBA image"""
    body = m.body
    bb = body.point(lambda v: 255 if v > 40 else 0).getbbox()
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    k = min(fit[0] * S / w, fit[1] * S / h, 1.6)

    def place(img):
        c = img.crop(bb)
        c = c.resize((max(1, round(c.width * k)), max(1, round(c.height * k))), Image.LANCZOS)
        out = Image.new("L", (N, N), 0)
        out.paste(c, ((N - c.width) // 2, (N - c.height) // 2))
        return out

    body, dent = place(body), place(ImageChops.multiply(m.dent, m.body.point(lambda v: 255 if v else 0)))
    dent = ImageChops.subtract(dent, ImageChops.subtract(Image.new("L", (N, N), 255), body))
    body = body.filter(ImageFilter.GaussianBlur(S * 0.30))
    a = np.asarray(body, np.float32) / 255.0
    yy = np.linspace(0, 1, N, dtype=np.float32)[:, None]
    top, bot = np.array([252, 253, 255], np.float32), np.array([204, 208, 222], np.float32)
    col = np.broadcast_to(top[None, None, :] * (1 - yy[..., None]) + bot[None, None, :] * yy[..., None], (N, N, 3)).copy()
    alpha = a * 0.95
    gl = np.clip(1.0 - yy / 0.55, 0, 1) ** 1.6                                   # the gloss on the upper half
    col = col * (1 - a[..., None] * gl[..., None] * 0.35) + 255.0 * a[..., None] * gl[..., None] * 0.35
    e = max(1, int(S * 1.5))                                                     # bevel: bright rim top/left, shadowed rim bottom/right
    up = np.asarray(ImageChops.offset(body, e, e), np.float32) / 255.0
    dn = np.asarray(ImageChops.offset(body, -e, -e), np.float32) / 255.0
    blur = lambda x: np.asarray(Image.fromarray((x * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(S * 0.4)), np.float32) / 255.0
    hi, lo = blur(np.clip(a - up, 0, 1)), blur(np.clip(a - dn, 0, 1))
    col = col * (1 - hi[..., None] * 0.9) + 255.0 * hi[..., None] * 0.9
    alpha = np.maximum(alpha, hi)
    col = col * (1 - lo[..., None] * 0.45) + np.array([120, 126, 150], np.float32) * lo[..., None] * 0.45
    alpha = np.maximum(alpha, lo * 0.97)
    dsoft = dent.filter(ImageFilter.GaussianBlur(S * 0.30))                       # recesses: darker, with a light lip on the lower inner edge
    dm = np.asarray(dsoft, np.float32) / 255.0
    lip = np.clip(dm - np.asarray(ImageChops.offset(dsoft, 0, -e), np.float32) / 255.0, 0, 1)
    shade = np.clip(dm - np.asarray(ImageChops.offset(dsoft, 0, e), np.float32) / 255.0, 0, 1)
    col = col * (1 - dm[..., None] * 0.80) + np.array([116, 124, 158], np.float32) * dm[..., None] * 0.80
    col = col * (1 - shade[..., None] * 0.35) + np.array([60, 66, 92], np.float32) * shade[..., None] * 0.35
    col = col * (1 - lip[..., None] * 0.85) + 255.0 * lip[..., None] * 0.85
    alpha = np.maximum(alpha, dm * 0.93 * a.clip(0, 1))
    rgba = np.dstack([col, alpha * 255]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA").resize((SZ, SZ), Image.LANCZOS)


def icon(path, badge=None, keep=0.02, smooth=1.0, merge=3, rect=False, seal=0, box=False):
    a = np.asarray(Image.open(path).convert("RGBA").getchannel("A"), np.float32) / 255.0
    solid = a > 0.5
    filled = ndi.binary_fill_holes(solid)
    holes = filled & ~solid                                  # the enclosed gaps of the outline: screens, keys, buttons

    def disk(r):
        yy, xx = np.ogrid[-r:r + 1, -r:r + 1]
        return (xx * xx + yy * yy) <= r * r

    if merge:                                                # details close together (the keys of a keyboard, a button group) become one recess
        holes = ndi.binary_fill_holes(ndi.binary_closing(holes, structure=disk(merge))) & filled
        holes = ndi.binary_opening(holes, structure=disk(merge))
    lab, n = ndi.label(holes)
    if n:                                                    # a hole under this share of the body is a detail the PSP's icons would not draw
        sizes = ndi.sum(holes, lab, range(1, n + 1))
        limit = max(6, keep * filled.sum())
        for i, s in enumerate(sizes, 1):
            if s < limit:
                holes[lab == i] = False
    if seal:
        filled = ndi.binary_closing(filled, structure=disk(seal))
    if smooth:                                               # a chunkier outline: blur and cut again
        filled = ndi.gaussian_filter(filled.astype(np.float32), smooth) > 0.5
        holes = ndi.gaussian_filter(holes.astype(np.float32), 1.8) > 0.5
        holes &= filled
    if rect and holes.any():                                 # the biggest recess becomes a plain rounded rectangle (a keyboard)
        lab2, n2 = ndi.label(holes)
        big = 1 + int(np.argmax(ndi.sum(holes, lab2, range(1, n2 + 1))))
        ys, xs = np.nonzero(lab2 == big)
        r = Image.new("L", (256, 256), 0)
        ImageDraw.Draw(r).rounded_rectangle([int(xs.min()) + 1, int(ys.min()) + 1, int(xs.max()) - 1, int(ys.max()) - 1], radius=7, fill=255)
        holes = (np.asarray(r) > 127) & (filled | box)
    if box:                                                  # a plain rounded body, with the gap under the top strip kept as a groove
        ys, xs = np.nonzero(filled)
        x0, x1, y0, y1 = int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())
        b = Image.new("L", (256, 256), 0)
        ImageDraw.Draw(b).rounded_rectangle([x0, y0, x1, y1], radius=9, fill=255)
        col = solid[:, x0 + int(0.62 * (x1 - x0))]
        gy = [y for y in range(y0 + 3, y1) if col[y - 1] and not col[y]]
        filled = np.asarray(b) > 127
        if gy:
            holes[gy[0] - 1:gy[0] + 3, x0 + 6:x1 - 5] = True
            sl = Image.new("L", (256, 256), 0)               # the Spectrum's rainbow: three slashes on the top strip
            for i in range(3):
                xs0 = x1 - 52 + i * 14
                ImageDraw.Draw(sl).line([(xs0, gy[0] - 5), (xs0 + 12, y0 + 5)], fill=255, width=5)
            holes |= np.asarray(sl) > 127
    soft = lambda x: Image.fromarray((np.clip(ndi.gaussian_filter(x.astype(np.float32), 0.6), 0, 1) * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC)
    m = Masks(soft(filled), soft(holes))
    if badge:                                                # a small word in a recess, to tell a variant from its sibling (CD, 32X)
        ys, xs = np.nonzero(filled)
        x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
        k = N / 256.0
        word, fx, fy, sc = badge
        cx, cy, h = (x0 + (x1 - x0) * fx) * k / S, (y0 + (y1 - y0) * fy) * k / S, (y1 - y0) * k / S * sc
        wd = h * 0.17 * len(word) * 0.62 + h * 0.10
        m.rr(cx - wd, cy - h * 0.13, cx + wd, cy + h * 0.13, h * 0.06)
        m.text_hole(word, cx, cy, h * 0.19)
    return render(m)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, db, label, tune, badge in SYSTEMS:
        im = icon(source(db), badge=badge, **tune)
        im.save(os.path.join(OUT, "tex_sys_%s.png" % name))
        print("tex_sys_%s.png  %s" % (name, label))
    print(len(SYSTEMS), "icons in", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
