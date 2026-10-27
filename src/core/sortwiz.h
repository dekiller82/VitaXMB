#pragma once

/* ---- the sort wizard (Settings > VitaXMB Settings > Sort Apps into Columns) ----
 * Kinds of app (rows): the first five are VitaXMB's own columns; PSP games, PS1 games, PSM games and Homebrew go to columns of the user's, named like
 * the row (a column that already has that name is reused, otherwise the first free one is made). Retro games (the ROMs of retro.h) are listed per system,
 * and the layout row says where they go: a folder per system in Emulators, a column per system, or one Retro column with a folder per system.
 * Only apps that have no place yet move: whatever the user placed stays, and so does every retail game (Game is the Vita's own games). */

enum { SW_EMU, SW_UTIL, SW_MUSIC, SW_VIDEO, SW_NET, SW_PSP, SW_PS1, SW_PSM, SW_HB, SW_KINDS };
static const char *const sw_label[SW_KINDS] = { "Emulators", "Utilities", "Music", "Video", "Network", "PSP games", "PS1 games", "PSM games", "Homebrew" };
static const char *const sw_hint[SW_KINDS] = {
	"RetroArch, PPSSPP, Adrenaline and other emulators",
	"VitaShell, Checkpoint and other tools",
	"Music players",
	"Video players and streaming apps",
	"Remote play, browsers and downloaders",
	"ISO / CSO images and PSP downloads, with or without a bubble",
	"PS1 classics, with or without a bubble",
	"PlayStation Mobile games (ux0:psm)",
	"Every other app that is not a retail game",
};
static const int sw_fixed[SW_KINDS] = { CAT_EMU, CAT_UTIL, CAT_MUSIC, CAT_VIDEO, CAT_NETWORK, -1, -1, -1, -1 };    /* -1: a column of the user's */
static const char *const sw_newcol[SW_KINDS] = { NULL, NULL, NULL, NULL, NULL, "PSP", "PS1", "PSM", "Homebrew" };
static const char *const sw_icon[SW_KINDS] = { NULL, NULL, NULL, NULL, NULL, "psp", "psx", "psm", NULL };       /* the picture of a column the wizard makes (see icon_by_slug) */

enum { SL_EMU_FOLDERS, SL_COLUMNS, SL_ONE_COLUMN, SL_COUNT };                              /* where the retro games go */
static const char *const sl_label[SL_COUNT] = { "Folders in Emulators", "A column per system", "One Retro column" };
static const char *const sl_hint[SL_COUNT] = {
	"A folder for each system inside the Emulators column",
	"A column of its own for each system (up to the free columns, then folders)",
	"One column called Retro with a folder for each system",
};

enum { SR_KIND, SR_LAYOUT, SR_SYS };
typedef struct { int type, idx; } SwRow;
static SwRow sw_row[SW_KINDS + 1 + RETRO_SYS_MAX];
static int sw_nrows;
static int sw_count[SW_KINDS];                                      /* apps with no place yet that each kind would move */
static int sw_sys_count[RETRO_SYS_MAX];                             /* retro games with no place yet, per system */
static unsigned char sw_on[SW_KINDS], sw_sys_on[RETRO_SYS_MAX];
static int sw_layout;

/* A bubble Adrenaline's tool made (PSPEMU...) starts a game file; whether that game is a PS1 classic is in the file's PARAM.SFO. */
static int sw_bubble_org(const char *id)
{
	static struct { char id[16]; int org; } memo[48];
	static int n_memo;
	for (int i = 0; i < n_memo; i++) if (!strcmp(memo[i].id, id)) return memo[i].org;
	int org = ORG_PSP;
	for (int i = 0; i < psp_n_bubbles; i++) {
		if (strcmp(psp_bubbles[i].id, id)) continue;
		PmSrc src;
		if (pm_src_open(&src, psp_bubbles[i].path)) {
			size_t n = 0;
			uint8_t *sfo = pm_game_file(&src, PM_F_SFO, &n);
			if (sfo) {
				char cat[8] = "";
				if (pm_sfo_string(sfo, n, "CATEGORY", cat, sizeof(cat)) && !strcmp(cat, "ME")) org = ORG_PS1;
				free(sfo);
			}
			pm_src_close(&src);
		}
		break;
	}
	if (n_memo < 48) { snprintf(memo[n_memo].id, sizeof(memo[0].id), "%s", id); memo[n_memo++].org = org; }
	return org;
}

/* The kind an installed app belongs to, or -1 when it stays where it is. */
static int sw_row_of(const Item *it)
{
	if (it->org >= ORG_RETRO0) return -1;                                                          /* a retro game has its own rows */
	if (it->org == ORG_PSM) return SW_PSM;
	if (it->path[0]) return it->org == ORG_PS1 ? SW_PS1 : SW_PSP;                                  /* a game of Adrenaline's folders */
	if (!strncmp(it->id, "PSPEMU", 6) && strcmp(it->id, "PSPEMUCFW")) return sw_bubble_org(it->id) == ORG_PS1 ? SW_PS1 : SW_PSP;
	if (!strncmp(it->id, "PCS", 3)) return -1;                                                    /* a retail Vita game */
	switch (app_keyword_col(it)) {
	case CAT_EMU: return SW_EMU;
	case CAT_UTIL: return SW_UTIL;
	case CAT_MUSIC: return SW_MUSIC;
	case CAT_VIDEO: return SW_VIDEO;
	case CAT_NETWORK: return SW_NET;
	default: return SW_HB;
	}
}

