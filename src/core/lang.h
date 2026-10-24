#pragma once

/* ------------------------------------------------------------------ */
/* Languages: the PSP firmware's own words, in its 12 languages        */
/* ------------------------------------------------------------------ */

/* The UI is written in English. T() looks a text up in the language file of the chosen language (assets/lang/<code>.lng, made by
 * tools/make_lang.py: one "English<TAB>translation" per line, \n for a line break). Most pairs are the firmware's own strings
 * (the RCO text tables of flash0:/vsh/resource); the texts of VitaXMB's own features are translated by hand. A text that is not
 * in the file (English itself, or a language the PSP never had) is shown as it is. */

enum { LANG_JA, LANG_EN, LANG_FR, LANG_ES, LANG_DE, LANG_IT, LANG_NL, LANG_PT, LANG_RU, LANG_KO, LANG_ZHT, LANG_ZHS, LANG_COUNT };
static const char *lang_code[LANG_COUNT] = { "ja", "en", "fr", "es", "de", "it", "nl", "pt", "ru", "ko", "zht", "zhs" };
/* each language in its own words, as the PSP's language list shows them */
static const char *lang_native[LANG_COUNT] = { "日本語", "English", "Français", "Español", "Deutsch", "Italiano", "Nederlands", "Português",
                                               "Русский", "한국어", "繁體中文", "简体中文" };

static void text_cache_flush(void);              /* render/text.h */
static void pt_fb_cache_clear(void);             /* theme/psptheme.h */
static void xlog(const char *fmt, ...);          /* render/texture.h */

static int lang_setting;                 /* 0: the Vita's own language, 1..LANG_COUNT: that language + 1 */
static int lang_cur = LANG_EN;           /* the language in use */

typedef struct { const char *k, *v; } LangKV;
static char *lang_buf;
static LangKV *lang_tab;
static unsigned lang_mask;

static unsigned lang_hash(const char *s)
{
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

static int lang_is_zhs(void) { return lang_cur == LANG_ZHS; }

static const char *T(const char *en)
{
	if (!lang_tab || !en || !en[0]) return en;
	unsigned h = lang_hash(en) & lang_mask;
	while (lang_tab[h].k) {
		if (!strcmp(lang_tab[h].k, en)) return lang_tab[h].v;
		h = (h + 1) & lang_mask;
	}
	return en;
}

/* the Vita's language setting as one of ours (the PSP had no Finnish, Swedish, Danish, Norwegian, Polish or Turkish: English then) */
static int lang_system(void)
{
	static int sys = -2;
	if (sys == -2) {
		sys = LANG_EN;
		SceAppUtilInitParam ip; SceAppUtilBootParam bp;
		memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp));
		sceAppUtilInit(&ip, &bp);
		int v = 1;
		int r = sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &v);
		xlog("system language: %d (call %08x)\n", v, (unsigned)r);
		if (r >= 0) {
			if (v >= 0 && v <= 11) sys = v;                       /* ja en fr es de it nl pt ru ko zh-t zh-s: the same order as the PSP's */
			else if (v == 17) sys = LANG_PT;                      /* Brazilian Portuguese */
		}
	}
	return sys;
}

static void lang_load(int idx)
{
	free(lang_buf); free(lang_tab);
	lang_buf = NULL; lang_tab = NULL; lang_mask = 0;
	lang_cur = idx;
	if (idx == LANG_EN) return;                                   /* the UI's own words */
	char path[64];
	snprintf(path, sizeof(path), "app0:assets/lang/%s.lng", lang_code[idx]);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	if (size <= 0 || size > 4 * 1024 * 1024 || !(lang_buf = malloc((size_t)size + 1)) || sceIoRead(fd, lang_buf, size) != size) {
		sceIoClose(fd); free(lang_buf); lang_buf = NULL; return;
	}
	sceIoClose(fd);
	lang_buf[size] = 0;
	int lines = 0;
	for (int i = 0; i < size; i++) if (lang_buf[i] == '\n') lines++;
	unsigned cap = 64;
	while (cap < (unsigned)lines * 2) cap <<= 1;
	lang_tab = calloc(cap, sizeof(LangKV));
	if (!lang_tab) { free(lang_buf); lang_buf = NULL; return; }
	lang_mask = cap - 1;
	for (char *p = lang_buf; *p; ) {
		char *e = strchr(p, '\n');
		if (!e) e = p + strlen(p);
		char *nx = *e ? e + 1 : e;
		*e = 0;
		if (e > p && e[-1] == '\r') e[-1] = 0;
		char *tab = strchr(p, '\t');
		if (tab && tab > p) {
			*tab = 0;
			char *v = tab + 1;
			for (char *k = p; *k; k++) if (k[0] == '\\' && k[1] == 'n') { k[0] = '\n'; memmove(k + 1, k + 2, strlen(k + 2) + 1); }
			for (char *k = v; *k; k++) if (k[0] == '\\' && k[1] == 'n') { k[0] = '\n'; memmove(k + 1, k + 2, strlen(k + 2) + 1); }
			unsigned h = lang_hash(p) & lang_mask;
			while (lang_tab[h].k) h = (h + 1) & lang_mask;
			lang_tab[h].k = p; lang_tab[h].v = v;
		}
		p = nx;
	}
}

/* Applies lang_setting: the language file and, for scripts the Latin font lacks, the fonts that draw them. */
static void lang_apply(void)
{
	int idx = lang_setting > 0 && lang_setting <= LANG_COUNT ? lang_setting - 1 : lang_system();
	if (idx != lang_cur || (idx != LANG_EN && !lang_buf)) lang_load(idx);
	pt_fb_cache_clear();                                          /* simplified Chinese is drawn by another font than the rest */
	text_cache_flush();
}
