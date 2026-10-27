#pragma once

/* ---- Choose Icon: a grid of every picture a column or a folder can have (the stock ones, then the retro systems') ---- */
enum { IP_FOLDER, IP_COLUMN };
static int ip_open, ip_kind, ip_target, ip_sel;       /* ip_sel 0: the default picture, n: col_icons[n - 1] */
static float ip_t, ip_top;

#define IP_COLS   8
#define IP_CELL_W 104.0f
#define IP_CELL_H 92.0f
#define IP_X0     64.0f
#define IP_Y0     104.0f
#define IP_VIS    4

static int ip_count(void) { return n_col_icons + 1; }

/* what a column or folder shows when nothing was chosen */
static vita2d_texture *ip_default_tex(void)
{
	if (ip_kind == IP_COLUMN && ip_target < CAT_C1 && col_tex0[ip_target]) return col_tex0[ip_target];
	return tex_folder;
}

static void ip_start(int kind, int target)
{
	col_icons_init();
	ip_kind = kind; ip_target = target;
	int cur = kind == IP_FOLDER ? folder_icon[target] : (col_icon[target] >= 0 ? col_icon[target] + 1 : 0);
	ip_sel = cur >= 0 && cur <= n_col_icons ? cur : 0;
	ip_top = (float)(ip_sel / IP_COLS) - 1.0f;
	if (ip_top < 0.0f) ip_top = 0.0f;
	ip_open = 1;
}

/* Cross: the highlighted picture becomes the folder's or the column's */
static void ip_apply(void)
{
	if (ip_kind == IP_FOLDER) {
		if (ip_target >= 0 && ip_target < n_folders) { folder_icon[ip_target] = (short)ip_sel; folders_save(); rebuild_game_lists(); }
	} else if (ip_target >= 0 && ip_target < CAT_COUNT) {
		col_icon[ip_target] = ip_sel ? ip_sel - 1 : (ip_target >= CAT_C1 ? 9 : -1);           /* a column of your own starts with the folder picture */
		col_apply_icons();
		columns_save();
		config_save();
	}
	ip_open = 0;
}

static void draw_iconpick(float t)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(225 * t)));
	ptext_vc_fit(60, 46, WHITE(a), 28, T("Choose Icon"), 400);
	const char *who = ip_kind == IP_FOLDER ? (ip_target >= 0 && ip_target < n_folders ? folder_name[ip_target] : "") : col_name(ip_target);
	ptext_vc_fit(300, 46, WHITE(a * 7 / 10), 22, who, 600);
	vita2d_draw_rectangle(50, 76, 860, 2, WHITE(a * 30 / 100));

	int n = ip_count(), rows = (n + IP_COLS - 1) / IP_COLS;
	int sel_row = ip_sel / IP_COLS;
	float want = sel_row - 1.0f, maxtop = rows - (float)IP_VIS;
	if (want > maxtop) want = maxtop;
	if (want < 0.0f) want = 0.0f;
	ip_top = lerpf(ip_top, want, 0.3f);
	for (int i = 0; i < n; i++) {
		float cx = IP_X0 + (i % IP_COLS) * IP_CELL_W + IP_CELL_W / 2, cy = IP_Y0 + ((i / IP_COLS) - ip_top) * IP_CELL_H + IP_CELL_H / 2;
		if (cy < IP_Y0 - IP_CELL_H / 2 || cy > IP_Y0 + IP_VIS * IP_CELL_H) continue;
		int ra = a;
		if (cy < IP_Y0 + 10.0f) ra = a * (int)(100 - (IP_Y0 + 10.0f - cy) * 1.4f) / 100;
		if (cy > IP_Y0 + (IP_VIS - 1) * IP_CELL_H + 40.0f) ra = a * (int)(100 - (cy - (IP_Y0 + (IP_VIS - 1) * IP_CELL_H + 40.0f)) * 3) / 100;
		if (ra <= 3) continue;
		int sel = i == ip_sel;
		if (sel) vita2d_draw_rectangle(cx - IP_CELL_W / 2 + 4, cy - IP_CELL_H / 2 + 3, IP_CELL_W - 8, IP_CELL_H - 6, WHITE(ra * 22 / 100));
		vita2d_texture *tx = i == 0 ? ip_default_tex() : col_icons[i - 1];
		if (tx) draw_icon_wh(tx, cx, cy, sel ? 72 : 62, sel ? 72 : 62, ra * (sel ? 100 : 82) / 100);
		if (i == 0) ptext_vc(cx - ptext_width(14, T("Default")) / 2.0f, cy + 34, WHITE(ra * 6 / 10), 14, T("Default"));
	}

	const char *name = ip_sel == 0 ? T("Default") : T(col_icon_name[ip_sel - 1]);
	ptext_vc(480 - ptext_width(24, name) / 2.0f, 470, WHITE(a), 24, name);
	glyph_cross(110, 508, 8, a);
	ptext_vc(129, 508, BTN_LABEL(a), 24, T("Select"));
	glyph_ring(760, 508, 9, a);
	ptext_vc(779, 508, BTN_LABEL(a), 24, T("Cancel"));
}
