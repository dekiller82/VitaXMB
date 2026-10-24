#pragma once

/* ---- where every app lives: the columns of the bar and the user's folders (ux0:data/VitaXMB/folders.txt) ----
 *   F<TAB>name        starts a folder
 *   C<TAB>column      the column that folder is in (a key of col_key; none = game)
 *   A<TAB>titleid     puts that app in the folder above
 *   P<TAB>column<TAB>titleid   puts that app straight into a column, outside any folder
 * An app with no line stays where it is found: Game > Memory Stick, unless the automatic sort moves it. The list shown in a column is
 * its own rows, then its folders, then the apps that are in none. */

#define MAX_FOLDERS 64
#define MAX_ASSIGN  GAME_MAX
#define FOLDERS_PATH CONFIG_DIR "/folders.txt"

/* the columns that can hold apps and folders: every column but Settings, and the user's own once made */
static const char *col_key[CAT_COUNT] = { "settings", "photo", "music", "video", "game", "network", "emu", "util", "c1", "c2", "c3", "c4" };

static int col_exists(int c) { return c != CAT_SETTINGS && (c < CAT_C1 || col_custom[c]); }

/* the host columns in the order of the bar (hidden ones too); returns how many */
static int col_hosts(int *out)
{
	int n = 0;
	for (int k = 0; k < CAT_COUNT; k++) if (col_exists(cat_order[k])) out[n++] = cat_order[k];
	return n;
}

static int col_from_key(const char *k)
{
	for (int c = 0; c < CAT_COUNT; c++) if (!strcmp(k, col_key[c])) return c;
	return CAT_GAME;
}

static char folder_name[MAX_FOLDERS][40];
static int folder_col[MAX_FOLDERS];
static int n_folders;
static struct { char id[16]; int folder; int col; } assign[MAX_ASSIGN];
static int n_assign;
static int col_fixed[CAT_COUNT];        /* rows a column has of its own (set once the menus are built); the placed folders and apps come after them */
static int col_placed[CAT_COUNT];       /* folders and apps placed in a column */
static Item *all_apps;                  /* every installed app (GAME_MAX, allocated by scan_apps); the menus hold copies */
static int n_all;
static short app_fold[GAME_MAX];        /* folder of all_apps[i], refreshed by refresh_app_folders() */
static unsigned char app_colv[GAME_MAX];/* column of all_apps[i] */
static int open_folder = -1;            /* folder shown in M_FOLDER */
static int open_col = CAT_GAME;         /* the column it is in (kept apart: a folder that was just deleted still sits in its column while the screen leaves it) */

/* The extra columns are drawn from a theme's own pictures, which a theme with full-height panels or strips has none for: its apps stay in Game then. */
static int cols_limited(void) { return pt_blade_mode || pt_strip_mode; }

static int app_entry(const char *id)
{
	for (int i = 0; i < n_assign; i++)
		if (!strcmp(assign[i].id, id)) return i;
	return -1;
}

static int app_folder(const char *id)
{
	int i = app_entry(id);
	return i < 0 ? -1 : assign[i].folder;
}

/* Puts an app in a column (outside a folder when f < 0) or in a folder (which is in one column). */
static void app_place(const char *id, int col, int f)
{
	if (f >= 0) col = folder_col[f];
	int i = app_entry(id);
	if (i < 0) {
		if (n_assign >= MAX_ASSIGN) return;
		i = n_assign++;
		snprintf(assign[i].id, sizeof(assign[0].id), "%s", id);
	}
	assign[i].folder = f;
	assign[i].col = col;
}

static void folders_save(void)
{
	FILE *f = fopen(FOLDERS_PATH, "wb");
	if (!f) return;
	for (int i = 0; i < n_folders; i++) {
		fprintf(f, "F\t%s\n", folder_name[i]);
		if (folder_col[i] != CAT_GAME) fprintf(f, "C\t%s\n", col_key[folder_col[i]]);
		for (int k = 0; k < n_assign; k++)
			if (assign[k].folder == i) fprintf(f, "A\t%s\n", assign[k].id);
	}
	for (int k = 0; k < n_assign; k++)
		if (assign[k].folder < 0) fprintf(f, "P\t%s\t%s\n", col_key[assign[k].col], assign[k].id);
	fclose(f);
}

