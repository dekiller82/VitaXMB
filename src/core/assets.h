#pragma once

/* ------------------------------------------------------------------ */
/* Assets                                                              */
/* ------------------------------------------------------------------ */

/* The stock icons are 128 px (tools/make_hires_icons.py), twice the PSP's 64: drawing boxes are fixed sizes, so only code that works
 * in texture pixels (the glow) needs to know. icon_k = how many texture pixels make one PSP pixel. */
static vita2d_texture *hi_icons[64];
static int hi_icon_n;

static int icon_k(const vita2d_texture *t)
{
	for (int i = 0; i < hi_icon_n; i++) if (hi_icons[i] == t) return 2;
	return 1;
}

static vita2d_texture *xicon(const char *name)
{
	char path[96];
	snprintf(path, sizeof(path), "app0:assets/icons/tex_%s.png", name);
	vita2d_texture *t = vita2d_load_PNG_file(path);
	if (t) {
		vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		if ((vita2d_texture_get_width(t) == 128 || vita2d_texture_get_width(t) == 56) && hi_icon_n < 64) hi_icons[hi_icon_n++] = t;
	}
	return t;
}

static vita2d_texture *cat_tex[CAT_COUNT];
static vita2d_texture *tex_theme, *tex_psp, *tex_exit, *tex_photo_s, *tex_music_s, *tex_video_s, *tex_net_s, *tex_game_s, *tex_savedata_s, *tex_ms_s, *tex_launch, *tex_badge, *tex_browser, *tex_remote, *tex_sharing, *tex_date, *tex_usb, *tex_rss, *tex_manual, *tex_lftv, *tex_folder, *tex_umd, *tex_trophy, *tex_trophy_cup;

static void load_icons(void)
{
	cat_tex[CAT_SETTINGS] = xicon("system");
	cat_tex[CAT_PHOTO]    = xicon("photo");
	cat_tex[CAT_MUSIC]    = xicon("music");
	cat_tex[CAT_VIDEO]    = xicon("video");
	cat_tex[CAT_GAME]     = xicon("game");
	cat_tex[CAT_NETWORK]  = xicon("network");
	cat_tex[CAT_EMU]      = xicon("emulator");
	cat_tex[CAT_UTIL]     = xicon("utility");
	tex_trophy    = xicon("trophy");
	tex_trophy_cup = xicon("trophy_cup");
	tex_theme   = xicon("cnf_theme");
	tex_psp     = xicon("cnf_psp");
	tex_exit    = xicon("cnf_save_energy");
	tex_photo_s = xicon("cnf_photo");
	tex_music_s = xicon("cnf_sound");
	tex_video_s = xicon("cnf_video");
	tex_net_s   = xicon("cnf_network");
	tex_game_s  = xicon("game");
	tex_savedata_s = xicon("savedata");
	tex_folder  = xicon("folder");
	tex_ms_s    = xicon("ms");
	tex_launch  = xicon("cnf_update");
	tex_badge   = xicon("badge");
	tex_browser = xicon("browser");
	tex_remote  = xicon("remote");
	tex_sharing = xicon("sharing");
	tex_date    = xicon("cnf_date");
	tex_usb     = xicon("cnf_usb");
	tex_rss     = xicon("rss");
	tex_manual  = xicon("manual");
	tex_lftv    = xicon("lftv");
	tex_umd     = xicon("umd");                 /* the disc under Memory Stick: the game started last */
}
