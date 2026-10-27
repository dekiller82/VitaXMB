#pragma once

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

/* Position of a category on the bar once the hidden ones are left out (the user's order), and the category at a position. */
static int cat_slot(int c)
{
	int n = 0;
	for (int k = 0; k < CAT_COUNT; k++) {
		int i = cat_order[k];
		if (i == c) break;
		if (!cat_hidden[i]) n++;
	}
	return n;
}

static int cat_at_slot(int s)
{
	int n = 0;
	for (int k = 0; k < CAT_COUNT; k++) {
		int i = cat_order[k];
		if (cat_hidden[i]) continue;
		if (n++ == s) return i;
	}
	return -1;
}

static int cat_can_hide(int c) { return c != CAT_SETTINGS && c != CAT_GAME; }

/* Emulators and Utilities are on the bar only while something is in them (and the user has not hidden them); a column of the user's own
 * only once it is made. `keep`, the category the cursor is on, stays even when it has just been emptied. */
static int cat_is_auto(int c) { return c == CAT_EMU || c == CAT_UTIL; }
static int cols_limited(void);                    /* core/folders.h */

static void cat_refresh_hidden(int keep)
{
	for (int c = 0; c < CAT_COUNT; c++) {
		int h = 0;
		if (c >= CAT_C1 && !col_custom[c]) h = 1;
		else if (cat_can_hide(c) && (cat_pref[c] || (cat_is_auto(c) && c != keep && col_used[c] == 0))) h = 1;
		if (cols_limited() && c >= CAT_EMU && c != keep) h = 1;       /* a theme with its own panels has no picture for the extra columns */
		cat_hidden[c] = h;
	}
}

static void config_load(void)
{
	SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	int v[16] = { 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0 };
	int n = sceIoRead(fd, v, sizeof(v));
	sceIoClose(fd);
	if (n >= 4 && v[0] >= 0 && v[0] <= 12) theme = v[0];
	if (n >= 8 && v[1] >= 0 && v[1] <= 3) launch_mode = v[1];
	if (n >= 12 && (v[2] == 0 || v[2] == 1)) art_decrypt = v[2];
	if (n >= 16 && (v[3] == 0 || v[3] == 1)) clock24 = v[3];
	if (n >= 20 && (v[4] == 0 || v[4] == 1)) sound_on = v[4];
	if (n >= 24 && (v[5] == 0 || v[5] == 1)) startup_anim = v[5];
	if (n >= 28 && (v[6] == 0 || v[6] == 1)) confirm_dialogs = v[6];
	if (n >= 32 && (v[7] == 0 || v[7] == 1)) extra_storage = v[7];
	if (n >= 36 && v[8] >= 0 && v[8] < (1 << CAT_COUNT))
		for (int c = 0; c < CAT_COUNT; c++) cat_pref[c] = cat_can_hide(c) && ((v[8] >> c) & 1);
	if (n >= 40 && (v[9] == 0 || v[9] == 1)) auto_update = v[9];
	if (n >= 44 && (v[10] == 0 || v[10] == 1)) batt_pct = v[10];
	if (n >= 48 && (v[11] == 0 || v[11] == 1)) nla_asked = v[11];
	if (n >= 52 && (v[12] == 0 || v[12] == 1)) boot_logo = v[12];
	if (n >= 56 && v[13] >= 0 && v[13] <= LANG_COUNT) lang_setting = v[13];
	if (n >= 60 && (v[14] == 0 || v[14] == 1)) sort_asked = v[14];
	if (n >= 64 && (v[15] == 0 || v[15] == 1)) retro_emu = v[15];
	cat_refresh_hidden(-1);
}

static void ensure_dirs(void)
{
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(CONFIG_DIR, 0777);
}

static void config_save(void)
{
	ensure_dirs();
	SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return;
	int mask = 0;
	for (int c = 0; c < CAT_COUNT; c++) if (cat_pref[c]) mask |= 1 << c;
	int v[16] = { theme, launch_mode, art_decrypt, clock24, sound_on, startup_anim, confirm_dialogs, extra_storage, mask, auto_update, batt_pct, nla_asked, boot_logo, lang_setting, sort_asked, retro_emu };
	sceIoWrite(fd, v, sizeof(v));
	sceIoClose(fd);
}