static void folders_load(void)
{
	n_folders = n_assign = 0;
	FILE *f = fopen(FOLDERS_PATH, "rb");
	if (!f) return;
	char line[128];
	int cur = -1;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == 'F' && line[1] == '\t' && n_folders < MAX_FOLDERS) {
			cur = n_folders++;
			snprintf(folder_name[cur], sizeof(folder_name[0]), "%s", line + 2);
			folder_col[cur] = CAT_GAME;
		} else if (line[0] == 'C' && line[1] == '\t' && cur >= 0) {
			folder_col[cur] = col_from_key(line + 2);
		} else if (line[0] == 'A' && line[1] == '\t' && cur >= 0 && n_assign < MAX_ASSIGN) {
			snprintf(assign[n_assign].id, sizeof(assign[0].id), "%s", line + 2);
			assign[n_assign].folder = cur;
			assign[n_assign++].col = folder_col[cur];
		} else if (line[0] == 'P' && line[1] == '\t' && n_assign < MAX_ASSIGN) {
			char *tab = strchr(line + 2, '\t');
			if (!tab) continue;
			*tab = 0;
			snprintf(assign[n_assign].id, sizeof(assign[0].id), "%s", tab + 1);
			assign[n_assign].folder = -1;
			assign[n_assign++].col = col_from_key(line + 2);
		}
	}
	fclose(f);
	for (int i = 0; i < n_assign; i++) if (assign[i].folder >= 0) assign[i].col = folder_col[assign[i].folder];
}

static void folder_default_name(char *out, int n)
{
	for (int k = 1; k < 100; k++) {
		snprintf(out, n, T("Folder %d"), k);
		int used = 0;
		for (int i = 0; i < n_folders; i++) if (!strcmp(folder_name[i], out)) used = 1;
		if (!used) return;
	}
}

static void folder_clean_name(char *dst, int n, const char *src)
{
	int o = 0;
	while (*src == ' ') src++;
	for (; *src && o < n - 1; src++)
		if (*src != '\t' && *src != '\n' && *src != '\r') dst[o++] = *src;
	while (o > 0 && dst[o - 1] == ' ') o--;
	dst[o] = 0;
}

static int folder_new(const char *name, int col)
{
	if (n_folders >= MAX_FOLDERS) return -1;
	int f = n_folders++;
	folder_clean_name(folder_name[f], sizeof(folder_name[0]), name);
	if (!folder_name[f][0]) folder_default_name(folder_name[f], sizeof(folder_name[0]));
	folder_col[f] = col >= 0 && col < CAT_COUNT && col != CAT_SETTINGS ? col : CAT_GAME;
	return f;
}

/* The apps of a deleted folder stay installed and go to the column the folder was in. */
static void folder_delete(int f)
{
	for (int i = 0; i < n_assign; i++) {
		if (assign[i].folder == f) { assign[i].folder = -1; assign[i].col = folder_col[f]; }
		else if (assign[i].folder > f) assign[i].folder--;
	}
	for (int i = f; i < n_folders - 1; i++) { memcpy(folder_name[i], folder_name[i + 1], sizeof(folder_name[0])); folder_col[i] = folder_col[i + 1]; }
	n_folders--;
}

/* Moves a folder, with everything in it, to another column. */
static void folder_move(int f, int col)
{
	folder_col[f] = col;
	for (int i = 0; i < n_assign; i++) if (assign[i].folder == f) assign[i].col = col;
}

/* app_folder() walks the assignment list, so look every app up once per rebuild instead of once per folder. An app whose column is not
 * available (a theme with its own panels has none for the extra columns) stays in Game. */
static void refresh_app_folders(void)
{
	for (int c = 0; c < CAT_COUNT; c++) col_used[c] = 0;
	for (int i = 0; i < n_folders; i++) col_used[folder_col[i]]++;
	for (int i = 0; i < n_all; i++) {
		int e = app_entry(all_apps[i].id), c = CAT_GAME;
		app_fold[i] = e < 0 ? -1 : (short)assign[e].folder;
		if (e >= 0) c = assign[e].folder >= 0 ? folder_col[assign[e].folder] : assign[e].col;
		if (c != CAT_GAME && (!col_exists(c) || (cols_limited() && c >= CAT_EMU))) c = CAT_GAME;
		app_colv[i] = (unsigned char)c;
		if (c != CAT_GAME) col_used[c]++;
	}
}

