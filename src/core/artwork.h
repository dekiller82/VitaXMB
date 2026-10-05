#pragma once

/* ---- artwork decryption worker ---- */

static char          dec_q[32][16];
static volatile int  dec_qh, dec_qt;
static volatile int  dec_gen;           /* bumps whenever a job finishes */
static volatile int  dec_run = 1;

static void dec_enqueue(const char *id)
{
	int t = dec_qt;
	if ((t + 1) % 32 == dec_qh) return;
	snprintf(dec_q[t], sizeof(dec_q[t]), "%s", id);
	dec_qt = (t + 1) % 32;
}

static void dec_process(const char *id)
{
	if (!vs_init()) return;
	char game[64], base[112], cdir[112], g[112] = "", b[112] = "", dst[130];
	snprintf(game, sizeof(game), "ux0:app/%s", id);
	snprintf(base, sizeof(base), "ux0:app/%s/sce_sys/livearea/contents/", id);

	int r = vs_mount(game);
	if (r < 0) { xlog("dec: mount failed %08x %s\n", r, id); return; }

	livearea_scan(base, g, b);
	sceIoMkdir(CACHE_ROOT, 0777);
	snprintf(cdir, sizeof(cdir), CACHE_ROOT "/%s", id);
	sceIoMkdir(cdir, 0777);
	if (g[0]) { snprintf(dst, sizeof(dst), "%s/gate.png", cdir); if (!copy_file(g, dst)) xlog("dec: copy gate failed %s\n", id); }
	if (b[0]) { snprintf(dst, sizeof(dst), "%s/bg.png", cdir);   if (!copy_file(b, dst)) xlog("dec: copy bg failed %s\n", id); }
	if (!g[0] && !b[0]) xlog("dec: nothing readable after mount %s\n", id);
	vs_umount();
}

static int dec_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	while (dec_run) {
		if (dec_qh != dec_qt) {
			char id[16];
			snprintf(id, sizeof(id), "%s", dec_q[dec_qh]);
			dec_process(id);
			dec_qh = (dec_qh + 1) % 32;
			dec_gen++;
		} else {
			sceKernelDelayThread(40000);
		}
	}
	return 0;
}

/* Finds the gate (rectangle) image and background for an installed app: the decrypted
 * cache first, then LiveArea's copy in ur0:appmeta, then the app's own (plain) files.
 * Retail apps with none of those get queued for decryption. */
static void resolve_meta_impl(Item *it)
{
	if (it->meta_resolved) return;
	it->meta_resolved = 1;
	if (it->kind != KIND_APP || !it->id[0]) return;

	char d[112];
	snprintf(d, sizeof(d), CACHE_ROOT "/%s/", it->id);
	livearea_scan(d, it->gate_path, it->bg_path);
	snprintf(d, sizeof(d), "ur0:appmeta/%s/livearea/contents/", it->id);
	livearea_scan(d, it->gate_path, it->bg_path);
	snprintf(d, sizeof(d), "ux0:app/%s/sce_sys/livearea/contents/", it->id);
	livearea_scan(d, it->gate_path, it->bg_path);

	/* pic0.png (960x544 key art) is the universal fallback; CopyIcons-style copies live in
	 * ur0:appmeta. Only accept real PNGs - the ones under ux0:app are encrypted for retail. */
	{
		char cand[3][112];
		snprintf(cand[0], 112, "ur0:appmeta/%s/pic0.png", it->id);
		snprintf(cand[1], 112, CACHE_ROOT "/%s/pic0.png", it->id);
		snprintf(cand[2], 112, "ux0:app/%s/sce_sys/pic0.png", it->id);
		it->pic_path[0] = 0;
		for (int i = 0; i < 3 && !it->pic_path[0]; i++)
			if (is_png_file(cand[i])) snprintf(it->pic_path, sizeof(it->pic_path), "%s", cand[i]);
		if (!it->gate_path[0] && it->pic_path[0]) snprintf(it->gate_path, sizeof(it->gate_path), "%s", it->pic_path);
	}

	if ((!it->gate_path[0] || !it->bg_path[0]) && art_decrypt && !it->dec_queued) {
		char pfs[96];
		SceIoStat st;
		snprintf(pfs, sizeof(pfs), "ux0:app/%s/sce_pfs", it->id);
		if (sceIoGetstat(pfs, &st) >= 0) {
			dec_enqueue(it->id);
			it->dec_queued = 1;
			return;
		}
	}
	if (!it->gate_path[0] || (!it->bg_path[0] && !it->pic_path[0]))
		xlog("meta %s gate=%s bg=%s%s\n", it->id, it->gate_path[0] ? "ok" : "MISSING",
		     it->bg_path[0] ? "ok" : "MISSING", it->dec_queued ? " (after decrypt)" : "");
}

/* Safe to call from the loader thread. meta_resolved: 0 untouched, 1 working, 2 finished. */
static void resolve_meta(Item *it)
{
	if (it->meta_resolved) return;
	resolve_meta_impl(it);
	__sync_synchronize();
	it->meta_resolved = 2;
}

static int item_cmp(const void *a, const void *b)
{
	return strcasecmp(((const Item *)a)->title, ((const Item *)b)->title);
}

static volatile int loader_pause, loader_busy;

static void clear_menu(int m)
{
	loader_pause = 1;
	while (loader_busy) sceKernelDelayThread(1000);
	Menu *mn = &menus[m];
	for (int i = 0; i < mn->count; i++) {
		if (mn->items[i].icon) defer_free(mn->items[i].icon);
		free(mn->items[i].pending_pix);
		mn->items[i].pending_pix = NULL;
	}
	mn->count = 0;
	loader_pause = 0;
}

static void clean_title(char *t)
{
	for (char *c = t; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
}

static void update_game_counts(void)
{
	Item *saves = &menus[CAT_GAME].items[0];
	Item *card  = &menus[CAT_GAME].items[1];
	int ns = menus[M_SAVES].items[0].kind == KIND_INFO ? 0 : menus[M_SAVES].count;
	int na = menus[M_MEMCARD].items[0].kind == KIND_INFO ? 0 : menus[M_MEMCARD].count;
	(void)ns; (void)na;
	saves->sub[0] = 0;
	uint64_t max_size = 0, free_size = 0;
	if (sceAppMgrGetDevInfo("ux0:", &max_size, &free_size) >= 0 && max_size) {
		if (free_size >= (1ULL << 30))
			snprintf(card->sub, sizeof(card->sub), "Free Space  %u GB", (unsigned)((free_size + (1ULL << 29)) >> 30));
		else
			snprintf(card->sub, sizeof(card->sub), "Free Space  %u MB", (unsigned)(free_size >> 20));
	} else {
		card->sub[0] = 0;
	}
}
