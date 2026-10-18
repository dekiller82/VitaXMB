#!/usr/bin/env python3
"""Redraws the PSP's music list pictures (number box, digits, codec pill and their shadows) at the Vita's native size.

The firmware's music_browser_plugin.rco holds them as tiny bitmaps (a pill is 72 x 14 pixels): drawn at the Vita's 2x they are
blurry. The shapes are simple, so they are rebuilt here as vector drawings, measured from the stock bitmaps:

  pill   body 51.75 x 12 PSP px with the label cut out of it, then two speaker arcs, all in white
  box    1 px outline, corner radius 4, a faint diagonal highlight inside, a glow ring (selected row) and a soft shadow
  digits 9 x 16 px glyphs (Source Sans 3, the closest free face to the PSP's artwork) with a blurred shadow

Every size below is in PSP pixels; the files are written at SCALE (2) times that, so one image pixel is one screen pixel.

  make_native_music.py <outdir>         writes <outdir>/music_*.png (needs Pillow and numpy, downloads Source Sans 3 once)

The constants were fitted against the stock pictures: the label and digits by least squares over font, weight, size and position,
the shadows by a blurred copy of the shape (sigma, peak, offset), the glow and the box shadow as a profile over the distance to the outline.

Source Sans 3 is (c) Adobe, SIL Open Font License 1.1.
"""
import math, os, sys, urllib.request
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SCALE = 2
SS = 8                                       # supersampling of every drawing
FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/sourcesans3/SourceSans3%5Bwght%5D.ttf"

# ---- measured from the stock bitmaps (PSP pixels) ----
PILL_W, PILL_H = 72, 14                      # tile
BODY = (7.0, 1.0, 58.75, 13.0)               # x0, y0, x1, y1 of the white body
BODY_R = 1.0
ARCS = ((57.4, 6.5, 4.1, 47.0), (57.4, 6.5, 7.5, 42.0))      # centre x, y, radius, half angle (degrees)
ARC_W = 1.3
LABEL_W, LABEL_SIZE, LABEL_CX, LABEL_BASE = 700, 13.5, 33.0, 11.5      # weight, size, centre x and baseline of the label
LABEL_MAXW = 40.0                            # a longer label is shrunk to this width
PILL_SH = (72, 22, 4, 1.6, 0.55, 0.0, 0.0)   # tile w, h, tile y of the pill, blur sigma, peak, shadow dx, dy

BOX = 34                                     # box tile
BOX_RECT = (1.0, 1.0, 33.0, 33.0)            # outer edge of the outline
BOX_R, BOX_LINE = 4.0, 1.0
BACK = (43.2, 0.57, 0.733)                   # highlight: a - b*x - c*y (of 255), white, inside the box
FOCUS = 78                                   # glow tile
SHADOW = 52                                  # box shadow tile
# glow and shadow as a function of the distance to the box outline (negative inside), in PSP px: (distance, value 0..1)
FOCUS_PROFILE = ((0, 0.85), (1, 0.785), (2, 0.697), (3, 0.591), (4, 0.503), (5, 0.39), (6, 0.307), (7, 0.217), (8, 0.155), (9, 0.109),
                 (10, 0.071), (11, 0.048), (12, 0.031), (13, 0.021), (14, 0.013), (15, 0.006), (16, 0.003), (17, 0.0))
SHADOW_PROFILE = ((-6, 0.0), (-5, 0.009), (-4, 0.032), (-3, 0.077), (-2, 0.133), (-1, 0.181), (0, 0.218), (1, 0.158), (2, 0.103), (3, 0.054),
                  (4, 0.022), (5, 0.005), (6, 0.0))
DIGIT_W, DIGIT_H = 9, 16
DIGIT_FONT = (600, 19.0, 4.5, 14.0, 0.85)    # weight, size, centre x, baseline, horizontal squeeze (the PSP's digits are narrow)
DIGIT_SH = (36, 36, 2.0, 0.93, 0.0, -1.0)     # tile w, h, blur sigma, peak, shadow dx, dy (the digit sits in the middle of the tile)

LABELS = ("MP3", "FLAC", "OGG", "WAV", "AIFF")


def font(weight, size):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "SourceSans3.ttf")
    if not os.path.exists(path):
        urllib.request.urlretrieve(FONT_URL, path)
    f = ImageFont.truetype(path, int(round(size * SS * SCALE)))
    f.set_variation_by_axes([weight])
    return f


def canvas(w, h):
    return Image.new("L", (w * SS * SCALE, h * SS * SCALE), 0)


def finish(mask, w, h):
    """a supersampled mask -> an array of 0..1 at the output size"""
    return np.array(mask.resize((w * SCALE, h * SCALE), Image.BOX), dtype=np.float32) / 255.0


def to_png(path, alpha, rgb=255):
    a = np.clip(alpha, 0, 1)
    img = np.zeros(a.shape + (4,), np.uint8)
    img[..., :3] = rgb
    img[..., 3] = np.round(a * 255).astype(np.uint8)
    Image.fromarray(img, "RGBA").save(path)


def u(v):                                    # PSP pixels -> supersampled canvas pixels
    return v * SS * SCALE


def rrect(dr, r, rad, fill=255, width=None):
    box = [u(r[0]), u(r[1]), u(r[2]) - 1, u(r[3]) - 1]
    if width:
        dr.rounded_rectangle(box, radius=u(rad), outline=fill, width=int(round(u(width))))
    else:
        dr.rounded_rectangle(box, radius=u(rad), fill=fill)


