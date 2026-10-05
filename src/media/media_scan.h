#pragma once

static void scan_videos(void)
{
	clear_menu(M_VIDEOS);
	static const char *base[] = { "ux0:video", "ux0:data/video", "ux0:Movies", "ux0:pspemu/VIDEO" };
	char extra_dirs[12][24];
	const char *dirs[20];
	int nd = 0;
	for (unsigned i = 0; i < sizeof(base) / sizeof(base[0]); i++) dirs[nd++] = base[i];
	if (extra_storage) {
		static const char *suffix[3] = { "video", "pspemu/VIDEO", "Movies" };
		char devs[4][8];
		int n = extra_devs(devs);
		for (int i = 0; i < n; i++)
			for (int k = 0; k < 3; k++) {
				snprintf(extra_dirs[i * 3 + k], sizeof(extra_dirs[0]), "%s%s", devs[i], suffix[k]);
				dirs[nd++] = extra_dirs[i * 3 + k];
			}
	}
	dirs[nd] = NULL;
	for (int d = 0; dirs[d]; d++) {
		SceUID dfd = sceIoDopen(dirs[d]);
		if (dfd < 0) continue;
		SceIoDirent ent;
		memset(&ent, 0, sizeof(ent));
		while (sceIoDread(dfd, &ent) > 0) {
			const char *dot = strrchr(ent.d_name, '.');
			if (dot && !SCE_S_ISDIR(ent.d_stat.st_mode) &&
			    (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".avi") || !strcasecmp(dot, ".mov") || !strcasecmp(dot, ".m4v"))) {
				char title[64], sub[48];
				snprintf(title, sizeof(title), "%.*s", (int)(dot - ent.d_name) > 60 ? 60 : (int)(dot - ent.d_name), ent.d_name);
				uint64_t sz = (uint64_t)ent.d_stat.st_size;
				if (sz >= (1ULL << 30)) snprintf(sub, sizeof(sub), "%.1f GB", sz / 1073741824.0);
				else snprintf(sub, sizeof(sub), "%u MB", (unsigned)(sz >> 20));
				Item *it = add_item(M_VIDEOS, KIND_URI, title, sub, NULL, tex_video_s);
				if (it) snprintf(it->uri, sizeof(it->uri), "video:browse?category=ALL");
			}
			memset(&ent, 0, sizeof(ent));
		}
		sceIoDclose(dfd);
	}
	qsort(menus[M_VIDEOS].items, menus[M_VIDEOS].count, sizeof(Item), item_cmp);
	if (menus[M_VIDEOS].count == 0) add_item(M_VIDEOS, KIND_INFO, "No videos", "Put videos in ux0:video", NULL, tex_video_s);
	if (menus[M_VIDEOS].sel >= menus[M_VIDEOS].count) menus[M_VIDEOS].sel = 0;
}

static void scan_music(void);
static void scan_music_wrapper(void)
{
	scan_music();
	Menu *mr = &menus[CAT_MUSIC];
	int n = menus[M_TRACKS].count && menus[M_TRACKS].items[0].kind == KIND_TRACK ? menus[M_TRACKS].count : 0;
	for (int i = 0; i < mr->count; i++)
		if (mr->items[i].submenu == M_TRACKS) snprintf(mr->items[i].sub, sizeof(mr->items[i].sub), "%d song%s", n, n == 1 ? "" : "s");
}
