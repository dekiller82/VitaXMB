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

/* The firmware's status pictures redrawn at four times their size (assets/psp/status_*.png, made by tools/make_hires_status.py): the
 * PSP's are tiny bitmaps (the battery's content is 26 x 14 pixels), so at the Vita's 2x they are soft. They are drawn at half size here.
 * names: battery (4 frames of 176 x 64), battery_shadow (4 of 180 x 68), mute, mute_shadow, busy and busy_shadow (30 cells of 68 x 68).
 * NULL when the file is not there; a theme's own pictures are used before these. */
static vita2d_texture *res_hires(const char *name)
{
	static const char *names[6] = { "battery", "battery_shadow", "mute", "mute_shadow", "busy", "busy_shadow" };
	static vita2d_texture *tex[6];
	static unsigned char tried[6];
	for (int i = 0; i < 6; i++) {
		if (strcmp(names[i], name) != 0) continue;
		if (!tried[i]) {
			tried[i] = 1;
			char path[64];
			snprintf(path, sizeof(path), "app0:assets/psp/status_%s.png", name);
			tex[i] = vita2d_load_PNG_file(path);
			if (tex[i]) vita2d_texture_set_filters(tex[i], SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		}
		return tex[i];
	}
	return NULL;
}

/* VitaXMB's own pictures that a theme's resource files can replace: (picture, file, label, the picture's focus glow, its shadow) */
typedef struct { vita2d_texture **own; const char *rco, *label, *focus, *shadow; } ResMap;
static const ResMap res_map[] = {
	{ &tex_badge,  "sysconf_plugin", "tex_sysconf_icon", "tex_sysconf_focus",  "tex_sysconf_shadow_icon" },     /* the icon of every row in the Settings pages */
	{ &tex_folder, "game_plugin",    "tex_directory",    "tex_directory_focus", "tex_directory_shadow" },       /* a folder in the game lists */
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

/* The theme's glow (the "_focus" picture the PSP puts behind the selected row) or shadow for one of those pictures, or NULL. */
static vita2d_texture *res_extra(vita2d_texture *t, int shadow)
{
	for (int i = 0; i < RES_MAP_N; i++)
		if (*res_map[i].own && res_theme(res_map[i].rco, res_map[i].label) == t)
			return res_theme(res_map[i].rco, shadow ? res_map[i].shadow : res_map[i].focus);
	return NULL;
}

/* What a game or save row shows before its icon is there (loading) and when it has none (broken); only a theme's pictures,
 * the stock rows keep the app's own icon. kind: 0 game, 1 save data. */
static vita2d_texture *res_placeholder(int kind, int loading)
{
	static const char *rco[2] = { "game_plugin", "savedata_plugin" };
	static const char *lab[2][2] = { { "tex_broken_data", "tex_loading" }, { "tex_default_icon", "icon_loading" } };
	return res_theme(rco[kind], lab[kind][loading ? 1 : 0]);
}

static int res_owned(vita2d_texture *t)
{
	for (int k = 0; k < 2; k++)
		for (int l = 0; l < 2; l++)
			if (t && res_placeholder(k, l) == t) return 1;
	for (int i = 0; i < RES_MAP_N; i++)
		if (*res_map[i].own && res_theme(res_map[i].rco, res_map[i].label) == t) return 1;
	return 0;
}
