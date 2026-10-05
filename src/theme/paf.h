#pragma once

/*
 * paf.h - numbers of the PSP's UI engine (paf.prx, firmware 6.61) that VitaXMB uses.
 *
 * Read from the module's code, not measured:
 *
 * Easing (the "acc" of an animation step, and the mode of every property move the engine starts; 0xeb0f0):
 *   0 linear   1 sqrt(t)   2 t*t   3 (1 + sin((t - 0.5) * pi)) / 2
 *   4 a step every 1/60 s that covers a fixed fraction of what is left (what lists and the category bar use)
 *     fraction per step k(N), N = whole frames in the duration (the table and the formula at 0xeaf38 / 0xeb094):
 *     k(N) = 1 for N = 0 ... and 1 / (N * 0.2168 + 3.7656) from N = 15 on.
 *
 * List styles (XList, 0x10a57c): row pitch, the extra distance the first rows above and below the selected one keep
 * (the selected row sits at the list's origin), and how many rows show above and below. PSP pixels.
 *   Settings pages (sysconf_plugin) use 4, the game list (game_plugin) 11, the music list (music_browser) 6, the video list 8.
 */

typedef struct { float pitch, gap_above, gap_below; } XStyle;

enum { XS_COLUMN = 0, XS_SETTINGS = 4, XS_MUSIC = 6, XS_VIDEO = 8, XS_GAME = 11, XS_COUNT = 24 };

static const XStyle xstyles[XS_COUNT] = {
	[0] = { 65, 65, 0 },   [1] = { 65, 60, 0 },   [2] = { 65, 65, 0 },
	[4] = { 45, 10, 10 },  [5] = { 54, 13, 13 },  [6] = { 43, 17, 17 },  [7] = { 40, 45, 45 },  [8] = { 54, 13.0625f, 13 },
	[9] = { 45, 30.5f, 30.5f }, [10] = { 54, 13, 13 }, [11] = { 45, 30.5f, 30.5f }, [12] = { 56, 0, 0 },
	[15] = { 89, 0, 0 },   [16] = { 45, 30.5f, 30.5f }, [17] = { 54, 13, 13 }, [18] = { 54, 13, 13 }, [19] = { 54, 13, 13 },
	[20] = { 54, 13, 13 }, [21] = { 45, 30.5f, 30.5f }, [22] = { 60, 0, 0 },  [23] = { 53, 13, 13 },
};

/* offset (Vita pixels, down is positive) of the row k places from the selected one */
static float xl_row(const XStyle *s, int k)
{
	if (k == 0) return 0.0f;
	if (k > 0) return 2.0f * (k * s->pitch + s->gap_below);
	return -2.0f * (-k * s->pitch + s->gap_above);
}

/* the same for a fractional offset d (rows are moving): a straight line between the two nearest rows */
static float xl_offset(int style, float d)
{
	const XStyle *s = &xstyles[style >= 0 && style < XS_COUNT && xstyles[style].pitch > 0.0f ? style : 0];
	if (d < -4.0f) d = -4.0f;
	if (d > 4.99f) d = 4.99f;
	int k = (int)floorf(d);
	float f = d - (float)k, a = xl_row(s, k);
	return a + (xl_row(s, k + 1) - a) * f;
}

/* fraction of the remaining distance a mode-4 move covers in one 1/60 s step, for a move of ms milliseconds */
static float ease4_step(float ms)
{
	static const float k[15] = { 1.0f, 0.99502486f, 0.90497738f, 0.79051381f, 0.68728518f, 0.60422963f, 0.53763443f, 0.48426148f,
	                             0.43956041f, 0.40160641f, 0.37037036f, 0.34305319f, 0.31948879f, 0.29895365f, 0.28089887f };
	int n = (int)(ms * 60.0f / 1000.0f + 0.001f);
	if (n < 0) n = 0;
	return n < 15 ? k[n] : 1.0f / ((float)n * 0.216796875f + 3.765625f);
}

/* progress p (0..1) of an animation step of dur milliseconds through easing mode m */
static float ease_curve(float p, int m, float dur)
{
	if (p <= 0.0f) return 0.0f;
	if (p >= 1.0f) return 1.0f;
	switch (m) {
	case 1: return sqrtf(p);
	case 2: return p * p;
	case 3: return 0.5f * (1.0f + sinf((p - 0.5f) * 3.14159265f));
	case 4: return 1.0f - powf(1.0f - ease4_step(dur), p * dur * 0.06f);          /* frames elapsed = p * dur / (1000 / 60) */
	}
	return p;
}
