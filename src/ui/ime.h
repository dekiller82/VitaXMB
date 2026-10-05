#pragma once

/* ---- the system's on-screen keyboard, for naming folders ---- */
enum { IME_NEW, IME_RENAME };
static int ime_active, ime_purpose, ime_target, ime_fake;
static uint16_t ime_title[32], ime_init[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1], ime_buf[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static void u8_to_u16(uint16_t *d, const char *s, int max)
{
	int o = 0;
	const unsigned char *u = (const unsigned char *)s;
	while (*u && o < max - 1) {
		if (u[0] < 0x80) { d[o++] = u[0]; u += 1; }
		else if ((u[0] & 0xE0) == 0xC0 && u[1]) { d[o++] = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
		else if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { d[o++] = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
		else u += 1;
	}
	d[o] = 0;
}

static void u16_to_u8(char *d, const uint16_t *s, int max)
{
	int o = 0;
	for (; *s && o < max - 4; s++) {
		if (*s < 0x80) d[o++] = (char)*s;
		else if (*s < 0x800) { d[o++] = (char)(0xC0 | (*s >> 6)); d[o++] = (char)(0x80 | (*s & 0x3F)); }
		else { d[o++] = (char)(0xE0 | (*s >> 12)); d[o++] = (char)(0x80 | ((*s >> 6) & 0x3F)); d[o++] = (char)(0x80 | (*s & 0x3F)); }
	}
	d[o] = 0;
}

static void ime_begin(const char *title, const char *initial, int purpose, int target)
{
	ime_purpose = purpose;
	ime_target = target;
#ifdef VITAXMB_DEBUG
	if (rc_name[0]) { ime_fake = 1; ime_active = 1; return; }
#endif
	SceImeDialogParam param;
	sceImeDialogParamInit(&param);
	u8_to_u16(ime_title, title, 32);
	u8_to_u16(ime_init, initial, SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1);
	memset(ime_buf, 0, sizeof(ime_buf));
	param.supportedLanguages = 0x0001FFFF;
	param.languagesForced = SCE_FALSE;
	param.type = SCE_IME_TYPE_DEFAULT;
	param.option = 0;
	param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
	param.maxTextLength = 30;
	param.title = ime_title;
	param.initialText = ime_init;
	param.inputTextBuffer = ime_buf;
	if (sceImeDialogInit(&param) >= 0) ime_active = 1;
}

/* 0 while the keyboard is up, 1 when the user confirmed a name, -1 when cancelled. */
static int ime_poll(char *out, int n)
{
	if (ime_fake) {
		ime_fake = 0;
		snprintf(out, n, "%s", rc_name);
		rc_name[0] = 0;
		return 1;
	}
	if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return 0;
	SceImeDialogResult res;
	memset(&res, 0, sizeof(res));
	sceImeDialogGetResult(&res);
	int ok = res.button == SCE_IME_DIALOG_BUTTON_ENTER;
	if (ok) u16_to_u8(out, ime_buf, n);
	sceImeDialogTerm();
	return ok && out[0] ? 1 : -1;
}

static int boot_month;
static void boot_backdrop(float t, float sky)
{
	/* only the sky: the ribbon is drawn by the boot until it becomes the background wave */
	(void)t;
	bg_no_wave = 1;
	bg_alpha = sky;
	draw_background(0.0f, boot_month);
	bg_no_wave = 0;
	bg_alpha = 1.0f;
}
