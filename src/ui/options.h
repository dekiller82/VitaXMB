#pragma once

/* ---- Options panel (triangle) ---- */
enum { OPT_START, OPT_INFO, OPT_REFRESH, OPT_NEWFOLDER, OPT_SELECT, OPT_RENAME, OPT_DELFOLDER, OPT_REMOVE };
static const char *opt_names[8] = { "Start", "Information", "Refresh List", "New Folder", "Select Games",
                                    "Rename Folder", "Delete Folder", "Remove from Folder" };

/* The panel at the right edge (Options, and the chooser): a theme's own picture when it has one (system_plugin.rco: tex_optionmenu_base,
 * a one-pixel row stretched over the panel), else the sky colours darkened. */
static void draw_side_panel(float px, int a, const Palette *pal)
{
	vita2d_texture *base = res_theme("system_plugin", "tex_optionmenu_base");
	if (base) {
		vita2d_draw_texture_tint_scale(base, px, 0, (SCREEN_W - px) / (float)vita2d_texture_get_width(base), SCREEN_H / (float)vita2d_texture_get_height(base), WHITE(a));
		return;
	}
	unsigned int ct = RGBA8(pal->sky_top[0] * 7 / 10, pal->sky_top[1] * 6 / 10, pal->sky_top[2] * 4 / 10, a * 90 / 100);
	unsigned int cb = RGBA8(pal->sky_top[0] * 5 / 10, pal->sky_top[1] * 4 / 10, pal->sky_top[2] * 3 / 10, a * 92 / 100);
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (v) {
		v[0] = (vita2d_color_vertex){ px, 0, 0.5f, ct };
		v[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, ct };
		v[2] = (vita2d_color_vertex){ px, SCREEN_H, 0.5f, cb };
		v[3] = (vita2d_color_vertex){ SCREEN_W, SCREEN_H, 0.5f, cb };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	}
	vita2d_draw_rectangle(px, 0, 2, SCREEN_H, RGBA8(255, 255, 255, a * 30 / 100));
}

static void draw_options_panel(float t, const int *ids, int n, int sel, const Palette *pal, int ctx)
{
	if (t <= 0.01f) return;
	float e = ease_out(t);
	float px = clampf(640.0f + pt_opt[ctx].dx, 480.0f, 760.0f) + (1.0f - e) * 330.0f;
	int a = (int)(255 * e);
	if (!pt_opt_hidden[ctx]) draw_side_panel(px, a, pal);
	const float os = pt_opt[ctx].scale > 0.1f ? pt_opt[ctx].scale : 1.0f, row = 40.0f * os, fs = 28.0f * os * pt_opt_k;
	float y0 = 353.0f + pt_opt[ctx].dy - (n - 1) * row / 2.0f;
	for (int i = 0; i < n; i++) {
		float y = y0 + i * row;
		if (i == sel) vita2d_draw_rectangle(px + 3, y - row / 2 + 1, SCREEN_W - px - 3, row - 2, RGBA8(255, 255, 255, a * 20 / 100));
		ptext_vc(px + 11, y, WHITE(i == sel ? a : a * 78 / 100), ptext_width(fs, opt_names[ids[i]]) > SCREEN_W - px - 22 ? fs * 0.86f : fs, opt_names[ids[i]]);
		if (ids[i] == OPT_START && i == sel) {
			float bx = px + 11 + ptext_width((unsigned)fs, "Start") + 12;
			vita2d_draw_rectangle(bx, y - 11, 66, 22, RGBA8(0, 0, 0, a * 55 / 100));
			ptext_vc(bx + 6, y, WHITE(a), 17, "START");
		}
	}
}
