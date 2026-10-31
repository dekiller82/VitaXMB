"""Builds docs/fonts/rodin-db.woff2 for the project page from the font the app ships (assets/font.otf).

The browsers lay the OpenType/CFF original out with far too wide letters, so the Latin part is cut out and
rebuilt with TrueType outlines (same advance widths, no layout tables). Needs fontTools and brotli.

    python tools/make_web_font.py
"""
import os
import subprocess
import sys
import tempfile
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UNICODES = "U+0020-007E,U+00A0-00FF,U+2013,U+2014,U+2018,U+2019,U+201C,U+201D,U+2022,U+2026,U+2122,U+2190-2193,U+00B7,U+2713"
tmp = os.path.join(tempfile.gettempdir(), "vxmb_sub.otf")
subprocess.check_call([sys.executable, "-m", "fontTools.subset", os.path.join(ROOT, "assets", "font.otf"), "--unicodes=" + UNICODES,
                       "--layout-features=kern", "--drop-tables+=vhea,vmtx,VORG,BASE,GSUB", "--no-hinting", "--output-file=" + tmp])
src = TTFont(tmp)
order = src.getGlyphOrder()
gs = src.getGlyphSet()
glyf = {}
for n in order:
    pen = TTGlyphPen(gs)
    gs[n].draw(Cu2QuPen(pen, 1.0, reverse_direction=True))
    glyf[n] = pen.glyph()
f = TTFont()
f.setGlyphOrder(order)
for t in ("head", "hhea", "OS/2", "hmtx", "cmap", "name", "post"):
    f[t] = src[t]
f["maxp"] = newTable("maxp")
f["maxp"].tableVersion = 0x00010000
for k in ("maxZones", "maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs", "maxStackElements",
          "maxSizeOfInstructions", "maxComponentElements", "maxComponentDepth"):
    setattr(f["maxp"], k, 0)
f["maxp"].maxZones = 1
f["glyf"] = newTable("glyf")
f["glyf"].glyphs = glyf
f["glyf"].glyphOrder = order
f["loca"] = newTable("loca")
f["post"].formatType = 3.0
f["head"].glyphDataFormat = 0
f.flavor = "woff2"
out = os.path.join(ROOT, "docs", "fonts", "rodin-db.woff2")
os.makedirs(os.path.dirname(out), exist_ok=True)
f.save(out)
print("wrote", out, len(order), "glyphs")