static int folder_count(int f)
{
	int c = 0;
	for (int i = 0; i < n_all; i++) if (app_fold[i] == f) c++;
	return c;
}

/* ---- the automatic sort: a guess from the name and the title id, applied only to apps that have no place yet ---- */

static const struct { int col; const char *kw; } sort_rules[] = {
	{ CAT_EMU, "retroarch" }, { CAT_EMU, "ppsspp" }, { CAT_EMU, "pcsx" }, { CAT_EMU, "mgba" }, { CAT_EMU, "snes9x" }, { CAT_EMU, "fceu" },
	{ CAT_EMU, "nestopia" }, { CAT_EMU, "gpsp" }, { CAT_EMU, "picodrive" }, { CAT_EMU, "genesis plus" }, { CAT_EMU, "scummvm" },
	{ CAT_EMU, "dosbox" }, { CAT_EMU, "drastic" }, { CAT_EMU, "mame" }, { CAT_EMU, "fbneo" }, { CAT_EMU, "fbalpha" }, { CAT_EMU, "emulator" },
	{ CAT_EMU, "adrenaline" }, { CAT_EMU, "pspemucfw" }, { CAT_EMU, "yabause" }, { CAT_EMU, "mednafen" }, { CAT_EMU, "gambatte" },
	{ CAT_EMU, "stella" }, { CAT_EMU, "mupen" }, { CAT_EMU, "duckstation" }, { CAT_EMU, "swanstation" }, { CAT_EMU, "flycast" },
	{ CAT_NETWORK, "moonlight" }, { CAT_NETWORK, "rps5" }, { CAT_NETWORK, "chiaki" }, { CAT_NETWORK, "cordless" }, { CAT_NETWORK, "vitacord" },
	{ CAT_NETWORK, "discord" }, { CAT_NETWORK, "telegram" }, { CAT_NETWORK, "netsurf" }, { CAT_NETWORK, "browser" }, { CAT_NETWORK, "remote play" },
	{ CAT_NETWORK, "remoteplay" }, { CAT_NETWORK, "parsec" }, { CAT_NETWORK, "steam link" }, { CAT_NETWORK, "torrent" }, { CAT_NETWORK, "pkgj" },
	{ CAT_NETWORK, "vitadb" }, { CAT_NETWORK, "vita db" }, { CAT_NETWORK, "reddit" }, { CAT_NETWORK, "mastodon" },
	{ CAT_MUSIC, "music" }, { CAT_MUSIC, "gmca" }, { CAT_MUSIC, "spotify" }, { CAT_MUSIC, "mp3" }, { CAT_MUSIC, "soundcloud" },
	{ CAT_MUSIC, "radio" }, { CAT_MUSIC, "podcast" }, { CAT_MUSIC, "jukebox" }, { CAT_MUSIC, "lyric" },
	{ CAT_VIDEO, "vitube" }, { CAT_VIDEO, "youtube" }, { CAT_VIDEO, "vlc" }, { CAT_VIDEO, "video" }, { CAT_VIDEO, "netflix" }, { CAT_VIDEO, "twitch" },
	{ CAT_VIDEO, "kodi" }, { CAT_VIDEO, "plex" }, { CAT_VIDEO, "jellyfin" }, { CAT_VIDEO, "anime" }, { CAT_VIDEO, "movie" },
	{ CAT_VIDEO, "crunchyroll" }, { CAT_VIDEO, "hulu" }, { CAT_VIDEO, "mpv" },
	{ CAT_UTIL, "vitashell" }, { CAT_UTIL, "checkpoint" }, { CAT_UTIL, "backup" }, { CAT_UTIL, "manager" }, { CAT_UTIL, "toolkit" },
	{ CAT_UTIL, "tools" }, { CAT_UTIL, "utility" }, { CAT_UTIL, "utilities" }, { CAT_UTIL, "editor" }, { CAT_UTIL, "extractor" },
	{ CAT_UTIL, "updater" }, { CAT_UTIL, "installer" }, { CAT_UTIL, "vpk" }, { CAT_UTIL, "plugin" }, { CAT_UTIL, "henkaku" },
	{ CAT_UTIL, "storage" }, { CAT_UTIL, "cleaner" }, { CAT_UTIL, "calculator" }, { CAT_UTIL, "notepad" }, { CAT_UTIL, "ftp" },
	{ CAT_UTIL, "save " }, { CAT_UTIL, "cheat" }, { CAT_UTIL, "remap" }, { CAT_UTIL, "controller" }, { CAT_UTIL, "debug" },
	{ CAT_UTIL, "benchmark" }, { CAT_UTIL, "monitor" }, { CAT_UTIL, "reboot" },
};

