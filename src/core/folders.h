#pragma once

/* ---- user folders: the games are grouped in ux0:data/VitaXMB/folders.txt ----
 *   F<TAB>name        starts a folder
 *   A<TAB>titleid     puts that app in the folder above
 * The list shown in Memory Card is: the folders, then the apps that are in none. */

#define MAX_FOLDERS 32
#define MAX_ASSIGN  GAME_MAX
#define FOLDERS_PATH CONFIG_DIR "/folders.txt"

static char folder_name[MAX_FOLDERS][40];
static int n_folders;
static struct { char id[16]; int folder; } assign[MAX_ASSIGN];
static int n_assign;
static Item *all_apps;                  /* every installed app (GAME_MAX, allocated by scan_apps); the menus hold copies */
static int n_all;
static short app_fold[GAME_MAX];        /* folder of all_apps[i], refreshed by refresh_app_folders() */
static int open_folder = -1;            /* folder shown in M_FOLDER */

static int app_folder(const char *id)
{
	for (int i = 0; i < n_assign; i++)
		if (!strcmp(assign[i].id, id)) return assign[i].folder;
	return -1;
}

static void app_set_folder(const char *id, int f)
{
	for (int i = 0; i < n_assign; i++)
		if (!strcmp(assign[i].id, id)) {
			if (f < 0) assign[i] = assign[--n_assign];
			else assign[i].folder = f;
			return;
		}
	if (f >= 0 && n_assign < MAX_ASSIGN) {
		snprintf(assign[n_assign].id, sizeof(assign[0].id), "%s", id);
		assign[n_assign++].folder = f;
	}
}

static void folders_save(void)
{
	FILE *f = fopen(FOLDERS_PATH, "wb");
	if (!f) return;
	for (int i = 0; i < n_folders; i++) {
		fprintf(f, "F\t%s\n", folder_name[i]);
		for (int k = 0; k < n_assign; k++)
			if (assign[k].folder == i) fprintf(f, "A\t%s\n", assign[k].id);
	}
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
		} else if (line[0] == 'A' && line[1] == '\t' && cur >= 0 && n_assign < MAX_ASSIGN) {
			snprintf(assign[n_assign].id, sizeof(assign[0].id), "%s", line + 2);
			assign[n_assign++].folder = cur;
		}
	}
	fclose(f);
}

static void folder_default_name(char *out, int n)
{
	for (int k = 1; k < 100; k++) {
		snprintf(out, n, "Folder %d", k);
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

static int folder_new(const char *name)
{
	if (n_folders >= MAX_FOLDERS) return -1;
	int f = n_folders++;
	folder_clean_name(folder_name[f], sizeof(folder_name[0]), name);
	if (!folder_name[f][0]) folder_default_name(folder_name[f], sizeof(folder_name[0]));
	return f;
}

static void folder_delete(int f)
{
	for (int i = 0; i < n_assign; ) {
		if (assign[i].folder == f) assign[i] = assign[--n_assign];
		else { if (assign[i].folder > f) assign[i].folder--; i++; }
	}
	for (int i = f; i < n_folders - 1; i++) memcpy(folder_name[i], folder_name[i + 1], sizeof(folder_name[0]));
	n_folders--;
}

/* app_folder() walks the assignment list, so look every app up once per rebuild instead of once per folder. */
static void refresh_app_folders(void)
{
	for (int i = 0; i < n_all; i++) app_fold[i] = (short)app_folder(all_apps[i].id);
}

static int folder_count(int f)
{
	int c = 0;
	for (int i = 0; i < n_all; i++) if (app_fold[i] == f) c++;
	return c;
}

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
		add_item(M_FOLDER, KIND_INFO, "Empty folder", "Options > Select Games", NULL, tex_folder);
	Menu *mn = &menus[M_FOLDER];
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (mn->sel < 0) mn->sel = 0;
	mn->pos = (float)mn->sel;
	loader_pause = 0;
}

static void rebuild_game_lists(void)
{
	Menu *mn = &menus[M_MEMCARD];
	refresh_app_folders();
	clear_menu(M_MEMCARD);
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);

	int order[MAX_FOLDERS];
	for (int i = 0; i < n_folders; i++) order[i] = i;
	for (int i = 1; i < n_folders; i++) {                     /* insertion sort by name */
		int v = order[i], k = i - 1;
		while (k >= 0 && strcasecmp(folder_name[order[k]], folder_name[v]) > 0) { order[k + 1] = order[k]; k--; }
		order[k + 1] = v;
	}
	for (int i = 0; i < n_folders; i++) {
		int f = order[i], c = folder_count(f);
		char sub[32];
		if (c == 0) snprintf(sub, sizeof(sub), "Empty");
		else snprintf(sub, sizeof(sub), "%d game%s", c, c == 1 ? "" : "s");
		Item *it = add_item(M_MEMCARD, KIND_FOLDER, folder_name[f], sub, NULL, tex_folder);
		if (it) { it->submenu = M_FOLDER; it->value_id = f; }
	}
	for (int i = 0; i < n_all; i++)
		if (app_fold[i] < 0) menu_add_app(M_MEMCARD, &all_apps[i]);
	if (mn->count == 0)
		add_item(M_MEMCARD, KIND_INFO, "No games found", "Nothing in ux0:app", NULL, tex_game_s);
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (mn->sel < 0) mn->sel = 0;
	mn->pos = (float)mn->sel;
	loader_pause = 0;
	fill_folder_menu(open_folder);
}
