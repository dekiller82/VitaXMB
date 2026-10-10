#pragma once

/* ------------------------------------------------------------------ */
/* Assets                                                              */
/* ------------------------------------------------------------------ */

static vita2d_texture *xicon(const char *name)
{
	char path[96];
	snprintf(path, sizeof(path), "app0:assets/icons/tex_%s.png", name);
	vita2d_texture *t = vita2d_load_PNG_file(path);
	if (t) vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return t;
}

static vita2d_texture *cat_tex[CAT_COUNT];
static vita2d_texture *tex_theme, *tex_psp, *tex_exit, *tex_photo_s, *tex_music_s, *tex_video_s, *tex_net_s, *tex_game_s, *tex_savedata_s, *tex_ms_s, *tex_launch, *tex_badge, *tex_browser, *tex_remote, *tex_sharing, *tex_date, *tex_usb, *tex_rss, *tex_manual, *tex_lftv, *tex_folder, *tex_umd;

static void load_icons(void)
{
	cat_tex[CAT_SETTINGS] = xicon("system");
	cat_tex[CAT_PHOTO]    = xicon("photo");
	cat_tex[CAT_MUSIC]    = xicon("music");
	cat_tex[CAT_VIDEO]    = xicon("video");
	cat_tex[CAT_GAME]     = xicon("game");
	cat_tex[CAT_NETWORK]  = xicon("network");
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
