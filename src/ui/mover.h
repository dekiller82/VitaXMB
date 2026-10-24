#pragma once

/* ---- "Move to...": pick the column or folder an app (or a folder) goes to; a new folder can be made on the spot ---- */
enum { MV_APPS, MV_FOLDER };
static int mv_open, mv_mode, mv_sel, mv_folder = -1, mv_created = -1, mv_n_apps;
static float mv_t, mv_top;
static unsigned char mv_apps[GAME_MAX];        /* flags by index in all_apps: the apps being moved */
static char mv_head[96];
static struct { int col, folder; } mv_rows[CAT_COUNT + MAX_FOLDERS];
static int mv_n;

#define MV_ROW_H 40.0f
#define MV_Y0    112.0f
#define MV_VIS   8

static void mv_build(void)
{
	mv_n = 0;
	int hosts[CAT_COUNT], nh = col_hosts(hosts);
	for (int k = 0; k < nh; k++) {
		int c = hosts[k];
		mv_rows[mv_n].col = c; mv_rows[mv_n++].folder = -1;
		if (mv_mode != MV_APPS) continue;
		int order[MAX_FOLDERS], n = 0;
		for (int i = 0; i < n_folders; i++) if (folder_col[i] == c) order[n++] = i;
		for (int i = 1; i < n; i++) {
			int v = order[i], j = i - 1;
			while (j >= 0 && strcasecmp(folder_name[order[j]], folder_name[v]) > 0) { order[j + 1] = order[j]; j--; }
			order[j + 1] = v;
		}
		for (int i = 0; i < n && mv_n < CAT_COUNT + MAX_FOLDERS; i++) { mv_rows[mv_n].col = c; mv_rows[mv_n++].folder = order[i]; }
	}
}

/* the row of the place the first app (or the folder) is in now */
static int mv_current_row(void)
{
	int col = CAT_GAME, folder = -1;
	if (mv_mode == MV_FOLDER) col = folder_col[mv_folder];
	else for (int i = 0; i < n_all; i++) if (mv_apps[i]) { col = app_colv[i]; folder = app_fold[i]; break; }
	for (int r = 0; r < mv_n; r++) if (mv_rows[r].col == col && mv_rows[r].folder == folder) return r;
	return 0;
}

static void mv_begin(void)
{
	mv_build();
	mv_sel = mv_current_row();
	mv_top = 0.0f;
	mv_created = -1;
	mv_open = 1;
}

static void mv_start_app(int idx)
{
	memset(mv_apps, 0, sizeof(mv_apps));
	if (idx < 0 || idx >= n_all) return;
	mv_apps[idx] = 1;
	mv_n_apps = 1;
	mv_mode = MV_APPS;
	snprintf(mv_head, sizeof(mv_head), T("Move \"%.40s\" to"), all_apps[idx].title);
	mv_begin();
}

/* the apps ticked in the organizer */
static void mv_start_ticked(const unsigned char *on)
{
	memset(mv_apps, 0, sizeof(mv_apps));
	mv_n_apps = 0;
	for (int i = 0; i < n_all; i++) if (on[i]) { mv_apps[i] = 1; mv_n_apps++; }
	if (!mv_n_apps) return;
	mv_mode = MV_APPS;
	snprintf(mv_head, sizeof(mv_head), T("Move %d apps to"), mv_n_apps);
	mv_begin();
}

static void mv_start_folder(int f)
{
	if (f < 0 || f >= n_folders) return;
	mv_folder = f;
	mv_mode = MV_FOLDER;
	snprintf(mv_head, sizeof(mv_head), T("Move the folder \"%.40s\" to"), folder_name[f]);
	mv_begin();
}

static int mv_app_index(const char *id)
{
	for (int i = 0; i < n_all; i++) if (!strcmp(all_apps[i].id, id)) return i;
	return -1;
}

/* Applies the choice. Returns 1; mv_dest_col / mv_dest_folder tell where it went (main follows a moved folder). */
static int mv_dest_col, mv_dest_folder;
static void mv_apply(unsigned char *organizer_ticks)
{
	int col = mv_rows[mv_sel].col, folder = mv_rows[mv_sel].folder;
	char where[96], msg[160];
	mv_dest_col = col; mv_dest_folder = folder;
	if (mv_mode == MV_FOLDER) {
		folder_move(mv_folder, col);
		place_name(where, sizeof(where), col, -1);
		snprintf(msg, sizeof(msg), T("Moved to %s"), where);
		mv_dest_folder = mv_folder;
	} else {
		place_name(where, sizeof(where), col, folder);
		for (int i = 0; i < n_all; i++) if (mv_apps[i]) app_place(all_apps[i].id, col, folder);
		if (mv_n_apps == 1) snprintf(msg, sizeof(msg), T("Moved to %s"), where);
		else snprintf(msg, sizeof(msg), T("%d apps moved to %s"), mv_n_apps, where);
		if (organizer_ticks) memset(organizer_ticks, 0, GAME_MAX);
	}
	folders_save();
	rebuild_game_lists();
	note_show("%s", msg);
	mv_created = -1;
	mv_open = 0;
}

