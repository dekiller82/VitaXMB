#pragma once

static void build_menus(void)
{
	/* Settings: the PSP's own groups, wired to what exists on a Vita */
	Item *nu = add_item(CAT_SETTINGS, KIND_PAGE, "Network Update", "", NULL, tex_launch);      /* first in Settings, as on the PSP */
	if (nu) nu->value_id = PAGE_UPDATE;
	add_folder(CAT_SETTINGS, "System Settings", "", M_SYSSET, tex_psp);
	add_folder(CAT_SETTINGS, "Theme Settings", "", M_THEMESET, tex_theme);
	add_folder(CAT_SETTINGS, "Network Settings", "", M_NETSET, tex_net_s);
	add_folder(CAT_SETTINGS, "VitaXMB Settings", "", M_VITAXMB, tex_launch);
	add_item(CAT_SETTINGS, KIND_EXIT, "Exit to LiveArea", "", NULL, tex_exit);

	Item *pg = add_item(M_SYSSET, KIND_PAGE, "System Information", "", NULL, tex_psp);
	if (pg) pg->value_id = PAGE_SYSINFO;
	add_uri(M_SYSSET, "Vita System Settings", "settings_dlg:", tex_psp);
	add_uri(M_SYSSET, "System Update", "settings_dlg:updater", tex_psp);

	add_value(M_THEMESET, "Color", SET_THEME, tex_theme);
	add_value(M_THEMESET, "Custom Theme (beta)", SET_CUSTOM, tex_theme);
	add_uri(M_THEMESET, "Vita Themes", "settings_dlg:custom_themes", tex_theme);

	add_uri(M_NETSET, "Wi-Fi Settings", "settings_dlg:wifi", tex_net_s);
	add_uri(M_NETSET, "Sign In to PlayStation Network", "settings_dlg:signin", tex_net_s);

	add_value(M_VITAXMB, "Clock Format", SET_CLOCK, tex_date);
	add_value(M_VITAXMB, "Sound Effects", SET_SOUND, tex_music_s);
	add_value(M_VITAXMB, "Startup Animation", SET_STARTUP, tex_launch);
	add_value(M_VITAXMB, "Confirmation Dialogs", SET_CONFIRM, tex_usb);
	add_value(M_VITAXMB, "Game Launch Method", SET_LAUNCH, tex_launch);
	add_value(M_VITAXMB, "Photo Category", SET_SHOW + CAT_PHOTO, tex_photo_s);
	add_value(M_VITAXMB, "Music Category", SET_SHOW + CAT_MUSIC, tex_music_s);
	add_value(M_VITAXMB, "Video Category", SET_SHOW + CAT_VIDEO, tex_video_s);
	add_value(M_VITAXMB, "Network Category", SET_SHOW + CAT_NETWORK, tex_net_s);
	add_value(M_VITAXMB, "Extra Storage (beta)", SET_EXTRA, tex_ms_s);
	add_value(M_VITAXMB, "Check for Updates at Start", SET_AUTOUPDATE, tex_launch);
	add_value(M_VITAXMB, "Decrypt Artwork (beta)", SET_ART, tex_photo_s);
	theme_item_update();

	/* Photo / Music / Video open the Vita's own apps */
	{
		Item *ph = add_uri(CAT_PHOTO, "Memory Stick™", "photo:browse?category=ALL", tex_ms_s);
		if (ph) snprintf(ph->sub, sizeof(ph->sub), "Open in Photos");
	}
	add_folder(CAT_MUSIC, "Memory Stick™", "", M_TRACKS, tex_ms_s);
	scan_music_wrapper();
	add_folder(CAT_VIDEO, "Memory Stick™", "", M_VIDEOS, tex_ms_s);
	scan_videos();

	/* Network: the Vita's online apps */
	/* System apps are opened with their own URI schemes: starting them by title ID
	 * (psgm:play?titleid=NPXS...) makes the system show error C2-12570-5. */
	add_uri(CAT_NETWORK, "Internet Browser", "wbapp0:", tex_browser);
	add_uri(CAT_NETWORK, "PlayStation Store", "psns:browse?category=STORE-MSF73008-VITAGAMES", tex_net_s);
	add_uri(CAT_NETWORK, "Party", "pspy:", tex_sharing);
	add_uri(CAT_NETWORK, "Messages", "psnmsg:", tex_rss);
	add_uri(CAT_NETWORK, "Near", "near:", tex_remote);

	Item *it;
	it = add_item(CAT_GAME, KIND_FOLDER, "Saved Data Utility", "", NULL, tex_savedata_s);
	if (it) it->submenu = M_SAVES;
	it = add_item(CAT_GAME, KIND_FOLDER, "Memory Stick™", "", NULL, tex_ms_s);
	if (it) it->submenu = M_MEMCARD;
	menus[CAT_GAME].sel = 1;

	folders_load();
	scan_apps();
	scan_saves();
	update_game_counts();
}

/* Icon art is decoded on a worker thread (file I/O + PNG decode + downscale are far too slow for
 * the render thread: they caused 40-370 ms hitches while scrolling). This function only decides
 * which rows want art, and uploads finished pixels, a couple per frame. */