/* How many apps with no place yet each row would move, and the rows of the list; returns the total. */
static int sw_scan(void)
{
	int total = 0;
	memset(sw_count, 0, sizeof(sw_count));
	memset(sw_sys_count, 0, sizeof(sw_sys_count));
	for (int i = 0; i < n_all; i++) {
		if (app_entry(all_apps[i].id) >= 0) continue;
		int r = sw_row_of(&all_apps[i]);
		if (r >= 0) { sw_count[r]++; total++; }
	}
	for (int i = 0; i < n_retro; i++)
		if (app_entry(retro_rec[i].id) < 0) { sw_sys_count[retro_rec[i].sys]++; total++; }
	sw_nrows = 0;
	for (int k = 0; k < SW_KINDS; k++) { sw_row[sw_nrows].type = SR_KIND; sw_row[sw_nrows++].idx = k; }
	int any = 0;
	for (int s = 0; s < n_retro_sys; s++) any |= sw_sys_count[s] > 0;
	if (any) {
		sw_row[sw_nrows].type = SR_LAYOUT; sw_row[sw_nrows++].idx = 0;
		for (int s = 0; s < n_retro_sys; s++) if (sw_sys_count[s]) { sw_row[sw_nrows].type = SR_SYS; sw_row[sw_nrows++].idx = s; }
	}
	return total;
}

static int sw_find_col(const char *name)
{
	for (int c = CAT_C1; c < CAT_COUNT; c++) if (col_custom[c] && !strcasecmp(col_title[c], name)) return c;
	return -1;
}

static int sw_find_folder(const char *name, int col)
{
	for (int f = 0; f < n_folders; f++) if (folder_col[f] == col && !strcasecmp(folder_name[f], name)) return f;
	return -1;
}

/* the column of the user's with that name, made when there is none; -1 when all of them are taken */
static int sw_col(const char *name, const char *icon, int *made)
{
	int c = sw_find_col(name);
	if (c < 0) {
		c = col_create(name);
		if (c >= 0) *made = 1;
	}
	if (c >= 0 && icon && (col_icon[c] < 0 || col_icon[c] == 9)) {          /* the system's picture, unless the user chose another one */
		int ic = icon_by_slug(icon);
		if (ic >= 0) { col_icon[c] = ic; col_apply_icons(); *made = 1; }
	}
	return c;
}

static int sw_folder(const char *name, const char *icon, int col)
{
	int f = sw_find_folder(name, col);
	if (f < 0) f = folder_new(name, col);
	if (f >= 0 && icon && folder_icon[f] == 0) { int ic = icon_by_slug(icon); if (ic >= 0) folder_icon[f] = (short)(ic + 1); }     /* the system's picture, unless the user chose another one */
	return f;
}

/* Moves the apps of the ticked kinds and the retro games of the ticked systems. Returns how many moved; *no_place counts the kinds and systems that found
 * no free column or folder. */
static int sw_apply(int *no_place)
{
	int col[SW_KINDS], moved = 0, made = 0;
	*no_place = 0;
	for (int r = 0; r < SW_KINDS; r++) {
		col[r] = -1;
		if (!sw_on[r] || !sw_count[r]) continue;
		col[r] = sw_fixed[r] >= 0 ? sw_fixed[r] : sw_col(sw_newcol[r], sw_icon[r], &made);
		if (col[r] < 0) (*no_place)++;
	}
	for (int i = 0; i < n_all; i++) {
		if (app_entry(all_apps[i].id) >= 0) continue;
		int r = sw_row_of(&all_apps[i]);
		if (r < 0 || col[r] < 0) continue;
		app_place(all_apps[i].id, col[r], -1);
		moved++;
	}

	/* the retro games: where each system goes (a column of its own, or a folder in a column) */
	int sys_col[RETRO_SYS_MAX], sys_fold[RETRO_SYS_MAX], sys_ok[RETRO_SYS_MAX], retro_col = -1;
	for (int s = 0; s < n_retro_sys; s++) {
		sys_col[s] = CAT_EMU; sys_fold[s] = -1; sys_ok[s] = 0;
		if (!sw_sys_on[s] || !sw_sys_count[s]) continue;
		const char *name = retro_sys[s].label, *icon = retro_sys[s].def ? retro_sys[s].def->icon : NULL;
		if (sw_layout == SL_COLUMNS) {
			int c = sw_col(name, icon, &made);
			if (c >= 0) { sys_col[s] = c; sys_ok[s] = 1; continue; }                          /* no free column left: a folder in Emulators instead */
		} else if (sw_layout == SL_ONE_COLUMN) {
			if (retro_col < 0) retro_col = sw_col("Retro", NULL, &made);
			if (retro_col >= 0) {
				sys_col[s] = retro_col; sys_fold[s] = sw_folder(name, icon, retro_col);
				sys_ok[s] = sys_fold[s] >= 0;
				if (!sys_ok[s]) (*no_place)++;
				continue;
			}
		}
		sys_fold[s] = sw_folder(name, icon, CAT_EMU);
		sys_ok[s] = sys_fold[s] >= 0;
		if (!sys_ok[s]) (*no_place)++;
	}
	for (int i = 0; i < n_retro; i++) {
		int s = retro_rec[i].sys;
		if (!sys_ok[s] || app_entry(retro_rec[i].id) >= 0) continue;
		app_place(retro_rec[i].id, sys_col[s], sys_fold[s]);
		moved++;
	}
	retro_add_placed();                                                                           /* the games that now have a place become apps */
	qsort(all_apps, n_all, sizeof(Item), item_cmp);
	if (made) columns_save();
	folders_save();
	return moved;
}