/* Closing without choosing: a folder made here and left empty goes again. */
static void mv_cancel(void)
{
	if (mv_created >= 0 && mv_created < n_folders && folder_count(mv_created) == 0) {
		folder_delete(mv_created);
		folders_save();
	}
	mv_created = -1;
	mv_open = 0;
}

/* The name for a folder made in the dialog arrived: it goes in the highlighted row's column and is selected. */
static void mv_new_folder(const char *name)
{
	if (mv_created >= 0 && mv_created < n_folders && folder_count(mv_created) == 0) folder_delete(mv_created);
	mv_created = -1;
	int f = folder_new(name, mv_rows[mv_sel].col);
	if (f < 0) return;
	mv_created = f;
	folders_save();
	mv_build();
	for (int r = 0; r < mv_n; r++) if (mv_rows[r].folder == f) mv_sel = r;
}

static int mv_count_in(int col, int folder)
{
	int c = 0;
	for (int i = 0; i < n_all; i++) {
		if (folder >= 0) c += app_fold[i] == folder;
		else c += app_colv[i] == col;
	}
	return c;
}

static void draw_mover(float t)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(225 * t)));
	ptext_vc_fit(60, 46, WHITE(a), 28, mv_head, 840);
	vita2d_draw_rectangle(50, 76, 860, 2, WHITE(a * 30 / 100));

	float want = mv_sel - 3.0f;
	float maxtop = mv_n - (float)MV_VIS;
	if (want > maxtop) want = maxtop;
	if (want < 0.0f) want = 0.0f;
	mv_top = lerpf(mv_top, want, 0.3f);
	int cur = mv_current_row();
	for (int i = 0; i < mv_n; i++) {
		float y = MV_Y0 + (i - mv_top) * MV_ROW_H;
		if (y < MV_Y0 - 24.0f || y > 452.0f) continue;
		int ra = a;
		if (y < MV_Y0 - 2.0f) ra = a * (int)(100 - (MV_Y0 - y) * 4) / 100;
		if (y > 430.0f) ra = a * (int)(100 - (y - 430.0f) * 4) / 100;
		if (ra <= 3) continue;
		int sel = i == mv_sel, folder = mv_rows[i].folder;
		if (sel) vita2d_draw_rectangle(50, y - 19, 860, 38, WHITE(ra * 20 / 100));
		int ta = ra * (sel ? 100 : 78) / 100;
		float tx;
		if (folder < 0) {
			draw_icon_wh(cat_tex[mv_rows[i].col], 84, y, 30, 30, ta);
			tx = 112;
			ptext_vc_fit(tx, y, WHITE(ta), 26, col_name(mv_rows[i].col), 540);
		} else {
			draw_icon_wh(tex_folder, 140, y, 26, 26, ta * 85 / 100);
			tx = 166;
			ptext_vc_fit(tx, y, WHITE(ta), 24, folder_name[folder], 500);
		}
		char tag[48];
		if (mv_mode == MV_APPS) {
			int n = mv_count_in(mv_rows[i].col, folder);
			snprintf(tag, sizeof(tag), T(n == 1 ? "%d app" : "%d apps"), n);
			ptext_vc(890 - ptext_width(18, tag), y, WHITE(ra * 5 / 10), 18, tag);
		}
		if (i == cur) {
			const char *c = T("Now");
			float cw = ptext_width(18, c), rx = mv_mode == MV_APPS ? 800.0f : 890.0f;
			vita2d_draw_rectangle(rx - cw - 16, y - 12, cw + 16, 24, RGBA8(255, 255, 255, ra * 22 / 100));
			ptext_vc(rx - cw - 8, y, WHITE(ra * 8 / 10), 18, c);
		}
	}
	glyph_cross(150, 508, 8, a);
	ptext_vc(169, 508, BTN_LABEL(a), 24, T("Move here"));
	float x = 169 + ptext_width(24, T("Move here")) + 56;
	if (mv_mode == MV_APPS) {
		glyph_triangle(x, 508, 9, a);
		ptext_vc(x + 19, 508, BTN_LABEL(a), 24, T("New Folder"));
		x += 19 + ptext_width(24, T("New Folder")) + 56;
	}
	glyph_ring(x, 508, 9, a);
	ptext_vc(x + 19, 508, BTN_LABEL(a), 24, T("Cancel"));
}
