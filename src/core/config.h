#pragma once

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

/* Position of a category on the bar once the hidden ones are left out. */
static int cat_slot(int c)
{
	int n = 0;
	for (int i = 0; i < c; i++) if (!cat_hidden[i]) n++;
	return n;
}

static int cat_can_hide(int c) { return c != CAT_SETTINGS && c != CAT_GAME; }

static void config_load(void)
{
	SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	int v[10] = { 0, 0, 0, 1, 1, 1, 1, 0, 0, 1 };
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
		for (int c = 0; c < CAT_COUNT; c++) cat_hidden[c] = cat_can_hide(c) && ((v[8] >> c) & 1);
	if (n >= 40 && (v[9] == 0 || v[9] == 1)) auto_update = v[9];
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
	for (int c = 0; c < CAT_COUNT; c++) if (cat_hidden[c]) mask |= 1 << c;
	int v[10] = { theme, launch_mode, art_decrypt, clock24, sound_on, startup_anim, confirm_dialogs, extra_storage, mask, auto_update };
	sceIoWrite(fd, v, sizeof(v));
	sceIoClose(fd);
}
