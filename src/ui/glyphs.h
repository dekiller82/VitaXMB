#pragma once

/* ---- PSP button glyphs: a theme's own pictures (system_plugin_fg.rco), else drawn from primitives (the font has none) ---- */
static int glyph_tex(const char *label, float cx, float cy, float r, int a)
{
	vita2d_texture *t = res_theme("system_plugin_fg", label);
	if (!t) return 0;
	float w = (float)vita2d_texture_get_width(t), h = (float)vita2d_texture_get_height(t), s = (2.0f * r + 2.0f) / (w > h ? w : h);
	char sl[40];
	snprintf(sl, sizeof(sl), "%s_shadow", label);                      /* tex_cross_shadow ...: centred, two pixels down and right */
	vita2d_texture *sh = res_theme("system_plugin_fg", sl);
	if (sh) vita2d_draw_texture_tint_scale(sh, cx + 2.0f * s - vita2d_texture_get_width(sh) * s / 2.0f, cy + 2.0f * s - vita2d_texture_get_height(sh) * s / 2.0f, s, s, WHITE(a));
	vita2d_draw_texture_tint_scale(t, cx - w * s / 2.0f, cy - h * s / 2.0f, s, s, BTN_ICON(a));
	return 1;
}

static void glyph_ring(float cx, float cy, float r, int a)
{
	if (glyph_tex("tex_circle", cx, cy, r, a)) return;
	for (int i = 0; i < 40; i++) {
		float ang = i * (6.2831853f / 40);
		vita2d_draw_fill_circle(cx + cosf(ang) * r, cy + sinf(ang) * r, 1.7f, WHITE(a));
	}
}
static void glyph_cross(float cx, float cy, float r, int a)
{
	if (glyph_tex("tex_cross", cx, cy, r, a)) return;
	for (int k = -1; k <= 1; k++) {
		vita2d_draw_line(cx - r, cy - r + k, cx + r, cy + r + k, WHITE(a));
		vita2d_draw_line(cx - r, cy + r + k, cx + r, cy - r + k, WHITE(a));
	}
}
static void glyph_triangle(float cx, float cy, float r, int a)
{
	if (glyph_tex("tex_triangle", cx, cy, r, a)) return;
	float x0 = cx - r, x1 = cx + r, x2 = cx, y0 = cy + r * 0.8f, y2 = cy - r;
	for (int k = 0; k < 2; k++) {
		vita2d_draw_line(x0, y0 + k, x1, y0 + k, WHITE(a));
		vita2d_draw_line(x0, y0 + k, x2, y2 + k, WHITE(a));
		vita2d_draw_line(x1, y0 + k, x2, y2 + k, WHITE(a));
	}
}
static void glyph_arrow_left(float cx, float cy, float h, int a)
{
	vita2d_texture *t = res_theme("system_plugin", "tex_arrow_left");          /* a theme's own arrow */
	if (t) {
		float s = h / (float)vita2d_texture_get_height(t);
		vita2d_texture *sh = res_theme("system_plugin", "tex_arrow_left_shadow");
		if (sh) vita2d_draw_texture_tint_scale(sh, cx + 2.0f * s - vita2d_texture_get_width(sh) * s / 2.0f, cy + 2.0f * s - vita2d_texture_get_height(sh) * s / 2.0f, s, s, WHITE(a));
		vita2d_draw_texture_tint_scale(t, cx - vita2d_texture_get_width(t) * s / 2.0f, cy - h / 2.0f, s, s, WHITE(a));
		return;
	}
	for (int i = 0; i < (int)h; i++) {
		float half = (i < h / 2 ? i : h - i);
		vita2d_draw_rectangle(cx - half * 0.7f, cy - h / 2 + i, half * 0.7f + 1, 1.0f, WHITE(a));
	}
}
