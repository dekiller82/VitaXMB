#pragma once

static void scan_apps(void)
{
	clear_menu(M_MEMCARD);
	Menu *mn = &menus[M_MEMCARD];

	SceUID dfd = sceIoDopen("ux0:app");
	if (dfd >= 0) {
		SceIoDirent ent;
		memset(&ent, 0, sizeof(ent));
		while (sceIoDread(dfd, &ent) > 0) {
			if (strcmp(ent.d_name, OWN_TITLEID) != 0 && strlen(ent.d_name) < 16 &&
			    SCE_S_ISDIR(ent.d_stat.st_mode)) {
				char path[128], title[64] = "", tid[16] = "";
				snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", ent.d_name);
				if (sfo_get_string(path, "TITLE", title, sizeof(title))) {
					clean_title(title);
					if (!sfo_get_string(path, "TITLE_ID", tid, sizeof(tid)))
						snprintf(tid, sizeof(tid), "%s", ent.d_name);
					Item *it = add_item(M_MEMCARD, KIND_APP, title, tid, ent.d_name, tex_game_s);
					if (it) {
						/* LiveArea's plain copy lives in ur0:appmeta; the files under ux0:app
						 * and ux0:appmeta are encrypted for retail titles. */
						snprintf(it->icon_path, sizeof(it->icon_path),
						         "ur0:appmeta/%s/icon0.png", ent.d_name);
						snprintf(it->icon_path2, sizeof(it->icon_path2),
						         "ux0:app/%s/sce_sys/icon0.png", ent.d_name);
						snprintf(it->pic_path, sizeof(it->pic_path),
						         "ur0:appmeta/%s/pic0.png", ent.d_name);
						snprintf(it->meta_dir, sizeof(it->meta_dir),
						         "ur0:appmeta/%s/livearea/contents/", ent.d_name);
					}
				}
			}
			memset(&ent, 0, sizeof(ent));
		}
		sceIoDclose(dfd);
	}
	qsort(mn->items, mn->count, sizeof(Item), item_cmp);
	n_all = mn->count;
	memcpy(all_apps, mn->items, n_all * sizeof(Item));
	rebuild_game_lists();
}

static void scan_saves(void)
{
	clear_menu(M_SAVES);
	Menu *mn = &menus[M_SAVES];

	SceUID dfd = sceIoDopen("ux0:user/00/savedata");
	if (dfd >= 0) {
		SceIoDirent ent;
		memset(&ent, 0, sizeof(ent));
		while (sceIoDread(dfd, &ent) > 0) {
			if (strlen(ent.d_name) < 16 && SCE_S_ISDIR(ent.d_stat.st_mode)) {
				char path[128], title[64] = "", sub[48] = "";
				snprintf(path, sizeof(path), "ux0:user/00/savedata/%s/sce_sys/param.sfo", ent.d_name);
				if (sfo_get_string(path, "TITLE", title, sizeof(title))) {
					clean_title(title);
					if (!sfo_get_string(path, "SUB_TITLE", sub, sizeof(sub)))
						snprintf(sub, sizeof(sub), "%s", ent.d_name);
					clean_title(sub);
					Item *it = add_item(M_SAVES, KIND_INFO, title, sub, ent.d_name, tex_savedata_s);
					if (it) {
						snprintf(it->icon_path, sizeof(it->icon_path),
						         "ux0:user/00/savedata/%s/sce_sys/icon0.png", ent.d_name);
						snprintf(it->icon_path2, sizeof(it->icon_path2),
						         "ur0:appmeta/%s/icon0.png", ent.d_name);
					}
				}
			}
			memset(&ent, 0, sizeof(ent));
		}
		sceIoDclose(dfd);
	}
	qsort(mn->items, mn->count, sizeof(Item), item_cmp);
	if (mn->count == 0)
		add_item(M_SAVES, KIND_INFO, "No saved data", "Nothing in ux0:user/00/savedata", NULL, tex_savedata_s);
	if (mn->sel >= mn->count) mn->sel = 0;
}

/* Secondary storage that shows up next to ux0: when a storage manager (StorageMgr, YAMT, ...) mounts
 * an SD2Vita or the original memory card elsewhere. A device counts if the system can report its size. */
static int extra_devs(char devs[4][8])
{
	/* A card is "there" when its root can be opened. (sceAppMgrGetDevInfo only answers for ux0:, and
	 * the names of the missing devices fail with ENODEV, so this is a reliable test.) */
	static const char *cand[4] = { "uma0:", "imc0:", "xmc0:", "grw0:" };
	int n = 0;
	for (int i = 0; i < 4; i++) {
		SceUID d = sceIoDopen(cand[i]);
		if (d >= 0) { sceIoDclose(d); snprintf(devs[n++], 8, "%s", cand[i]); }
	}
	return n;
}

/* Capacity and free space of any mounted device (VitaShell uses the same devctl). */
static int dev_space(const char *dev, uint64_t *max_size, uint64_t *free_size)
{
	if (sceAppMgrGetDevInfo(dev, max_size, free_size) >= 0 && *max_size) return 1;
	SceIoDevInfo info;
	memset(&info, 0, sizeof(info));
	if (sceIoDevctl(dev, 0x3001, NULL, 0, &info, sizeof(info)) >= 0 && info.max_size) {
		*max_size = info.max_size;
		*free_size = info.free_size;
		return 1;
	}
	return 0;
}
