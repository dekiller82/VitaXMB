#pragma once

/*
 * resources.h - pictures taken by name from the active theme's own RCO files.
 *
 * A theme ships the same resource files as the firmware (sysconf_plugin.rco for the Settings pages, game_plugin.rco
 * for the game lists, system_plugin_fg.rco for the button glyphs, savedata_plugin.rco for the separator line...). Each
 * holds labelled pictures ("tex_sysconf_icon", "tex_directory", "tex_cross"). res_theme() finds one in the theme's file;
 * it returns NULL when the theme (or that file, or that picture) is not there, and the caller keeps its own.
 */

typedef struct { char name[40]; Rco *r; int tried; } ResRco;
static ResRco res_rco[12];

static void res_reset(void)
{
	for (int i = 0; i < 12; i++) {
		if (res_rco[i].r) rco_free(res_rco[i].r);
		memset(&res_rco[i], 0, sizeof(res_rco[i]));
	}
}

static vita2d_texture *res_theme(const char *rco, const char *label)
{
	if (pt_active < 0) return NULL;
	ResRco *e = NULL;
	for (int i = 0; i < 12; i++) {
		if (res_rco[i].name[0] && strcmp(res_rco[i].name, rco) == 0) { e = &res_rco[i]; break; }
		if (!res_rco[i].name[0]) { e = &res_rco[i]; snprintf(e->name, sizeof(e->name), "%s", rco); break; }
	}
	if (!e) return NULL;
	if (!e->tried) {
		e->tried = 1;
		char path[96];
		snprintf(path, sizeof(path), "/vsh/resource/%s.rco", rco);
		size_t n = 0;
		uint8_t *d = pt_active_file(path, &n);
		if (d) e->r = rco_open(d, n);
	}
	return e->r ? rco_image_by_label(e->r, label) : NULL;
}

/* The status bar's pictures (tex_mute, tex_busy ...): the theme's, else the firmware's own from assets/psp (a flat copy of
 * the stock system_plugin_fg.rco, made with tools/make_flat_rco.py). */
static uint8_t *boot_read_file(const char *path, size_t *n);
static Rco *res_stock_fg;
static int res_stock_fg_tried;

static vita2d_texture *res_fg(const char *label)
{
	vita2d_texture *t = res_theme("system_plugin_fg", label);
	if (t) return t;
	if (!res_stock_fg_tried) {
		res_stock_fg_tried = 1;
		size_t n = 0;
		uint8_t *d = boot_read_file("app0:assets/psp/system_plugin_fg.rco", &n);
		if (d) res_stock_fg = rco_open(d, n);
	}
	return res_stock_fg ? rco_image_by_label(res_stock_fg, label) : NULL;
}

/* VitaXMB's own pictures that a theme's resource files can replace: (picture, file, label) */
typedef struct { vita2d_texture **own; const char *rco, *label; } ResMap;
static const ResMap res_map[] = {
	{ &tex_badge,  "sysconf_plugin", "tex_sysconf_icon" },          /* the icon of every row in the Settings pages */
	{ &tex_folder, "game_plugin",    "tex_directory" },             /* a folder in the game lists */
};
#define RES_MAP_N ((int)(sizeof(res_map) / sizeof(res_map[0])))

/* the theme's version of one of VitaXMB's pictures (or the picture itself) */
static vita2d_texture *res_swap(vita2d_texture *t)
{
	for (int i = 0; i < RES_MAP_N; i++)
		if (*res_map[i].own == t) {
			vita2d_texture *o = res_theme(res_map[i].rco, res_map[i].label);
			return o ? o : t;
		}
	return t;
}

static int res_owned(vita2d_texture *t)
{
	for (int i = 0; i < RES_MAP_N; i++)
		if (*res_map[i].own && res_theme(res_map[i].rco, res_map[i].label) == t) return 1;
	return 0;
}
