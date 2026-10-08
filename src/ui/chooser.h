#pragma once

/* ---- chooser: Color and Custom Theme (the PSP's list on the right with a picture of the choice beside it) ---- */
static int calendar_month(void)
{
	SceDateTime d;
	sceRtcGetCurrentClockLocalTime(&d);
	return d.month >= 1 && d.month <= 12 ? d.month - 1 : 0;
}
#define theme_now_month calendar_month()

static int ch_open, ch_kind, ch_sel, ch_n, ch_theme_before;      /* kind 0: Custom Theme, 1: Color */
static float ch_t, ch_top;
static vita2d_texture *ch_prev[PT_MAX + 1];
static unsigned char ch_prev_tried[PT_MAX + 1];

static void ch_name(int i, char *out, size_t n)
{
	if (ch_kind == 1) snprintf(out, n, "%s", theme_names[i]);
	else if (i == 0) snprintf(out, n, "%s", "Off");
	else if (i == pt_n + 1) snprintf(out, n, "%s", "Random");        /* a theme picked at every start (CXMB's random.ctf) */
	else snprintf(out, n, "%.40s", pt_list[i - 1].name);
}

static int ch_current(void) { return ch_kind == 1 ? theme : (pt_random ? pt_n + 1 : pt_active + 1); }

static void ch_start(int kind)
{
	ch_kind = kind;
	if (kind == 0) pt_scan();
	for (int i = 0; i <= PT_MAX; i++) { if (ch_prev[i]) { defer_free(ch_prev[i]); ch_prev[i] = NULL; } ch_prev_tried[i] = 0; }
	ch_n = kind == 1 ? 13 : pt_n + (pt_n > 0 ? 2 : 1);
	ch_sel = ch_current();
	if (ch_sel < 0 || ch_sel >= ch_n) ch_sel = 0;
	ch_theme_before = theme;
	ch_top = 0.0f;
	ch_open = 1;
}

/* The picture shown for choice i: a theme's preview, or the sky of a colour. */
static vita2d_texture *ch_picture(int i)
{
	if (ch_kind == 1) {
		int mi = i == 0 ? theme_now_month : i - 1;
		return month_sky_tex(mi);
	}
	if (i == 0) return month_sky_tex(theme_now_month);
	if (i < 0 || i > PT_MAX || i > pt_n) return NULL;                 /* Random has no preview */
	if (!ch_prev_tried[i]) {
		ch_prev_tried[i] = 1;
		ch_prev[i] = pt_peek(i - 1);
	}
	return ch_prev[i];
}

static void ch_apply(void)
{
	if (ch_kind == 1) {
		theme = ch_sel;
		config_save();
	} else {
		pt_random = pt_n > 0 && ch_sel == pt_n + 1;
		if (ch_sel == 0) pt_unload(); else if (pt_random) pt_pick_random(); else pt_load(ch_sel - 1);
		pt_save_selection();
	}
	theme_item_update();                        /* the Settings rows show the new Color / Custom Theme name */
}

static void draw_chooser(float t, const Palette *pal)
{
	if (t <= 0.01f) return;
	float e = ease_out(t);
	int a = (int)(255 * e);
	/* the picture: large, left of the list */
	vita2d_texture *pic = ch_picture(ch_sel);
	const float pw = 600.0f, ph = 340.0f, pxl = 30.0f - (1.0f - e) * 80.0f, pyt = 102.0f;
	vita2d_draw_rectangle(pxl + 4, pyt + 5, pw, ph, RGBA8(0, 0, 0, a * 28 / 100));
	if (pic) {
		vita2d_draw_texture_tint_scale(pic, pxl, pyt, pw / (float)vita2d_texture_get_width(pic), ph / (float)vita2d_texture_get_height(pic), RGBA8(255, 255, 255, a));
		if (ch_kind == 1) vita2d_draw_rectangle(pxl, pyt, pw, ph, bga(WHITE((int)(a * SKY_VEIL))));
	} else {
		vita2d_draw_rectangle(pxl, pyt, pw, ph, RGBA8(0, 0, 0, a * 35 / 100));
		ptext_vc(pxl + pw / 2 - ptext_width(26, "No preview") / 2.0f, pyt + ph / 2, WHITE(a * 7 / 10), 26, "No preview");
	}
	/* the list on the right */
	float px = 660.0f + (1.0f - e) * 300.0f;
	draw_side_panel(px, a, pal);
	ptext_vc_fit(px + 14, 36, WHITE(a), 30, ch_kind == 1 ? "Color" : "Custom Theme (beta)", SCREEN_W - px - 28);
	vita2d_draw_rectangle(px + 10, 64, SCREEN_W - px - 20, 2, WHITE(a * 35 / 100));
	const float row = 40.0f, y0 = 100.0f;
	int visible = 9;
	if (ch_sel < ch_top) ch_top = (float)ch_sel;
	if (ch_sel > ch_top + visible - 1) ch_top = (float)(ch_sel - visible + 1);
	for (int i = 0; i < ch_n; i++) {
		float y = y0 + (i - ch_top) * row;
		if (y < y0 - 20.0f || y > y0 + visible * row - 10.0f) continue;
		char nm[48];
		ch_name(i, nm, sizeof(nm));
		int sel = i == ch_sel;
		if (sel) vita2d_draw_rectangle(px + 3, y - 19, SCREEN_W - px - 3, 38, RGBA8(255, 255, 255, a * 20 / 100));
		ptext_vc_fit(px + 26, y, WHITE(sel ? a : a * 78 / 100), 26, nm, SCREEN_W - px - 56);
		if (i == ch_current()) glyph_triangle(px + 13, y, 5, a);              /* the one in use */
	}
	if (ch_top > 0.5f) ptext_vc(px + 14, y0 - 30, WHITE(a * 6 / 10), 20, "...");
	if (ch_top + visible < ch_n) ptext_vc(px + 14, y0 + visible * row - 14, WHITE(a * 6 / 10), 20, "...");
	glyph_cross(px + 24, 506, 8, a);
	ptext_vc(px + 40, 506, BTN_LABEL(a), 22, "Select");
	glyph_ring(px + 150, 506, 8, a);
	ptext_vc(px + 166, 506, BTN_LABEL(a), 22, "Back");
}
