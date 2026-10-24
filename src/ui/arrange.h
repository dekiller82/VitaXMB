#pragma once

/* ---- Arrange Columns: reorder, rename, hide, give another picture, make and delete the columns of the bar ---- */
enum { AC_NONE, AC_CLOSE, AC_RENAME, AC_NEW, AC_DELETE };
static int ac_open, ac_sel, ac_grab;
static float ac_t, ac_top;
static int ac_rows[CAT_COUNT + 1], ac_n;        /* the column of each row (every column that exists, in bar order); the last row, -1, makes a new column */

#define AC_ROW_H 44.0f
#define AC_Y0    120.0f
#define AC_VIS   8

static int ac_exists(int c) { return c < CAT_C1 || col_custom[c]; }
static int ac_free_slot(void) { for (int c = CAT_C1; c < CAT_COUNT; c++) if (!col_custom[c]) return 1; return 0; }

static void ac_build(void)
{
	ac_n = 0;
	for (int k = 0; k < CAT_COUNT; k++) if (ac_exists(cat_order[k])) ac_rows[ac_n++] = cat_order[k];
	if (ac_free_slot()) ac_rows[ac_n++] = -1;
}

static void ac_start(void)
{
	ac_build();
	ac_sel = 0;
	for (int i = 0; i < ac_n; i++) if (ac_rows[i] == cat_now) ac_sel = i;
	ac_top = 0.0f;
	ac_grab = 0;
	ac_open = 1;
}

static void ac_select_col(int c)
{
	ac_build();
	for (int i = 0; i < ac_n; i++) if (ac_rows[i] == c) ac_sel = i;
}

/* the picture in use is the one at this index of col_icons (a column still on its own picture finds it there too) */
static int ac_icon_index(int c)
{
	if (col_icon[c] >= 0) return col_icon[c];
	for (int i = 0; i < n_col_icons; i++) if (col_icons[i] == col_tex0[c]) return i;
	return -1;
}

static void ac_save(void)
{
	cat_refresh_hidden(cat_now);
	columns_save();
	config_save();
}

static int ac_real_rows(void) { return ac_rows[ac_n - 1] < 0 ? ac_n - 1 : ac_n; }

/* Handles one frame of input; returns a request the main loop carries out (the keyboard, the delete question, closing). */
static int ac_input(unsigned pressed)
{
	int real = ac_real_rows();
	int c = ac_sel < ac_n ? ac_rows[ac_sel] : -1;
	if (pressed & SCE_CTRL_UP) {
		if (ac_sel > 0) {
			if (ac_grab && c >= 0) {                                         /* carrying the column up: it swaps places with its neighbour */
				int a = ac_rows[ac_sel - 1], ia = -1, ib = -1;
				for (int k = 0; k < CAT_COUNT; k++) { if (cat_order[k] == a) ia = k; if (cat_order[k] == c) ib = k; }
				if (ia >= 0 && ib >= 0) { cat_order[ia] = c; cat_order[ib] = a; }
				ac_build();
				cat_refresh_hidden(cat_now);
			}
			ac_sel--;
			sound_play(SND_CURSOR);
		}
	}
	if (pressed & SCE_CTRL_DOWN) {
		if (ac_sel < (ac_grab ? real - 1 : ac_n - 1)) {
			if (ac_grab && c >= 0) {
				int a = ac_rows[ac_sel + 1], ia = -1, ib = -1;
				for (int k = 0; k < CAT_COUNT; k++) { if (cat_order[k] == a) ia = k; if (cat_order[k] == c) ib = k; }
				if (ia >= 0 && ib >= 0) { cat_order[ia] = c; cat_order[ib] = a; }
				ac_build();
				cat_refresh_hidden(cat_now);
			}
			ac_sel++;
			sound_play(SND_CURSOR);
		}
	}
	c = ac_sel < ac_n ? ac_rows[ac_sel] : -1;
	if (pressed & SCE_CTRL_CROSS) {
		if (c < 0) { sound_play(SND_CONFIRM); return AC_NEW; }
		ac_grab = !ac_grab;
		if (!ac_grab) ac_save();                                               /* dropped: keep the new order */
		sound_play(SND_CURSOR);
	}
	if ((pressed & SCE_CTRL_SQUARE) && c >= 0) { ac_grab = 0; sound_play(SND_CONFIRM); return AC_RENAME; }
	if ((pressed & SCE_CTRL_TRIANGLE) && c >= 0 && cat_can_hide(c)) {
		cat_pref[c] = !cat_pref[c];
		ac_save();
		sound_play(SND_CURSOR);
	}
	if ((pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) && c >= 0 && n_col_icons) {
		int dir = (pressed & SCE_CTRL_RTRIGGER) ? 1 : -1, v = ac_icon_index(c);
		v = v < 0 ? 0 : (v + dir + n_col_icons) % n_col_icons;
		col_icon[c] = v;
		col_apply_icons();
		ac_save();
		sound_play(SND_CURSOR);
	}
	if ((pressed & SCE_CTRL_START) && c >= CAT_C1) { sound_play(SND_CONFIRM); return AC_DELETE; }
	if (pressed & SCE_CTRL_CIRCLE) { ac_grab = 0; ac_save(); sound_play(SND_CANCEL); return AC_CLOSE; }
	return AC_NONE;
}

