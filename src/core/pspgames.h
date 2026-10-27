#pragma once

/* ------------------------------------------------------------------ */
/* Every PSP and PS1 game of Adrenaline, bubble or not                 */
/* ------------------------------------------------------------------ */

/* Adrenaline only makes a LiveArea bubble for the games its bubble tool was run for. Games are listed from its folders instead:
 * ux0:pspemu/ISO (ISO and CSO images) and ux0:pspemu/PSP/GAME/<name>/EBOOT.PBP (PSP downloads, PS1 classics, homebrew). A game that
 * already has a bubble stays that bubble's entry. The title, the icon and the background come out of the game's own file (PARAM.SFO,
 * ICON0.PNG, PIC1.PNG); the two pictures are kept as PNG files in ux0:data/VitaXMB/psp for the icon loader.
 *
 * Starting a game: through its own bubble when Adrenaline Bubble Manager's modules are installed, otherwise through VitaXMB's launcher (psp/launcher),
 * which Adrenaline's Autorun option starts: see psp_autorun_prepare below. */

#define PSP_CACHE_DIR CONFIG_DIR "/psp"

static struct { char id[16]; char path[192]; } psp_bubbles[48];       /* the bubbles Adrenaline's tool made: the game each one starts */
static int psp_n_bubbles;

static unsigned psp_hash(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s)) * 16777619u;
	return h;
}

