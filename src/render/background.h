#pragma once

/* ------------------------------------------------------------------ */
/* Background                                                          */
/* ------------------------------------------------------------------ */

/* The firmware's picture for a month as a texture (made on first use). */
static vita2d_texture *month_sky_tex(int mi)
{
	static vita2d_texture *tex[12];
	if (mi < 0 || mi > 11) mi = 0;
	if (!tex[mi]) {
		unsigned char rgba[SKY_TEX_W * SKY_TEX_H * 4];
		for (int i = 0; i < SKY_TEX_W * SKY_TEX_H; i++) {
			rgba[i * 4] = sky_months[mi][i * 3]; rgba[i * 4 + 1] = sky_months[mi][i * 3 + 1];
			rgba[i * 4 + 2] = sky_months[mi][i * 3 + 2]; rgba[i * 4 + 3] = 255;
		}
		tex[mi] = texture_from_rgba(rgba, SKY_TEX_W, SKY_TEX_H);
	}
	return tex[mi];
}

#define SKY_VEIL 0.18f                          /* white laid over the month picture */
typedef struct { unsigned char sky_top[3], sky_bot[3], wave_top[3], wave_bot[3]; } Palette;

/* October is measured from a real PSP XMB capture; other months derive from the older table
 * (sky darker than the water, like the PSP themes) until their captures are available. */
static Palette get_palette(int month)
{
	Palette p;
	if (pt_have_colors) {                          /* a PSP theme sets its own colors */
		memcpy(p.sky_top, pt_sky_top, 3); memcpy(p.sky_bot, pt_sky_bot, 3);
		memcpy(p.wave_top, pt_wave_top, 3); memcpy(p.wave_bot, pt_wave_bot, 3);
		return p;
	}
	if (month < 0 || month > 11) month = 0;
	/* the firmware's own picture for the month, as a theme's 01-12.bmp would be: top and bottom rows give the sky,
	 * the wave takes a paler version of the bottom, under the thin white veil the sky is drawn with */
	const unsigned char *px = sky_months[month];
	for (int i = 0; i < 3; i++) {
		long t = 0, b = 0;
		for (int y = 0; y < 3; y++)
			for (int x = 0; x < SKY_TEX_W; x++) {
				t += px[(y * SKY_TEX_W + x) * 3 + i];
				b += px[((SKY_TEX_H - 1 - y) * SKY_TEX_W + x) * 3 + i];
			}
		float top = (float)t / (3 * SKY_TEX_W), bot = (float)b / (3 * SKY_TEX_W);
		top += (255.0f - top) * SKY_VEIL;
		bot += (255.0f - bot) * SKY_VEIL;
		p.sky_top[i] = (unsigned char)top;
		p.sky_bot[i] = (unsigned char)bot;
		p.wave_top[i] = (unsigned char)(bot + (255.0f - bot) * 0.10f);
		p.wave_bot[i] = (unsigned char)(bot + (255.0f - bot) * 0.28f);
	}
	return p;
}

/* One strip between two wave-shaped lines, vertex colours blended from c0 to c1. */
static float bg_alpha = 1.0f;
static int bg_no_wave;                          /* the start-up dissolve draws the wave itself */
static unsigned int bga(unsigned int c)
{
	unsigned int a = (unsigned int)((c >> 24) * bg_alpha);
	return (c & 0x00FFFFFFu) | (a << 24);
}

static void wave_strip(float base0, float base1, float t, const float p[3], unsigned int c0, unsigned int c1)
{
	const int step = 24;
	int cols = SCREEN_W / step + 2;
	vita2d_color_vertex *v = vita2d_pool_memalign(cols * 2 * sizeof(*v), sizeof(*v));
	if (!v) return;
	for (int i = 0; i < cols; i++) {
		float x = (float)(i * step);
		float w = p[0] * sinf(x * p[1] + t * p[2]) + p[0] * 0.35f * sinf(x * p[1] * 2.3f + 1.7f - t * p[2] * 0.7f);
		v[i * 2].x = x;     v[i * 2].y = base0 + w;     v[i * 2].z = 0.5f; v[i * 2].color = bga(c0);
		v[i * 2 + 1].x = x; v[i * 2 + 1].y = base1 + w; v[i * 2 + 1].z = 0.5f; v[i * 2 + 1].color = bga(c1);
	}
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, cols * 2);
}