/* The column an app most likely belongs in: Game for anything that is not clearly something else. Retail games (PCS...) and the games
 * of Adrenaline's folders are always games. */
static int app_guess_col(const Item *it)
{
	if (it->path[0]) return CAT_GAME;
	if (!strncmp(it->id, "PCS", 3)) return CAT_GAME;
	if (!strncmp(it->id, "PSPEMU", 6) && strcmp(it->id, "PSPEMUCFW")) return CAT_GAME;
	char hay[140];
	snprintf(hay, sizeof(hay), "%s %s", it->title, it->id);
	for (char *c = hay; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
	for (unsigned i = 0; i < sizeof(sort_rules) / sizeof(sort_rules[0]); i++)
		if (strstr(hay, sort_rules[i].kw)) return sort_rules[i].col;
	return CAT_GAME;
}

/* Moves the apps that have no place yet to the column they most likely belong in. With dry set only counts them. */
static int autosort_apps(int dry)
{
	int n = 0;
	for (int i = 0; i < n_all; i++) {
		if (app_entry(all_apps[i].id) >= 0) continue;              /* the user (or an earlier sort) already placed it */
		int c = app_guess_col(&all_apps[i]);
		if (c == CAT_GAME) continue;
		if (!dry) app_place(all_apps[i].id, c, -1);
		n++;
	}
	return n;
}

#define SORT_ASK_TEXT "Emulators, utilities, music, video and network apps\n" \
	"move to their own columns; games stay in Game.\n" \
	"Apps you placed yourself stay where they are."

/* Copies an app from the master list into a menu with a clean (not yet loaded) state. */
static void menu_add_app(int m, const Item *src)
{
	Menu *mn = &menus[m];
	if (mn->count >= mn->cap) return;
	Item *d = &mn->items[mn->count];
	*d = *src;
	d->icon = NULL; d->icon_tried = 0; d->load_state = 0; d->pending_pix = NULL;
	d->icon_rect = 0; d->meta_resolved = 0; d->dec_queued = 0; d->glow = 0.0f;
	d->gate_path[0] = d->bg_path[0] = 0;
	__sync_synchronize();
	mn->count++;
}

static void fill_folder_menu(int f)
{
	clear_menu(M_FOLDER);
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);
	if (f >= 0 && f < n_folders)
		for (int i = 0; i < n_all; i++)
			if (app_fold[i] == f) menu_add_app(M_FOLDER, &all_apps[i]);
	if (menus[M_FOLDER].count == 0)
		{ int keep = items_tr; items_tr = 3; add_item(M_FOLDER, KIND_INFO, "Empty folder", "Options > Select Games", NULL, tex_folder); items_tr = keep; }
	Menu *mn = &menus[M_FOLDER];
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (mn->sel < 0) mn->sel = 0;
	mn->pos = (float)mn->sel;
	loader_pause = 0;
}

static void lastplayed_refresh(void);

/* The folder rows of a column (the folders in it, by name), added to the menu m. */
static void add_folder_rows(int m, int col)
{
	int order[MAX_FOLDERS], n = 0;
	for (int i = 0; i < n_folders; i++) if (folder_col[i] == col) order[n++] = i;
	for (int i = 1; i < n; i++) {                              /* insertion sort by name */
		int v = order[i], k = i - 1;
		while (k >= 0 && strcasecmp(folder_name[order[k]], folder_name[v]) > 0) { order[k + 1] = order[k]; k--; }
		order[k + 1] = v;
	}
	for (int i = 0; i < n; i++) {
		int f = order[i], c = folder_count(f);
		char sub[32];
		if (c == 0) snprintf(sub, sizeof(sub), "%s", T("Empty"));
		else if (col == CAT_GAME) snprintf(sub, sizeof(sub), T(c == 1 ? "%d game" : "%d games"), c);
		else snprintf(sub, sizeof(sub), T(c == 1 ? "%d app" : "%d apps"), c);
		Item *it = add_item(m, KIND_FOLDER, folder_name[f], sub, NULL, tex_folder);
		if (it) { it->submenu = M_FOLDER; it->value_id = f; }
	}
}