static void psp_find_bubbles(void)
{
	psp_n_bubbles = 0;
	SceUID d = sceIoDopen("ux0:app");
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0 && psp_n_bubbles < 48) {
		if (!strncmp(e.d_name, "PSPEMU", 6) && strcmp(e.d_name, "PSPEMUCFW") && strlen(e.d_name) < 16 && SCE_S_ISDIR(e.d_stat.st_mode)) {
			char game[192];
			if (pm_bubble_game(e.d_name, game, sizeof(game))) {
				snprintf(psp_bubbles[psp_n_bubbles].id, sizeof(psp_bubbles[0].id), "%s", e.d_name);
				snprintf(psp_bubbles[psp_n_bubbles].path, sizeof(psp_bubbles[0].path), "%s", game);
				psp_n_bubbles++;
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

/* the bubble that starts this game file, or NULL */
static const char *psp_bubble_for(const char *path)
{
	for (int i = 0; i < psp_n_bubbles; i++)
		if (!strcasecmp(psp_bubbles[i].path, path)) return psp_bubbles[i].id;
	return NULL;
}

static int psp_exists(const char *p)
{
	SceIoStat st;
	return sceIoGetstat(p, &st) >= 0;
}

/* a file of the game as a file of its own (the picture loaders read files) */
static int psp_extract(PmSrc *src, int what, const char *dst)
{
	if (psp_exists(dst)) return 1;
	size_t n = 0;
	uint8_t *b = pm_game_file(src, what, &n);
	if (!b) return 0;
	SceUID fd = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	int ok = fd >= 0 && sceIoWrite(fd, b, (SceSize)n) == (int)n;
	if (fd >= 0) sceIoClose(fd);
	free(b);
	if (!ok) sceIoRemove(dst);
	return ok;
}

/* the bytes of a name that are not UTF-8 (Latin-1 file names) become '?' */
static void psp_utf8_fix(char *s)
{
	unsigned char *p = (unsigned char *)s;
	while (*p) {
		int n = *p < 0x80 ? 1 : ((*p & 0xE0) == 0xC0 ? 2 : ((*p & 0xF0) == 0xE0 ? 3 : ((*p & 0xF8) == 0xF0 ? 4 : 0)));
		int ok = n > 0;
		for (int i = 1; ok && i < n; i++) if ((p[i] & 0xC0) != 0x80) ok = 0;
		if (ok) p += n; else *p++ = '?';
	}
}

static void psp_add_game(const char *path, const char *fallback_name)
{
	if (psp_bubble_for(path)) return;                                 /* its bubble is in the list already */
	PmSrc src;
	if (!pm_src_open(&src, path)) return;
	char title[64] = "", disc[16] = "", cat[8] = "", id[16];
	size_t n = 0;
	uint8_t *sfo = pm_game_file(&src, PM_F_SFO, &n);
	if (sfo) {
		pm_sfo_string(sfo, n, "TITLE", title, sizeof(title));
		pm_sfo_string(sfo, n, "DISC_ID", disc, sizeof(disc));
		pm_sfo_string(sfo, n, "CATEGORY", cat, sizeof(cat));
		free(sfo);
	}
	if (!title[0]) snprintf(title, sizeof(title), "%s", fallback_name);
	psp_utf8_fix(title);
	snprintf(id, sizeof(id), "P%08X", psp_hash(path));
	clean_title(title);
	Item *it = add_item(M_MEMCARD, KIND_APP, title, disc, id, tex_game_s);
	if (it) {
		sceIoMkdir(PSP_CACHE_DIR, 0777);
		char png[96], bg[96];
		snprintf(png, sizeof(png), PSP_CACHE_DIR "/%s.png", id);
		snprintf(bg, sizeof(bg), PSP_CACHE_DIR "/%s_bg.png", id);
		if (psp_extract(&src, PM_F_ICON0, png)) snprintf(it->icon_path, sizeof(it->icon_path), "%s", png);
		if (psp_extract(&src, PM_F_PIC1, bg)) snprintf(it->pic_path, sizeof(it->pic_path), "%s", bg);
		snprintf(it->path, sizeof(it->path), "%s", path);
		it->org = !strcmp(cat, "ME") ? 2 : 1;                           /* PS1 classics are "ME", everything else is a PSP title */
	}
	pm_src_close(&src);
}

static void scan_psp_games(void)
{
	psp_find_bubbles();
	SceIoDirent e;
	SceUID d = sceIoDopen("ux0:pspemu/ISO");
	if (d >= 0) {
		memset(&e, 0, sizeof(e));
		while (sceIoDread(d, &e) > 0) {
			if (!SCE_S_ISDIR(e.d_stat.st_mode) && (has_ci(e.d_name, ".iso") || has_ci(e.d_name, ".cso"))) {
				char path[256], name[64];
				snprintf(path, sizeof(path), "ux0:pspemu/ISO/%s", e.d_name);
				snprintf(name, sizeof(name), "%.*s", (int)(strrchr(e.d_name, '.') - e.d_name) > 60 ? 60 : (int)(strrchr(e.d_name, '.') - e.d_name), e.d_name);
				psp_add_game(path, name);
			}
			memset(&e, 0, sizeof(e));
		}
		sceIoDclose(d);
	}
	d = sceIoDopen("ux0:pspemu/PSP/GAME");
	if (d >= 0) {
		memset(&e, 0, sizeof(e));
		while (sceIoDread(d, &e) > 0) {
			if (SCE_S_ISDIR(e.d_stat.st_mode)) {
				char path[256];
				snprintf(path, sizeof(path), "ux0:pspemu/PSP/GAME/%s/EBOOT.PBP", e.d_name);
				if (psp_exists(path)) psp_add_game(path, e.d_name);
			}
			memset(&e, 0, sizeof(e));
		}
		sceIoDclose(d);
	}
}

/* ---- starting a game that has no bubble ---- */

/* Starting a game that has no bubble (or whose bubble cannot start it): the launcher of psp/launcher. Adrenaline 8 starts ms0:/PSP/GAME/BOOT/EBOOT.PBP by
 * itself when its "Autorun" option is on (systemctrl's init_patch.c). That file is VitaXMB's launcher: it reads the game's path from VXMB.TXT next to it,
 * deletes it and starts the ISO / CSO / PBP. So, before Adrenaline is opened, the launcher is put in place, the path is written and Autorun is
 * switched on (one byte of flash1/config.adrenaline); the byte goes back the next time VitaXMB starts. */

#define PSP_BOOT_DIR "ux0:pspemu/PSP/GAME/BOOT"
#define PSP_AUTORUN_STATE CONFIG_DIR "/psp_autorun.txt"
#define ADR_CFG "ux0:app/PSPEMUCFW/flash1/config.adrenaline"
#define ADR_CFG_STARTUP 10                    /* int magic[2], then u8: hide_corrupt, skip_logo, startup_program */

static uint32_t psp_crc_file(const char *p)
{
	SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	static uint8_t buf[32768];
	uint32_t c = crc32(0, NULL, 0);
	int n, any = 0;
	while ((n = sceIoRead(fd, buf, sizeof(buf))) > 0) { c = crc32(c, buf, (uInt)n); any = 1; }
	sceIoClose(fd);
	return any ? c : 0;
}

static int psp_write_file(const char *p, const void *d, int n)
{
	SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return 0;
	int ok = sceIoWrite(fd, d, (SceSize)n) == n;
	sceIoClose(fd);
	return ok;
}

/* the one byte of Adrenaline's config; set < 0 only reads. Returns the old value, or -1 when the file is not an Adrenaline config / not writable. */
static int psp_cfg_startup(int set)
{
	SceUID fd = sceIoOpen(ADR_CFG, SCE_O_RDONLY, 0);
	uint8_t b[128];
	int n = fd >= 0 ? sceIoRead(fd, b, sizeof(b)) : -1;
	if (fd >= 0) sceIoClose(fd);
	if (n < 16 || pt_rd32(b) != 0x192EFC3Cu || pt_rd32(b + 4) != 0x17BEB6AAu) return -1;
	int old = b[ADR_CFG_STARTUP];
	if (set < 0 || set == old) return old;
	b[ADR_CFG_STARTUP] = (uint8_t)set;
	for (int attempt = 0; attempt < 2; attempt++) {
		SceUID w = sceIoOpen(ADR_CFG, SCE_O_WRONLY, 0);
		int ok = w >= 0 && sceIoWrite(w, b, (SceSize)n) == n;
		if (w >= 0) sceIoClose(w);
		if (ok) return old;
		if (attempt == 0 && !vs_init()) break;                          /* VitaShell's patch lets an app write into ux0:app */
	}
	return -1;
}

/* undoes a launch whose game did not start, or the one that did: Autorun back to what it was, no stale path */
static void psp_autorun_restore(void)
{
	FILE *f = fopen(PSP_AUTORUN_STATE, "rb");
	if (f) {
		int orig = -1;
		if (fscanf(f, "%d", &orig) != 1) orig = -1;
		fclose(f);
		if (orig >= 0 && orig <= 1) psp_cfg_startup(orig);
		sceIoRemove(PSP_AUTORUN_STATE);
	}
	if (psp_exists(PSP_BOOT_DIR "/VXMB.TXT")) sceIoRemove(PSP_BOOT_DIR "/VXMB.TXT");
}

/* puts the launcher, the path and the Autorun switch in place; 1 when Adrenaline can be opened to start the game */
static int psp_autorun_prepare(const char *game)
{
	uint32_t ours = psp_crc_file("app0:assets/psp/vxmbboot.pbp");
	if (!ours) { xlog("psp launch: the launcher file is missing from this install\n"); return 0; }
	psp_autorun_restore();
	sceIoMkdir("ux0:pspemu", 0777); sceIoMkdir("ux0:pspemu/PSP", 0777); sceIoMkdir("ux0:pspemu/PSP/GAME", 0777); sceIoMkdir(PSP_BOOT_DIR, 0777);
	uint32_t have = psp_crc_file(PSP_BOOT_DIR "/EBOOT.PBP");
	if (have != ours) {
		if (have) { xlog("psp launch: %s/EBOOT.PBP is somebody else's autorun program: not touching it\n", PSP_BOOT_DIR); return 0; }
		SceUID in = sceIoOpen("app0:assets/psp/vxmbboot.pbp", SCE_O_RDONLY, 0);
		int size = in >= 0 ? sceIoLseek32(in, 0, SCE_SEEK_END) : -1;
		uint8_t *b = size > 0 ? malloc((size_t)size) : NULL;
		if (in >= 0) sceIoLseek32(in, 0, SCE_SEEK_SET);
		int ok = b && sceIoRead(in, b, size) == size && psp_write_file(PSP_BOOT_DIR "/EBOOT.PBP", b, size);
		if (in >= 0) sceIoClose(in);
		free(b);
		if (!ok) { xlog("psp launch: could not put the launcher in %s\n", PSP_BOOT_DIR); return 0; }
	}
	char txt[300];
	const char *p = game;
	if (!strncasecmp(p, "ux0:pspemu/", 11)) snprintf(txt, sizeof(txt), "ms0:/%s", p + 11);        /* Adrenaline's ms0: is ux0:pspemu */
	else snprintf(txt, sizeof(txt), "%s", p);
	if (!psp_write_file(PSP_BOOT_DIR "/VXMB.TXT", txt, (int)strlen(txt))) { xlog("psp launch: could not write VXMB.TXT\n"); return 0; }
	int old = psp_cfg_startup(-1);
	if (old < 0) { xlog("psp launch: Adrenaline's config.adrenaline cannot be read\n"); sceIoRemove(PSP_BOOT_DIR "/VXMB.TXT"); return 0; }
	FILE *sf = fopen(PSP_AUTORUN_STATE, "wb");
	if (sf) { fprintf(sf, "%d\n", old); fclose(sf); }
	if (psp_cfg_startup(1) < 0) { xlog("psp launch: could not switch Autorun on\n"); psp_autorun_restore(); return 0; }
	xlog("psp launch: autorun on, launcher will start %s\n", txt);
	return 1;
}

/* Adrenaline Bubble Manager's modules: with them Adrenaline starts a bubble's game by itself; without them a bubble only shows the launcher's error */
static int psp_abm_modules(void)
{
	return psp_exists("ux0:app/PSPEMUCFW/sce_module/adrbubblebooter.suprx") && psp_exists("ux0:app/PSPEMUCFW/sce_module/bootconv.suprx");
}

/* the title ID to start for a listed game: its own bubble when that can work, else Adrenaline itself with the launcher doing the rest */
static const char *psp_launch_id(const Item *it, int *fallback)
{
	*fallback = 0;
	psp_find_bubbles();
	const char *own = psp_bubble_for(it->path);
	if (own && psp_abm_modules()) { xlog("psp launch: %s has its own bubble %s\n", it->path, own); return own; }
	if (!psp_autorun_prepare(it->path)) *fallback = 1;
	return "PSPEMUCFW";
}