static void draw_background(float t, int month)
{
	if (pt_active >= 0) pt_set_month(month);                  /* a theme has a sky picture for every month */
	Palette pal = get_palette(month);
	unsigned int ct = RGBA8(pal.sky_top[0], pal.sky_top[1], pal.sky_top[2], 255);
	unsigned int cb = RGBA8(pal.sky_bot[0], pal.sky_bot[1], pal.sky_bot[2], 255);
	if (pt_wall) {                                 /* a theme wallpaper replaces the sky and the waves */
		vita2d_draw_texture_tint_scale(pt_wall, 0, 0, SCREEN_W / (float)vita2d_texture_get_width(pt_wall),
		                               SCREEN_H / (float)vita2d_texture_get_height(pt_wall), RGBA8(255, 255, 255, (int)(255 * bg_alpha)));
		return;
	}
	if (pt_sky)
		vita2d_draw_texture_tint_scale(pt_sky, 0, 0, SCREEN_W / (float)vita2d_texture_get_width(pt_sky),
		                               SCREEN_H / (float)vita2d_texture_get_height(pt_sky), RGBA8(255, 255, 255, (int)(255 * bg_alpha)));
	if (!pt_sky) {                                 /* the firmware's picture for the month, stretched over the screen */
		int mi = month < 0 || month > 11 ? 0 : month;
		vita2d_texture *mtex = month_sky_tex(mi);
		if (mtex) {
			vita2d_draw_texture_tint_scale(mtex, 0, 0, SCREEN_W / (float)SKY_TEX_W, SCREEN_H / (float)SKY_TEX_H, RGBA8(255, 255, 255, (int)(255 * bg_alpha)));
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, bga(WHITE((int)(255 * SKY_VEIL))));
		}
	}
	vita2d_color_vertex *g = NULL;
	if (g) {
		g[0] = (vita2d_color_vertex){ 0, 0, 0.5f, bga(ct) };
		g[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, bga(ct) };
		g[2] = (vita2d_color_vertex){ 0, 340, 0.5f, bga(cb) };
		g[3] = (vita2d_color_vertex){ SCREEN_W, 340, 0.5f, bga(cb) };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, g, 4);
		vita2d_draw_rectangle(0, 340, SCREEN_W, SCREEN_H - 340, bga(cb));
	}

	if (bg_no_wave) return;
	if (wave_m.ok) {                               /* the firmware's own moving wave (or the theme's) */
		static const unsigned char white[3] = { 255, 255, 255 };     /* the same model and material as the boot's ribbon: white, translucent, shaded by its folds */
		if (wave_off) return;
		if (bgs.ok) bgs_draw(bg_alpha); else wave_draw(t, white, bg_alpha);
		return;
	}
	unsigned int wt = RGBA8(pal.wave_top[0], pal.wave_top[1], pal.wave_top[2], 255);
	unsigned int wb = RGBA8(pal.wave_bot[0], pal.wave_bot[1], pal.wave_bot[2], 255);

	/* main wave: a solid body of "water" with a pale crest */
	static const float main_p[3] = { 24, 0.0046f, 0.20f };
	wave_strip(275, 400, t, main_p, wt, wb);
	wave_strip(400, SCREEN_H + 90, t, main_p, wb, wb);
	wave_strip(268, 276, t, main_p, WHITE(0), WHITE(78));
	wave_strip(276, 290, t, main_p, WHITE(78), WHITE(0));

	/* two faint ribbons deeper down */
	static const float p2[3] = { 20, 0.0061f, -0.16f };
	wave_strip(372, 384, t, p2, WHITE(0), WHITE(34));
	wave_strip(384, 430, t, p2, WHITE(34), WHITE(0));
	static const float p3[3] = { 16, 0.0079f, 0.24f };
	wave_strip(452, 460, t, p3, WHITE(0), WHITE(26));
	wave_strip(460, 500, t, p3, WHITE(26), WHITE(0));
}
