#!/usr/bin/env python3
"""Redraws the PSP video player's pictures (control icons, codec tags, search steps, help buttons) at the Vita's native size.

The firmware's msvideo_main_plugin.rco holds them as tiny bitmaps (an icon is 28 x 28 pixels): stretched to the Vita's 2x they are blurry.
Every picture is traced instead: the bitmap's coverage is enlarged smoothly 16 times, its edge is made crisp again (smoothstep around the
50 % line, which keeps one-pixel strokes such as the letters of "A-B") and the result is reduced to the Vita's pixel grid. Shadows and
glows are soft shapes already and are only enlarged smoothly.

  make_native_video.py <dir of stock pictures> <outdir>

<dir of stock pictures> holds the RCO's images as PNGs named "<index>_<label>.png": decode the firmware's msvideo_main_plugin.rco (RLZ-compressed;
tools/rlz.py and tools/make_flat_rco.py turn it into the flat layout) and save its GIM pictures that way. The result is committed in assets/psp/video,
so this is only needed to redo it. Writes <outdir>/<label>.png, plus <label>_shadow.png (black, alpha) for the icons that have one.
"""
import os, sys
import numpy as np
from PIL import Image

SCALE = 2          # Vita pixels per PSP pixel
UP = 16            # tracing resolution
LO, HI = 0.30, 0.70

# pictures drawn as crisp shapes (white, alpha)
CRISP = ['play', 'pause', 'stop', 'prev', 'next', 'cp_search_b', 'cp_search_f', 'slow', 'step', 'play_plus', 'play_minus',
         'jump', 'ssearch', 'cp_sound', 'cp_subtitle', 'change_screen_mode', 'cp_display', 'cp_help',
         'cp_abrepeat', 'repeat', 'title_repeat', 'folder_repeat', 'cp_clear', 'shuffle', 'Ato', 'B', 'speed_arrow',
         'search_b_1', 'search_b_2', 'search_b_3', 'search_f_1', 'search_f_2', 'search_f_3', 'down_arrow',
         'tex_mpg4', 'tex_avc', 'tex_mjpeg', 'tex_playing', 'title_icon']
# soft pictures (glows are white, shadows are masks)
GLOW = ['play_focus', 'pause_focus', 'stop_focus', 'prev_focus', 'next_focus', 'cp_search_b_focus', 'cp_search_f_focus', 'slow_focus', 'step_focus',
        'play_plus_focus', 'play_minus_focus', 'jump_focus', 'ssearch_focus', 'cp_sound_focus', 'cp_subtitle_focus', 'change_screen_mode_focus',
        'cp_display_focus', 'cp_help_focus', 'cp_abrepeat_focus', 'repeat_focus', 'cp_clear_focus', 'shuffle_focus', 'jump_select_focus']
SHADOW = [n + '_shadow' for n in CRISP + [g[:-6] for g in GLOW if g.endswith('_focus')]]


def load(dirname, label):
    for f in sorted(os.listdir(dirname)):
        if f.split('_', 1)[-1] == label + '.png':
            return Image.open(os.path.join(dirname, f)).convert('RGBA')
    return None


def crisp(im):
    a = im.getchannel('A')
    w, h = a.size
    v = np.asarray(a.resize((w * UP, h * UP), Image.BICUBIC), dtype=np.float32) / 255.0
    t = np.clip((v - LO) / (HI - LO), 0, 1)
    t = t * t * (3 - 2 * t)
    return Image.fromarray((t * 255).astype(np.uint8)).resize((w * SCALE, h * SCALE), Image.BOX)


def soft(im, mask):
    """glow: the alpha channel; shadow: the brightness is the alpha"""
    if mask:
        a = np.asarray(im.convert('RGB').convert('L'), dtype=np.float32)
        a = Image.fromarray(a.astype(np.uint8))
    else:
        a = im.getchannel('A')
    return a.resize((a.width * SCALE, a.height * SCALE), Image.BICUBIC)


def save(mask, path, rgb):
    out = Image.new('RGBA', mask.size, rgb + (0,))
    out.putalpha(mask)
    out.save(path)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    n = 0
    for label in CRISP:
        im = load(src, label)
        if im is not None:
            save(crisp(im), os.path.join(out, label + '.png'), (255, 255, 255)); n += 1
    for label in GLOW:
        im = load(src, label)
        if im is not None:
            save(soft(im, False), os.path.join(out, label + '.png'), (255, 255, 255)); n += 1
    for label in SHADOW:
        im = load(src, label)
        if im is not None:
            save(soft(im, True), os.path.join(out, label + '.png'), (0, 0, 0)); n += 1
    print('wrote', n, 'pictures to', out)


if __name__ == '__main__':
    main()