def arcs(dr, fill=255):
    for cx, cy, r, ang in ARCS:
        dr.arc([u(cx - r), u(cy - r), u(cx + r), u(cy + r)], -ang, ang, fill=fill, width=int(round(u(ARC_W))))
        for s in (-1, 1):                    # round caps
            a = math.radians(s * ang)
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            dr.ellipse([u(x - ARC_W / 2), u(y - ARC_W / 2), u(x + ARC_W / 2), u(y + ARC_W / 2)], fill=fill)


def pill_silhouette():
    m = canvas(PILL_W, PILL_H)
    dr = ImageDraw.Draw(m)
    rrect(dr, BODY, BODY_R)
    arcs(dr)
    return m


def blurred(mask, sigma, w, h, peak, dx, dy):
    img = mask.filter(ImageFilter.GaussianBlur(sigma * SS * SCALE))
    arr = finish(img, w, h) * peak
    if dx or dy:
        sh = np.zeros_like(arr)
        ox, oy = int(round(dx * SCALE)), int(round(dy * SCALE))
        ys, xs = arr.shape
        sh[max(oy, 0):ys + min(oy, 0), max(ox, 0):xs + min(ox, 0)] = arr[max(-oy, 0):ys - max(oy, 0), max(-ox, 0):xs - max(ox, 0)]
        arr = sh
    return arr


def make_pill(label):
    m = pill_silhouette()
    dr = ImageDraw.Draw(m)
    f = font(LABEL_W, LABEL_SIZE)
    w = dr.textlength(label, font=f) / (SS * SCALE)
    if w > LABEL_MAXW:
        f = font(LABEL_W, LABEL_SIZE * LABEL_MAXW / w)
    dr.text((u(LABEL_CX), u(LABEL_BASE)), label, font=f, fill=0, anchor="ms")       # the label is a hole in the body
    return finish(m, PILL_W, PILL_H)


def make_pill_shadow():
    tw, th, ty, sigma, peak, dx, dy = PILL_SH
    m = canvas(tw, th)
    big = pill_silhouette()
    m.paste(big, (0, int(u(ty))))
    return blurred(m, sigma, tw, th, peak, dx, dy)


def box_distance(w, h, cx, cy):
    """distance (PSP px, negative inside) from every output pixel centre to the outline of the rounded box centred on (cx, cy)"""
    x0, y0, x1, y1 = BOX_RECT
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    ys, xs = np.mgrid[0:h * SCALE, 0:w * SCALE]
    px = (xs + 0.5) / SCALE - cx
    py = (ys + 0.5) / SCALE - cy
    qx, qy = np.abs(px) - (hx - BOX_R), np.abs(py) - (hy - BOX_R)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - BOX_R


def profile(tab, d):
    xs = [t[0] for t in tab]
    ys = [t[1] for t in tab]
    return np.interp(d, xs, ys, left=ys[0], right=ys[-1])


def make_box():
    m = canvas(BOX, BOX)
    rrect(ImageDraw.Draw(m), BOX_RECT, BOX_R, width=BOX_LINE)
    return finish(m, BOX, BOX)


def make_back():
    ys, xs = np.mgrid[0:BOX * SCALE, 0:BOX * SCALE]
    px, py = (xs + 0.5) / SCALE, (ys + 0.5) / SCALE
    a, b, c = BACK
    v = np.clip((a - b * px - c * py) / 255.0, 0, 1)
    inside = box_distance(BOX, BOX, BOX / 2, BOX / 2) < -0.5
    return v * inside


def make_focus():
    d = box_distance(FOCUS, FOCUS, FOCUS / 2, FOCUS / 2)
    return np.where(d > 0, profile(FOCUS_PROFILE, d), 0.0)


def make_shadow():
    return profile(SHADOW_PROFILE, box_distance(SHADOW, SHADOW, SHADOW / 2, SHADOW / 2))


def digit_mask(ch):
    wgt, size, cx, base, xs = DIGIT_FONT
    wide = canvas(DIGIT_W * 2, DIGIT_H)
    ImageDraw.Draw(wide).text((u(DIGIT_W), u(base)), ch, font=font(wgt, size), fill=255, anchor="ms")
    wide = wide.resize((int(round(wide.width * xs)), wide.height), Image.BICUBIC)
    m = canvas(DIGIT_W, DIGIT_H)
    m.paste(wide, (int(round(u(cx) - wide.width / 2)), 0))
    return m


def make_digit(ch):
    return finish(digit_mask(ch), DIGIT_W, DIGIT_H)


def make_digit_shadow(ch):
    tw, th, sigma, peak, dx, dy = DIGIT_SH
    m = canvas(tw, th)
    m.paste(digit_mask(ch), (int(u((tw - DIGIT_W) / 2)), int(u((th - DIGIT_H) / 2))))
    return blurred(m, sigma, tw, th, peak, dx, dy)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for lab in LABELS:
        to_png(os.path.join(out, "music_pill_%s.png" % lab.lower()), make_pill(lab))
    to_png(os.path.join(out, "music_pill_shadow.png"), make_pill_shadow(), 0)
    to_png(os.path.join(out, "music_box.png"), make_box())
    to_png(os.path.join(out, "music_box_back.png"), make_back())
    to_png(os.path.join(out, "music_box_focus.png"), make_focus())
    to_png(os.path.join(out, "music_box_shadow.png"), make_shadow(), 0)
    for i in range(10):
        to_png(os.path.join(out, "music_digit_%d.png" % i), make_digit(str(i)))
        to_png(os.path.join(out, "music_digit_%d_shadow.png" % i), make_digit_shadow(str(i)), 0)
    print("wrote", out)


if __name__ == "__main__":
    main()
