#pragma once

/* ---- the trophy page: one game's trophies, the same data LiveArea's trophy bubble shows ---- */
static int tr_sel, tr_ok;
static float tr_top;
static vita2d_texture *tr_icon;

static const unsigned char tr_tint[4][3] = { { 190, 222, 255 }, { 255, 204, 64 }, { 206, 212, 222 }, { 214, 132, 62 } };   /* platinum, gold, silver, bronze */

static void trophy_page_open(const char *commid)
{
	tr_ok = trophy_load(commid);
	tr_sel = 0;
	tr_top = 0.0f;
	if (tr_icon) { defer_free(tr_icon); tr_icon = NULL; }
	char path[96];
	snprintf(path, sizeof(path), TROPHY_CONF "/%s/ICON0.PNG", commid);
	int w = 0, h = 0;
	uint8_t *pix = png_decode_scaled(path, 192, &w, &h);
	if (pix) { tr_icon = texture_from_rgba(pix, w, h); free(pix); }
}

/* a cup in the colour of its grade (grade 1..4); dim while locked */
static void draw_cup(float cx, float cy, float size, int grade, int a)
{
	if (!tex_trophy_cup || grade < 1 || grade > 4) return;
	const unsigned char *c = tr_tint[grade - 1];
	float s = size / (float)vita2d_texture_get_width(tex_trophy_cup);
	vita2d_draw_texture_tint_scale(tex_trophy_cup, cx - size / 2.0f, cy - size / 2.0f, s, s, RGBA8(c[0], c[1], c[2], a));
}

static void trophy_page_input(unsigned pressed)
{
	int n = trophy_n_rows;
	if ((pressed & SCE_CTRL_UP) && tr_sel > 0) { tr_sel--; sound_play(SND_CURSOR); }
	if ((pressed & SCE_CTRL_DOWN) && tr_sel < n - 1) { tr_sel++; sound_play(SND_CURSOR); }
	if (pressed & SCE_CTRL_LTRIGGER) { tr_sel = tr_sel < 5 ? 0 : tr_sel - 5; sound_play(SND_CURSOR); }
	if (pressed & SCE_CTRL_RTRIGGER) { tr_sel = tr_sel + 5 > n - 1 ? (n > 0 ? n - 1 : 0) : tr_sel + 5; sound_play(SND_CURSOR); }
}

static void draw_trophy_page(float alpha)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	const TrophyTitle *t = &trophy_cur;
	if (!tr_ok) {
		const char *m = T("No trophy data for this game yet");
		ptext_vc(480 - ptext_width(28, m) / 2.0f, 250, WHITE(a), 28, m);
		glyph_ring(532, 522, 10, a);
		ptext_vc(552, 522, BTN_LABEL(a), 28, T("Back"));
		return;
	}
	/* header: picture, name, progress, the count of every grade */
	if (tr_icon) {
		float w, h;
		fit_box(tr_icon, 96.0f, 96.0f, &w, &h);
		vita2d_draw_rectangle(60 + 3, 44 + 4, 96, 96, RGBA8(0, 0, 0, a * 25 / 100));
		draw_icon_wh(tr_icon, 108, 92, w, h, a);
	} else {
		draw_icon_wh(tex_trophy, 108, 92, 80, 80, a);
	}
	ptext_vc_fit(184 - text_bearing(30, t->title), 66, WHITE(a), 30, t->title, 740.0f);
	const float bx = 184, by = 96, bw = 560;
	vita2d_draw_rectangle(bx, by, bw, 10, RGBA8(0, 0, 0, a * 40 / 100));
	vita2d_draw_rectangle(bx, by, bw * clampf(t->progress / 100.0f, 0.0f, 1.0f), 10, RGBA8(235, 235, 240, a));
	vita2d_draw_rectangle(bx, by, bw, 1, WHITE(a * 35 / 100));
	char buf[48];
	snprintf(buf, sizeof(buf), "%d%%", t->progress);
	ptext_vc(bx + bw + 16, by + 5, WHITE(a), 24, buf);
	for (int g = 0; g < 4; g++) {
		float x = bx + g * 130.0f;
		draw_cup(x + 14, 138, 30, g + 1, a);
		snprintf(buf, sizeof(buf), "%d / %d", t->have[g], t->all[g]);
		ptext_vc(x + 36, 138, WHITE(a * 85 / 100), 22, buf);
	}
	snprintf(buf, sizeof(buf), T("%d of %d"), t->unlocked, t->total);
	ptext_right_vc(920, 138, WHITE(a * 7 / 10), 22, buf);
	vita2d_draw_rectangle(50, 166, 860, 2, WHITE(a * 30 / 100));

	/* the trophies */
	const float row_h = 60.0f, y0 = 214.0f;
	const int vis = 5;
	int n = trophy_n_rows;
	float want = tr_sel - 2.0f;
	if (want > n - vis) want = (float)(n - vis);
	if (want < 0.0f) want = 0.0f;
	tr_top = lerpf(tr_top, want, 0.3f);
	for (int i = 0; i < n; i++) {
		float y = y0 + (i - tr_top) * row_h;
		if (y < y0 - 40.0f || y > y0 + vis * row_h - 10.0f) continue;
		int ra = a;
		if (y < y0 - 4.0f) ra = a * (int)(100 - (y0 - y) * 2.5f) / 100;
		if (y > y0 + (vis - 1) * row_h + 4.0f) ra = a * (int)(100 - (y - (y0 + (vis - 1) * row_h)) * 2.5f) / 100;
		if (ra <= 3) continue;
		const TrophyRow *r = &trophy_rows[i];
		int sel = i == tr_sel;
		if (sel) vita2d_draw_rectangle(50, y - 28, 860, 56, WHITE(ra * 20 / 100));
		int veiled = r->hidden && !r->unlocked;
		draw_cup(86, y, 40, r->grade, r->unlocked ? ra : ra * 32 / 100);
		const char *name = veiled ? T("Hidden Trophy") : r->title;
		const char *desc = veiled ? T("Keep playing to reveal it.") : r->desc;
		int ta = ra * (r->unlocked ? 100 : 72) / 100;
		ptext_vc_fit(130 - text_bearing(24, name), y - 11, WHITE(ta), 24, name, 640.0f);
		ptext_vc_fit(130 - text_bearing(18, desc), y + 14, WHITE(ta * 7 / 10), 18, desc, 700.0f);
		if (r->unlocked && r->date[0]) ptext_right_vc(890, y - 11, WHITE(ra * 7 / 10), 20, r->date);
		else if (!r->unlocked) ptext_right_vc(890, y - 11, WHITE(ra * 4 / 10), 20, T("Locked"));
	}
	if (n > vis) {                                                       /* a scroll bar at the right edge */
		float th = 5.0f * row_h * vis / (float)n;
		float ty = y0 - 28.0f + (5.0f * row_h - th) * (n > vis ? tr_top / (float)(n - vis) : 0.0f);
		vita2d_draw_rectangle(918, y0 - 28.0f, 3, 5.0f * row_h, WHITE(a * 12 / 100));
		vita2d_draw_rectangle(918, ty, 3, th, WHITE(a * 55 / 100));
	}
	glyph_ring(532, 522, 10, a);
	ptext_vc(552, 522, BTN_LABEL(a), 28, T("Back"));
}
