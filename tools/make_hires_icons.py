#!/usr/bin/env python3
"""Makes the 2x XMB icons in assets/icons (128 px; the 28 px tex_badge becomes 56) from the originals in tools/icons64.

    python tools/make_hires_icons.py

The Vita screen is twice the PSP's, so twice the PSP's size is what the icons need to look sharp. The originals are glass-like: the body is
about 89% opaque with brighter, fully opaque bevel rims. So only the outline is re-cut at the higher resolution (at the contour
where the alpha is half of its local plateau); the opacity, the rims and the colour gradients are smooth upscales of the original.
Works at 4x and averages down to 128 px for clean antialiasing.
Shrunk back to 64 px the result differs from the original by less than 2 out of 255 on average.
"""
import glob
import os
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "icons64")
OUT = os.path.join(HERE, "..", "assets", "icons")
F = 4


def up(a):
    return ndi.zoom(a, F, order=3, mode="nearest", grid_mode=True)


def sstep(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def fit_circle(al):
    """centre and radius (source pixels) of the round outline of a picture: along rays from the middle of its box, where the
    alpha first rises to half of what it reaches two pixels further in; then a least-squares circle through those points"""
    ys, xs = np.nonzero(al > 0.05)
    cx, cy = (xs.min() + xs.max()) / 2.0 + .5, (ys.min() + ys.max()) / 2.0 + .5
    R = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2.0 + 3
    pts = []
    for t in np.linspace(0, 2 * np.pi, 72, endpoint=False):
        rr = np.arange(R, 0, -0.1)
        v = ndi.map_coordinates(al, [cy + rr * np.sin(t) - .5, cx + rr * np.cos(t) - .5], order=1, mode="constant")
        i = np.argmax(v > 0.05)
        if v[i] <= 0.05: continue
        r0 = rr[i]
        plat = v[i:i + 20].max()
        j = i + np.argmax(v[i:] >= plat * .5)
        pts.append((cx + rr[j] * np.cos(t), cy + rr[j] * np.sin(t)))
    P = np.array(pts)
    A = np.c_[2 * P, np.ones(len(P))]
    sol = np.linalg.lstsq(A, (P ** 2).sum(1), rcond=None)[0]
    return sol[0], sol[1], float(np.sqrt(sol[2] + sol[0] ** 2 + sol[1] ** 2))


def hires(im, k=3.0, round_outline=False):
    a = np.asarray(im.convert("RGBA"), np.float32) / 255
    al = a[..., 3]
    pm = a[..., :3] * al[..., None]
    A = np.clip(up(al), 0, 1)
    M = ndi.gaussian_filter(ndi.maximum_filter(A, size=2 * F + 1), F * .5)       # local plateau opacity
    ratio = A / np.maximum(M, 1e-3)
    cov = sstep((ratio - 0.5) * k + 0.5)                                         # crisp outline
    core = ndi.gaussian_filter(ndi.minimum_filter((ratio > 0.5).astype(np.float32), size=2 * F + 1), F * .5)
    if round_outline:
        # the outer edge is a circle: take it from a fitted one, and the recut contour only inside it (the cut-out)
        cx, cy, r = fit_circle(al)
        yy, xx = (np.mgrid[0:A.shape[0], 0:A.shape[1]] + .5) / F
        d = np.hypot(xx - cx, yy - cy)
        circ = np.clip((r - d) * F + .5, 0, 1)
        w = np.clip((r - 1.8 - d) * 1.5, 0, 1)                                    # 0 at the rim, 1 inside
        cov = circ * (1 - w * (1 - cov))
    A2 = cov * (core * A + (1 - core) * M)                                       # keep the glass opacity inside
    C = np.stack([up(pm[..., c]) for c in range(3)], -1) / np.maximum(A[..., None], 1e-3)
    C = np.clip(C, 0, 1)
    # average down in premultiplied form so edges do not pick up a halo
    P = np.dstack([C * A2[..., None], A2])
    n = F // 2                                                                   # 4x down to 2x
    h, w = P.shape[0] // n, P.shape[1] // n
    P = P.reshape(h, n, w, n, 4).mean((1, 3))
    al2 = P[..., 3]
    rgb = np.clip(P[..., :3] / np.maximum(al2[..., None], 1e-3), 0, 1)
    return Image.fromarray((np.dstack([rgb, al2]) * 255 + .5).astype(np.uint8), "RGBA")


if __name__ == "__main__":
    for f in sorted(glob.glob(os.path.join(SRC, "tex_*.png"))):
        hires(Image.open(f), round_outline=os.path.basename(f) == "tex_badge.png").save(os.path.join(OUT, os.path.basename(f)))
        print(os.path.basename(f))
