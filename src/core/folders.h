#pragma once

/* ---- where every app lives: the columns of the bar and the user's folders (ux0:data/VitaXMB/folders.txt) ----
 *   F<TAB>name        starts a folder
 *   C<TAB>column      the column that folder is in (a key of col_key; none = game)
 *   A<TAB>titleid     puts that app in the folder above
 *   P<TAB>column<TAB>titleid   puts that app straight into a column, outside any folder
 * An app with no line stays where it is found: Game > Memory Stick, unless the automatic sort moves it. The list shown in a column is
 * its own rows, then its folders, then the apps that are in none. */

#define MAX_FOLDERS 160
#define MAX_ASSIGN  GAME_MAX
#define FOLDERS_PATH CONFIG_DIR "/folders.txt"

/* the columns that can hold apps and folders: every column but Settings, and the user's own once made */
static const char *col_key[CAT_COUNT] = { "settings", "photo", "music", "video", "game", "network", "emu", "util", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "c10", "c11", "c12", "c13", "c14", "c15", "c16", "c17", "c18", "c19", "c20", "c21", "c22" };

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
static short folder_icon[MAX_FOLDERS];  /* the picture chosen for a folder: an index in col_icons[] plus one; 0 is the folder picture */
static vita2d_texture *col_icons[64];   /* the pictures a column or a folder can have (the stock ones, then the systems') */
static const char *col_icon_name[64];
static int n_col_icons;
static int n_folders;
static struct { char id[16]; int folder; int col; } assign[MAX_ASSIGN];
static int n_assign;
static int col_fixed[CAT_COUNT];        /* rows a column has of its own (set once the menus are built); the placed folders and apps come after them */
static int col_placed[CAT_COUNT];       /* folders and apps placed in a column */
static Item *all_apps;                  /* every installed app (GAME_MAX, allocated by scan_apps); the menus hold copies */
static int n_all;
static short app_fold[GAME_MAX];        /* folder of all_apps[i], refreshed by refresh_app_folders() */
static unsigned char app_colv[GAME_MAX];/* column of all_apps[i] */
static unsigned char app_realcol[GAME_MAX];/* the column the user gave it, also when the theme has no panel for it (then app_colv is Game) */
static int open_folder = -1;            /* folder shown in M_FOLDER */
static int open_vcol = -1;             /* the column shown as a folder in M_VCOL (a theme without the extra columns) */
static int open_col = CAT_GAME;         /* the column it is in (kept apart: a folder that was just deleted still sits in its column while the screen leaves it) */

/* The extra columns are drawn from a theme's own pictures, which a theme with full-height panels or strips has none for: its apps stay in Game then. */
static int cols_limited(void) { return pt_blade_mode || pt_strip_mode; }

/* a lookup by title id through a hash of the assignment list (a few thousand placed retro games would make a plain walk slow);
 * the entries are only ever appended, so a new one is added to the hash as it is made, and a reload rebuilds it */
#define ASSIGN_HASH 16384
static short assign_hash[ASSIGN_HASH];            /* index + 1 of the first entry with that hash, linear probing; 0 empty */
static int assign_hashed = -1;                    /* entries in the hash, -1: it must be rebuilt */

static unsigned assign_h(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	return h & (ASSIGN_HASH - 1);
}

static void assign_hash_add(int i)
{
	unsigned h = assign_h(assign[i].id);
	while (assign_hash[h]) {
		if (!strcmp(assign[assign_hash[h] - 1].id, assign[i].id)) return;     /* the first one of a duplicate stays */
		h = (h + 1) & (ASSIGN_HASH - 1);
	}
	assign_hash[h] = (short)(i + 1);
}

static void assign_reindex(void)
{
	memset(assign_hash, 0, sizeof(assign_hash));
	for (int i = 0; i < n_assign; i++) assign_hash_add(i);
	assign_hashed = n_assign;
}

static int app_entry(const char *id)
{
	if (assign_hashed != n_assign) assign_reindex();
	unsigned h = assign_h(id);
	while (assign_hash[h]) {
		if (!strcmp(assign[assign_hash[h] - 1].id, id)) return assign_hash[h] - 1;
		h = (h + 1) & (ASSIGN_HASH - 1);
	}
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
		if (assign_hashed == n_assign - 1) { assign_hash_add(i); assign_hashed = n_assign; }
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
		if (folder_icon[i] > 0) fprintf(f, "I\t%d\n", folder_icon[i] - 1);
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
	assign_hashed = -1;
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
			folder_icon[cur] = 0;
		} else if (line[0] == 'C' && line[1] == '\t' && cur >= 0) {
			folder_col[cur] = col_from_key(line + 2);
		} else if (line[0] == 'I' && line[1] == '\t' && cur >= 0) {
			int v = atoi(line + 2);
			folder_icon[cur] = v >= 0 && v < 63 ? (short)(v + 1) : 0;
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
	folder_icon[f] = 0;
	return f;
}

