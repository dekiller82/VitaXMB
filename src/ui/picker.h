#pragma once

/* ---- the app picker: tick the apps that belong in a folder (PK_FOLDER), or tick apps to move together (PK_ORGANIZE) ---- */
enum { PK_FOLDER, PK_ORGANIZE };
static int pk_open, pk_folder = -1, pk_sel, pk_mode;
static float pk_t, pk_top;
static unsigned char pk_on[GAME_MAX];

static int pk_count(void)
{
	int c = 0;
	for (int i = 0; i < n_all; i++) c += pk_on[i];
	return c;
}

static void pk_start(int f)
{
	pk_mode = PK_FOLDER;
	pk_folder = f;
	pk_sel = 0;
	pk_top = 0.0f;
	for (int i = 0; i < n_all; i++) pk_on[i] = app_folder(all_apps[i].id) == f;
	pk_open = 1;
}

/* Organize Apps: every app with the place it is in; tick some and move them together */
static void pk_start_organize(void)
{
	pk_mode = PK_ORGANIZE;
	pk_folder = -1;
	pk_sel = 0;
	pk_top = 0.0f;
	memset(pk_on, 0, sizeof(pk_on));
	pk_open = 1;
}

static void pk_apply(void)
{
	if (pk_mode == PK_FOLDER) {
		for (int i = 0; i < n_all; i++) {
			int cur_f = app_folder(all_apps[i].id);
			if (pk_on[i]) app_place(all_apps[i].id, folder_col[pk_folder], pk_folder);
			else if (cur_f == pk_folder) app_place(all_apps[i].id, folder_col[pk_folder], -1);      /* out of the folder, into its column */
		}
		folders_save();
		rebuild_game_lists();
	}
}

static void draw_picker(float t)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(225 * t)));
	char head[96];
	if (pk_mode == PK_ORGANIZE) snprintf(head, sizeof(head), "%s", T("Organize Apps"));
	else snprintf(head, sizeof(head), T("Games in \"%s\""), pk_folder >= 0 ? folder_name[pk_folder] : "");
	ptext_vc_fit(60, 46, WHITE(a), 28, head, 600);
	char cnt[32];
	snprintf(cnt, sizeof(cnt), T("%d selected"), pk_count());
	ptext_vc(900 - ptext_width(22, cnt), 46, WHITE(a * 7 / 10), 22, cnt);
	vita2d_draw_rectangle(50, 76, 860, 2, WHITE(a * 30 / 100));

	if (n_all == 0) {
		ptext_vc(480 - ptext_width(24, T("No games installed")) / 2.0f, 270, WHITE(a * 7 / 10), 24, T("No games installed"));
	}
	const float row_h = 38.0f, y0 = 112.0f;
	for (int i = 0; i < n_all; i++) {
		float y = y0 + (i - pk_top) * row_h;
		if (y < y0 - 20.0f || y > 452.0f) continue;
		int sel = i == pk_sel;
		int ra = a;
		if (y < y0 - 2.0f) ra = a * (int)(100 - (y0 - y) * 5) / 100;
		if (y > 430.0f) ra = a * (int)(100 - (y - 430.0f) * 4) / 100;
		if (ra <= 3) continue;
		if (sel) vita2d_draw_rectangle(50, y - 18, 860, 36, WHITE(ra * 20 / 100));
		/* check box */
		float bx = 70, by = y - 10;
		unsigned int bc = WHITE(ra * (sel ? 100 : 70) / 100);
		vita2d_texture *tbox = res_theme("system_plugin", "tex_box"), *tchk = res_theme("system_plugin", "tex_check");     /* a theme's own check box */
		if (tbox && tchk) {
			float s = 20.0f / (float)vita2d_texture_get_width(tbox);
			vita2d_draw_texture_tint_scale(tbox, bx, by, s, s, WHITE(ra * (sel ? 100 : 70) / 100));
			if (pk_on[i]) vita2d_draw_texture_tint_scale(tchk, bx + 1, by - 2, s * 1.2f, s * 1.2f, WHITE(ra));
		} else {
			vita2d_draw_rectangle(bx, by, 20, 2, bc);
			vita2d_draw_rectangle(bx, by + 18, 20, 2, bc);
			vita2d_draw_rectangle(bx, by, 2, 20, bc);
			vita2d_draw_rectangle(bx + 18, by, 2, 20, bc);
			if (pk_on[i]) vita2d_draw_rectangle(bx + 5, by + 5, 10, 10, WHITE(ra));
		}
		const Item *it = &all_apps[i];
		ptext_vc_fit(110, y, WHITE(ra * (sel ? 100 : 78) / 100), 24, it->title, pk_mode == PK_ORGANIZE ? 450 : 540);
		if (pk_mode == PK_ORGANIZE) {                       /* where it is now */
			char tag[96];
			place_name(tag, sizeof(tag), app_colv[i], app_fold[i]);
			float tw = ptext_width(18, tag);
			if (tw > 320.0f) tw = 320.0f;
			ptext_vc_fit(890 - tw, y, WHITE(ra * 6 / 10), 18, tag, 320);
		} else {
			int of = app_folder(it->id);
			if (of >= 0 && of != pk_folder) {
				char tag[64];
				snprintf(tag, sizeof(tag), T("In: %s"), folder_name[of]);
				ptext_vc(890 - ptext_width(18, tag), y, WHITE(ra * 5 / 10), 18, tag);
			}
		}
	}
	glyph_cross(110, 508, 8, a);
	ptext_vc(129, 508, BTN_LABEL(a), 24, T("Select"));
	if (pk_mode == PK_ORGANIZE) {
		glyph_square(290, 508, 8, a);
		ptext_vc(309, 508, BTN_LABEL(a), 24, T("All / None"));
		glyph_triangle(500, 508, 9, a);
		ptext_vc(519, 508, BTN_LABEL(a), 24, pk_count() ? T("Move Selected") : T("Move"));
		glyph_ring(760, 508, 9, a);
		ptext_vc(779, 508, BTN_LABEL(a), 24, T("Done"));
	} else {
		glyph_triangle(300, 508, 9, a);
		ptext_vc(319, 508, BTN_LABEL(a), 24, T("All / None"));
		glyph_ring(560, 508, 9, a);
		ptext_vc(579, 508, BTN_LABEL(a), 24, T("Done"));
	}
}

/* The "Options" pill at the bottom right: a single rounded shape (row by row, so the translucent
 * fill never double-darkens where caps and body would overlap). */
static void draw_options_pill(int a)
{
	if (a <= 3) return;
	const float x = 784, y = 448, w = 164, h = 38, r = h / 2;
	unsigned int bg = RGBA8(16, 16, 20, a * 52 / 100);
	for (int row = 0; row < (int)h; row++) {
		float dy = row + 0.5f - r;
		float inset = r - sqrtf(fmaxf(0.0f, r * r - dy * dy));
		vita2d_draw_rectangle(x + inset, y + row, w - 2 * inset, 1.0f, bg);
	}
	glyph_triangle(x + 22, y + h / 2, 8, a);
	ptext_vc(x + 42, y + h / 2, WHITE(a), 24, T("Options"));
}