/* A column other than Game: its own rows, then the folders and apps placed in it. An app is a row like the Last Played disc under Memory Stick:
 * a game icon until the cursor is on it, then the game's own landscape picture with the title beside it (tr bit 4, see draw_column). With nothing
 * placed in a column of the user's own, a single line says where the Options are. */
static void rebuild_host_col(int c)
{
	Menu *mn = &menus[c];
	if (!mn->items) return;
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);
	for (int i = col_fixed[c]; i < mn->count; i++) {
		if (mn->items[i].icon) defer_free(mn->items[i].icon);
		free(mn->items[i].pending_pix);
		mn->items[i].pending_pix = NULL;
	}
	if (mn->count > col_fixed[c]) mn->count = col_fixed[c];
	loader_pause = 0;
	int keep = items_tr;
	items_tr = 0;
	add_folder_rows(c, c);
	items_tr = keep;
	for (int i = 0; i < n_all; i++)
		if (app_colv[i] == c && app_fold[i] < 0) {
			menu_add_app(c, &all_apps[i]);
			Item *d = &mn->items[mn->count - 1];
			d->stock = tex_game_s;
			d->tr = 4 | 8;                                    /* 8: shows its own icon, not the generic one, while the cursor is elsewhere */
		}
	col_placed[c] = mn->count - col_fixed[c];
	if (col_placed[c] == 0 && col_fixed[c] == 0) {
		items_tr = 3;
		add_item(c, KIND_INFO, "No apps here", "Options > Organize Apps", NULL, tex_folder);
		items_tr = keep;
	}
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (mn->sel < 0) mn->sel = 0;
	mn->pos = (float)mn->sel;
}

static void rebuild_game_lists(void)
{
	Menu *mn = &menus[M_MEMCARD];
	refresh_app_folders();
	clear_menu(M_MEMCARD);
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);

	{ int keep = items_tr; items_tr = 0; add_folder_rows(M_MEMCARD, CAT_GAME); items_tr = keep; }
	for (int i = 0; i < n_all; i++)
		if (app_fold[i] < 0 && app_colv[i] == CAT_GAME) menu_add_app(M_MEMCARD, &all_apps[i]);
	if (mn->count == 0)
		{ int keep = items_tr; items_tr = 3; add_item(M_MEMCARD, KIND_INFO, "No games found", "Nothing in ux0:app", NULL, tex_game_s); items_tr = keep; }
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (mn->sel < 0) mn->sel = 0;
	mn->pos = (float)mn->sel;
	loader_pause = 0;
	for (int c = 0; c < CAT_COUNT; c++)
		if (c != CAT_SETTINGS && c != CAT_GAME) rebuild_host_col(c);
	cat_refresh_hidden(cat_now);
	fill_folder_menu(open_folder);
	lastplayed_refresh();                                     /* the disc under Memory Stick follows the installed apps */
}

/* The name of a place, for the messages: "Music", "Music > Concerts". */
static void place_name(char *out, size_t n, int col, int folder)
{
	if (folder >= 0) snprintf(out, n, "%s > %s", col_name(folder_col[folder]), folder_name[folder]);
	else snprintf(out, n, "%s", col_name(col));
}

/* ---- the columns the user arranged: ux0:data/VitaXMB/columns.txt ----
 *   O<TAB>key key key ...    the order of the bar
 *   C<TAB>key<TAB>name       a column the user made (c1 .. c4)
 *   R<TAB>key<TAB>name       a column the user renamed
 *   I<TAB>key<TAB>number     the picture the user chose (index in col_icons) */
#define COLUMNS_PATH CONFIG_DIR "/columns.txt"

static vita2d_texture *col_tex0[CAT_COUNT];         /* each column's own picture */
static vita2d_texture *col_icons[18];               /* the pictures to choose from */
static int n_col_icons;

static void col_icons_init(void)
{
	if (n_col_icons) return;
	for (int c = 0; c < CAT_COUNT; c++) col_tex0[c] = cat_tex[c];
	vita2d_texture *l[] = { cat_tex[CAT_GAME], cat_tex[CAT_MUSIC], cat_tex[CAT_VIDEO], cat_tex[CAT_PHOTO], cat_tex[CAT_NETWORK], cat_tex[CAT_EMU], cat_tex[CAT_UTIL],
	                        cat_tex[CAT_SETTINGS], tex_trophy, tex_folder, tex_umd, tex_ms_s, tex_remote, tex_sharing, tex_browser, tex_rss, tex_lftv, tex_savedata_s };
	for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]) && n_col_icons < 18; i++) if (l[i]) col_icons[n_col_icons++] = l[i];
}

