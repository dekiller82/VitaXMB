#pragma once

static void rescan_media(void);
static void update_music_count(void);              /* media/media_scan.h */
static void rebuild_game_lists(void);              /* core/folders.h */

static const char *theme_names[13] = { "Automatic (by month)", "January", "February", "March", "April",
	"May", "June", "July", "August", "September", "October", "November", "December" };
static const char *launch_names[4] = { "A: standard", "B: open flag", "C: launch flag", "D: stay open" };

/* shown through T() where they are drawn: the names of the themes are the PSP's own month names */

static void setting_text(int id, char *out, size_t n)
{
	const char *onoff[2] = { T("Off"), T("On") };
	switch (id) {
	case SET_THEME:   snprintf(out, n, "%s", T(theme_names[theme])); break;
	case SET_CLOCK:   snprintf(out, n, "%s", clock24 ? T("24-hour") : T("12-hour")); break;
	case SET_LANG:
		if (lang_setting == 0) snprintf(out, n, "%s (%s)", T("Automatic"), lang_native[lang_system()]);
		else snprintf(out, n, "%s", lang_native[lang_setting - 1]);
		break;
	case SET_SOUND:   snprintf(out, n, "%s", onoff[sound_on]); break;
	case SET_STARTUP: snprintf(out, n, "%s", onoff[startup_anim]); break;
	case SET_BOOTLOGO: snprintf(out, n, "%s", boot_logo ? "PSV / PlayStation Vita" : "PSP / PlayStation Portable"); break;
	case SET_AUTOUPDATE: snprintf(out, n, "%s", onoff[auto_update]); break;
	case SET_BATTPCT: snprintf(out, n, "%s", onoff[batt_pct]); break;
	case SET_NOLA:    snprintf(out, n, "%s", nla_bundled() ? onoff[nla_on] : T("Unavailable")); break;
	case SET_CONFIRM: snprintf(out, n, "%s", onoff[confirm_dialogs]); break;
	case SET_LAUNCH:  snprintf(out, n, "%s", T(launch_names[launch_mode])); break;
	case SET_ART:     snprintf(out, n, "%s", onoff[art_decrypt]); break;
	case SET_CUSTOM:  pt_text(out, n); break;
	case SET_EXTRA: {
		if (!extra_storage) { snprintf(out, n, "%s", T("Off")); break; }
		char devs[4][8];
		int nd = extra_devs(devs);
		if (nd == 0) snprintf(out, n, "%s", T("On (none)"));
		else if (nd == 1) snprintf(out, n, T("On (%s)"), devs[0]);
		else snprintf(out, n, T("On (%d cards)"), nd);
		break; }
	default:
		if (id >= SET_SHOW && id < SET_SHOW + CAT_COUNT) snprintf(out, n, "%s", cat_pref[id - SET_SHOW] ? T("Hidden") : (cat_is_auto(id - SET_SHOW) ? T("Automatic") : T("Shown")));
		else out[0] = 0;
	}
}

/* Re-reads every value row's text after a setting changed. */
static void theme_item_update(void)
{
	for (int m = 0; m < M_COUNT; m++) {
		Menu *mn = &menus[m];
		for (int i = 0; i < mn->count; i++)
			if (mn->items[i].kind == KIND_VALUE)
				setting_text(mn->items[i].value_id, mn->items[i].sub, sizeof(mn->items[i].sub));
	}
}

static void setting_change(int id, int dir)
{
	switch (id) {
	case SET_THEME:   theme = (theme + dir + 13) % 13; break;
	case SET_CLOCK:   clock24 = !clock24; break;
	case SET_LANG:
		lang_setting = (lang_setting + (dir >= 0 ? 1 : -1) + LANG_COUNT + 1) % (LANG_COUNT + 1);
		lang_apply();
		update_game_counts(); update_music_count(); rebuild_game_lists();          /* "Free Space", "N songs", "N games" carry the old language */
		break;
	case SET_SOUND:   sound_on = !sound_on; break;
	case SET_STARTUP: startup_anim = !startup_anim; break;
	case SET_BOOTLOGO: boot_logo = !boot_logo; break;
	case SET_AUTOUPDATE: auto_update = !auto_update; break;
	case SET_BATTPCT: batt_pct = !batt_pct; break;
	case SET_NOLA:    nla_toggle(); nla_on = nla_enabled(); break;
	case SET_CONFIRM: confirm_dialogs = !confirm_dialogs; break;
	case SET_LAUNCH:  launch_mode = (launch_mode + dir + 4) % 4; break;
	case SET_EXTRA:
		extra_storage = !extra_storage;
		rescan_media();
		break;
	case SET_CUSTOM:  pt_cycle(dir >= 0 ? 1 : -1); break;
	default:
		if (id >= SET_SHOW && id < SET_SHOW + CAT_COUNT && cat_can_hide(id - SET_SHOW))
			{ cat_pref[id - SET_SHOW] = !cat_pref[id - SET_SHOW]; cat_refresh_hidden(cat_now); }
		break;
	case SET_ART:
		art_decrypt = !art_decrypt;
		if (art_decrypt) {
			Menu *am = &menus[M_MEMCARD];
			for (int i = 0; i < am->count; i++) { am->items[i].meta_resolved = 0; am->items[i].dec_queued = 0; }
		}
		break;
	}
	theme_item_update();
	config_save();
}

static Item *add_value(int m, const char *title, int id, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_VALUE, title, "", NULL, icon);
	if (it) it->value_id = id;
	return it;
}

static Item *add_uri(int m, const char *title, const char *uri, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_URI, title, "", NULL, icon);
	if (it) snprintf(it->uri, sizeof(it->uri), "%s", uri);
	return it;
}

static Item *add_folder(int m, const char *title, const char *sub, int submenu, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_FOLDER, title, sub, NULL, icon);
	if (it) it->submenu = submenu;
	return it;
}