/* The apps of a deleted folder stay installed and go to the column the folder was in. */
static void folder_delete(int f)
{
	for (int i = 0; i < n_assign; i++) {
		if (assign[i].folder == f) { assign[i].folder = -1; assign[i].col = folder_col[f]; }
		else if (assign[i].folder > f) assign[i].folder--;
	}
	for (int i = f; i < n_folders - 1; i++) { memcpy(folder_name[i], folder_name[i + 1], sizeof(folder_name[0])); folder_col[i] = folder_col[i + 1]; folder_icon[i] = folder_icon[i + 1]; }
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
		if (c != CAT_GAME && !col_exists(c)) c = CAT_GAME;
		app_realcol[i] = (unsigned char)c;
		if (c != CAT_GAME && cols_limited() && c >= CAT_EMU) c = CAT_GAME;
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

/* The column the name and the title id point to (Game when no keyword fits). */
static int app_keyword_col(const Item *it)
{
	char hay[140];
	snprintf(hay, sizeof(hay), "%s %s", it->title, it->id);
	for (char *c = hay; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
	for (unsigned i = 0; i < sizeof(sort_rules) / sizeof(sort_rules[0]); i++)
		if (strstr(hay, sort_rules[i].kw)) return sort_rules[i].col;
	return CAT_GAME;
}

/* the first start's question; the sort itself is the wizard in core/sortwiz.h */
#define SORT_ASK_TEXT "Choose which kinds of app get their own column:\n" \
	"emulators, utilities, PSP and PS1 games, homebrew...\n" \
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

/* A theme with its own panels has no picture for the extra columns (Emulators, Utilities, the user's own), so each one is a folder under Memory Stick,
 * and the folders of that column are folders inside it. */
static void add_folder_rows(int m, int col);

static int vcol_apps(int c)
{
	int n = 0;
	for (int i = 0; i < n_all; i++) if (app_realcol[i] == c) n++;
	return n;
}

static void fill_vcol_menu(int c)
{
	clear_menu(M_VCOL);
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);
	if (c >= CAT_EMU && c < CAT_COUNT) {
		int keep = items_tr;
		items_tr = 0;
		add_folder_rows(M_VCOL, c);
		items_tr = keep;
		for (int i = 0; i < n_all; i++)
			if (app_realcol[i] == c && app_fold[i] < 0) menu_add_app(M_VCOL, &all_apps[i]);
	}
	if (menus[M_VCOL].count == 0)
		{ int keep = items_tr; items_tr = 3; add_item(M_VCOL, KIND_INFO, "Empty folder", "Options > New Folder", NULL, tex_folder); items_tr = keep; }
	Menu *mn = &menus[M_VCOL];
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
		vita2d_texture *ft = tex_folder;
		int ic = folder_icon[f] - 1;
		if (ic >= 0 && ic < n_col_icons && col_icons[ic]) ft = col_icons[ic];
		Item *it = add_item(m, KIND_FOLDER, folder_name[f], sub, NULL, ft);
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
	{	/* a column with more rows than its array (a retro system with hundreds of games): a larger array takes over. The old one stays allocated:
		 * a pointer to one of its rows may still be around, and a stale row is harmless where a freed one is not. */
		int need = col_fixed[c] + 1;
		for (int i = 0; i < n_folders; i++) if (folder_col[i] == c) need++;
		for (int i = 0; i < n_all; i++) if (app_colv[i] == c && app_fold[i] < 0) need++;
		if (need > mn->cap) {
			int ncap = mn->cap > 0 ? mn->cap : MAX_ITEMS;
			while (ncap < need) ncap *= 2;
			Item *nb = (Item *)calloc((size_t)ncap, sizeof(Item));
			if (nb) { memcpy(nb, mn->items, (size_t)mn->count * sizeof(Item)); mn->items = nb; mn->cap = ncap; }
		}
	}
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
	if (cols_limited()) {                                      /* each extra column as a folder */
		int keep = items_tr;
		items_tr = 0;
		for (int k = 0; k < CAT_COUNT; k++) {
			int c = cat_order[k], na = c >= CAT_EMU && col_exists(c) ? vcol_apps(c) : 0, nf = 0;
			for (int i = 0; i < n_folders; i++) nf += folder_col[i] == c;
			if (c < CAT_EMU || !col_exists(c) || (!na && !nf)) continue;
			char sub[32];
			snprintf(sub, sizeof(sub), T(na == 1 ? "%d app" : "%d apps"), na);
			vita2d_texture *ft = cat_tex[c] ? cat_tex[c] : tex_folder;
			Item *it = add_item(M_MEMCARD, KIND_FOLDER, col_name(c), sub, NULL, ft);
			if (it) { it->submenu = M_VCOL; it->value_id = c; }
		}
		items_tr = keep;
	}
	for (int i = 0; i < n_all; i++)
		if (app_fold[i] < 0 && app_colv[i] == CAT_GAME && !(cols_limited() && app_realcol[i] >= CAT_EMU)) menu_add_app(M_MEMCARD, &all_apps[i]);
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
	if (open_vcol >= 0) fill_vcol_menu(open_vcol);
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
 *   C<TAB>key<TAB>name       a column the user made (c1 .. c22)
 *   R<TAB>key<TAB>name       a column the user renamed
 *   I<TAB>key<TAB>number     the picture the user chose (index in col_icons) */
#define COLUMNS_PATH CONFIG_DIR "/columns.txt"

static vita2d_texture *col_tex0[CAT_COUNT];         /* each column's own picture */

/* the pictures of the retro systems (assets/icons/tex_sys_<name>.png, made by tools/make_system_icons.py) follow the stock ones in the list, so the
 * numbers saved in columns.txt and folders.txt stay valid */
#define N_SYS_ICONS 33
static const char *const sys_icon_slug[N_SYS_ICONS] = { "nes", "snes", "gb", "gbc", "gba", "md", "sms", "gg", "scd", "s32x", "pce", "pcecd", "neogeo", "ngp", "ws", "wsc", "a2600", "a5200", "a7800", "lynx", "atarist", "amiga", "c64", "msx", "msx2", "zx", "arcade", "mame", "coleco", "vectrex", "psx", "psp", "psm" };
static const char *const sys_icon_label[N_SYS_ICONS] = { "NES", "SNES", "Game Boy", "Game Boy Color", "Game Boy Advance", "Mega Drive", "Master System", "Game Gear", "Sega CD", "Sega 32X", "PC Engine", "PC Engine CD", "Neo Geo", "Neo Geo Pocket", "WonderSwan", "WonderSwan Color", "Atari 2600", "Atari 5200", "Atari 7800", "Atari Lynx", "Atari ST", "Amiga", "Commodore 64", "MSX", "MSX2", "ZX Spectrum", "Arcade", "MAME", "ColecoVision", "Vectrex", "PlayStation", "PSP", "PSM" };
static const char *const base_icon_label[18] = { "Game", "Music", "Video", "Photo", "Network", "Emulators", "Utilities", "Settings", "Trophies", "Folder", "UMD", "Memory Stick™",
                                                 "Remote Play", "Sharing", "Browser", "RSS", "LocationFree TV", "Saved Data" };
static int sys_icon_first = -1;                     /* where the systems' pictures start in col_icons[] */

static void col_icons_init(void)
{
	if (n_col_icons) return;
	for (int c = 0; c < CAT_COUNT; c++) col_tex0[c] = cat_tex[c];
	vita2d_texture *l[] = { cat_tex[CAT_GAME], cat_tex[CAT_MUSIC], cat_tex[CAT_VIDEO], cat_tex[CAT_PHOTO], cat_tex[CAT_NETWORK], cat_tex[CAT_EMU], cat_tex[CAT_UTIL],
	                        cat_tex[CAT_SETTINGS], tex_trophy, tex_folder, tex_umd, tex_ms_s, tex_remote, tex_sharing, tex_browser, tex_rss, tex_lftv, tex_savedata_s };
	for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]) && n_col_icons < 18; i++)
		if (l[i]) { col_icon_name[n_col_icons] = base_icon_label[i]; col_icons[n_col_icons++] = l[i]; }
	sys_icon_first = n_col_icons;
	for (int i = 0; i < N_SYS_ICONS && n_col_icons < 64; i++) {
		char nm[24];
		snprintf(nm, sizeof(nm), "sys_%s", sys_icon_slug[i]);
		vita2d_texture *t = xicon(nm);
		col_icon_name[n_col_icons] = sys_icon_label[i];
		col_icons[n_col_icons++] = t ? t : tex_folder;                  /* a missing file keeps its place in the list */
	}
}

/* the index in col_icons[] of a system's picture, or -1 */
static int icon_by_slug(const char *slug)
{
	col_icons_init();
	if (!slug || sys_icon_first < 0) return -1;
	for (int i = 0; i < N_SYS_ICONS; i++)
		if (!strcmp(sys_icon_slug[i], slug) && sys_icon_first + i < n_col_icons) return sys_icon_first + i;
	return -1;
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

/* A new column of the user's: the first free one of the 22. Returns it, or -1. */
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