static void col_apply_icons(void)
{
	col_icons_init();
	for (int c = 0; c < CAT_COUNT; c++)
		cat_tex[c] = col_icon[c] >= 0 && col_icon[c] < n_col_icons ? col_icons[col_icon[c]] : col_tex0[c];
}

static void columns_save(void)
{
	FILE *f = fopen(COLUMNS_PATH, "wb");
	if (!f) return;
	fputs("O\t", f);
	for (int k = 0; k < CAT_COUNT; k++) fprintf(f, "%s%s", k ? " " : "", col_key[cat_order[k]]);
	fputc(10, f);
	for (int c = 0; c < CAT_COUNT; c++) {
		if (col_custom[c]) fprintf(f, "C\t%s\t%s\n", col_key[c], col_title[c]);
		else if (col_title[c][0]) fprintf(f, "R\t%s\t%s\n", col_key[c], col_title[c]);
		if (col_icon[c] >= 0) fprintf(f, "I\t%s\t%d\n", col_key[c], col_icon[c]);
	}
	fclose(f);
}

static int col_key_index(const char *k, int len)
{
	for (int c = 0; c < CAT_COUNT; c++) if ((int)strlen(col_key[c]) == len && !strncmp(k, col_key[c], len)) return c;
	return -1;
}

static void columns_load(void)
{
	col_icons_init();
	FILE *f = fopen(COLUMNS_PATH, "rb");
	if (f) {
		char line[160];
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (line[0] == 'O' && line[1] == '\t') {
				int seen[CAT_COUNT] = { 0 }, n = 0;
				const char *p = line + 2;
				while (*p && n < CAT_COUNT) {
					const char *e = p;
					while (*e && *e != ' ') e++;
					int c = col_key_index(p, (int)(e - p));
					if (c >= 0 && !seen[c]) { seen[c] = 1; cat_order[n++] = c; }
					p = *e ? e + 1 : e;
				}
				for (int c = 0; c < CAT_COUNT && n < CAT_COUNT; c++) if (!seen[c]) cat_order[n++] = c;
			} else if ((line[0] == 'C' || line[0] == 'R' || line[0] == 'I') && line[1] == '\t') {
				char *tab = strchr(line + 2, '\t');
				if (!tab) continue;
				*tab = 0;
				int c = col_from_key(line + 2);
				if (strcmp(col_key[c], line + 2) != 0) continue;
				if (line[0] == 'I') { int v = atoi(tab + 1); col_icon[c] = v >= 0 && v < n_col_icons ? v : -1; }
				else {
					if (line[0] == 'C') { if (c < CAT_C1) continue; col_custom[c] = 1; }
					snprintf(col_title[c], sizeof(col_title[0]), "%s", tab + 1);
				}
			}
		}
		fclose(f);
	}
	for (int c = CAT_C1; c < CAT_COUNT; c++) if (col_custom[c] && col_icon[c] < 0) col_icon[c] = 9;
	col_apply_icons();
}

/* A new column of the user's: the first free one of the four. Returns it, or -1. */
static int col_create(const char *name)
{
	for (int c = CAT_C1; c < CAT_COUNT; c++) {
		if (col_custom[c]) continue;
		col_custom[c] = 1;
		col_icon[c] = 9;                                    /* a folder until the user picks another */
		folder_clean_name(col_title[c], sizeof(col_title[0]), name);
		if (!col_title[c][0]) snprintf(col_title[c], sizeof(col_title[0]), T("Column %d"), c - CAT_C1 + 1);
		col_apply_icons();
		return c;
	}
	return -1;
}

/* Deleting a column of the user's: its apps and folders go to Game. */
static void col_delete(int c)
{
	if (c < CAT_C1 || !col_custom[c]) return;
	for (int i = 0; i < n_folders; i++) if (folder_col[i] == c) folder_col[i] = CAT_GAME;
	for (int i = 0; i < n_assign; i++) if (assign[i].col == c) assign[i].col = CAT_GAME;
	col_custom[c] = 0;
	col_title[c][0] = 0;
	col_icon[c] = -1;
	col_apply_icons();
}