static void glyph_shoulder(float cx, float cy, const char *letter, int a)
{
	vita2d_draw_rectangle(cx - 14, cy - 9, 28, 2, WHITE(a));
	vita2d_draw_rectangle(cx - 14, cy + 7, 28, 2, WHITE(a));
	vita2d_draw_rectangle(cx - 14, cy - 9, 2, 18, WHITE(a));
	vita2d_draw_rectangle(cx + 12, cy - 9, 2, 18, WHITE(a));
	ptext_vc(cx - ptext_width(16, letter) / 2.0f, cy, WHITE(a), 16, letter);
}

static void draw_arrange(float t)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(225 * t)));
	ptext_vc(60, 46, WHITE(a), 28, T("Arrange Columns"));
	const char *hint = ac_grab ? T("Up / Down: move   Cross: drop") : T("Cross: pick up and move");
	ptext_vc(900 - ptext_width(20, hint), 46, WHITE(a * 6 / 10), 20, hint);
	vita2d_draw_rectangle(50, 76, 860, 2, WHITE(a * 30 / 100));

	float want = ac_sel - 3.0f, maxtop = ac_n - (float)AC_VIS;
	if (want > maxtop) want = maxtop;
	if (want < 0.0f) want = 0.0f;
	ac_top = lerpf(ac_top, want, 0.3f);
	for (int i = 0; i < ac_n; i++) {
		float y = AC_Y0 + (i - ac_top) * AC_ROW_H;
		if (y < AC_Y0 - 26.0f || y > 470.0f) continue;
		int ra = a;
		if (y < AC_Y0 - 2.0f) ra = a * (int)(100 - (AC_Y0 - y) * 4) / 100;
		if (y > 440.0f) ra = a * (int)(100 - (y - 440.0f) * 3) / 100;
		if (ra <= 3) continue;
		int c = ac_rows[i], sel = i == ac_sel, grabbed = sel && ac_grab;
		float dx = grabbed ? 14.0f : 0.0f;
		if (sel) vita2d_draw_rectangle(50, y - 21, 860, 42, WHITE(ra * (grabbed ? 38 : 20) / 100));
		int ta = ra * (sel ? 100 : 78) / 100;
		if (c < 0) {
			vita2d_draw_rectangle(80, y - 1, 20, 3, WHITE(ta));
			vita2d_draw_rectangle(88.5f, y - 10, 3, 20, WHITE(ta));
			ptext_vc(124, y, WHITE(ta), 26, T("New Column"));
			continue;
		}
		int off = cat_hidden[c];
		if (cat_tex[c]) draw_icon_wh(cat_tex[c], 90 + dx, y, 36, 36, off ? ta * 45 / 100 : ta);
		ptext_vc_fit(124 + dx, y, WHITE(off ? ta * 55 / 100 : ta), 26, col_name(c), 560);
		const char *tag = cat_pref[c] ? T("Hidden") : (cat_is_auto(c) ? T("Automatic") : NULL);
		if (tag) ptext_vc(890 - ptext_width(18, tag), y, WHITE(ra * 5 / 10), 18, tag);
	}

	/* the buttons */
	const int c = ac_sel < ac_n ? ac_rows[ac_sel] : -1;
	const float cw = 158.0f;
	float cy = 508.0f;
	int x = 28;
	glyph_cross(x + 10, cy, 8, a);
	ptext_vc_fit(x + 28, cy, BTN_LABEL(a), 20, c < 0 ? T("New Column") : (ac_grab ? T("Drop") : T("Move")), cw - 40);
	x += (int)cw;
	glyph_square(x + 10, cy, 8, a);
	ptext_vc_fit(x + 28, cy, BTN_LABEL(a), 20, T("Rename"), cw - 40);
	x += (int)cw;
	glyph_triangle(x + 10, cy, 9, a);
	ptext_vc_fit(x + 28, cy, BTN_LABEL(a), 20, T("Show / Hide"), cw - 40);
	x += (int)cw;
	glyph_shoulder(x + 14, cy, "L", a);
	glyph_shoulder(x + 48, cy, "R", a);
	ptext_vc_fit(x + 70, cy, BTN_LABEL(a), 20, T("Icon"), cw - 80);
	x += (int)cw;
	{
		int da = c >= CAT_C1 ? a : a * 35 / 100;                                 /* only a column of your own can be deleted */
		vita2d_draw_rectangle(x, cy - 10, 54, 20, RGBA8(0, 0, 0, da * 55 / 100));
		ptext_vc(x + 5, cy, WHITE(da), 15, "START");
		ptext_vc_fit(x + 64, cy, BTN_LABEL(da), 20, T("Delete"), cw - 72);
	}
	x += (int)cw;
	glyph_ring(x + 10, cy, 9, a);
	ptext_vc_fit(x + 28, cy, BTN_LABEL(a), 20, T("Done"), cw - 40);
}
