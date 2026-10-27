#pragma once

/* ---- Options > Uninstall: removes an installed app (the bubble and its files); the saved data stays, as on the Vita's own LiveArea ---- */

/* the apps that can go: a bubble with files in ux0:app, not VitaXMB itself, not a game file (PSP image) or a retro game or PSM title */
static int app_can_uninstall(const Item *it)
{
	if (it->kind != KIND_APP || it->org != 0 || it->rix || it->path[0]) return 0;
	if (!it->id[0] || strlen(it->id) != 9 || !strcmp(it->id, OWN_TITLEID)) return 0;
	char p[40];
	snprintf(p, sizeof(p), "ux0:app/%s", it->id);
	return psp_exists(p);
}

static void rm_tree(const char *path)
{
	for (int pass = 0; pass < 400; pass++) {                        /* a pass takes up to 64 entries; repeat until the folder is empty */
		SceUID d = sceIoDopen(path);
		if (d < 0) { sceIoRemove(path); return; }                    /* a file */
		SceIoDirent ent;
		char names[64][48];
		int n = 0;
		memset(&ent, 0, sizeof(ent));
		while (n < 64 && sceIoDread(d, &ent) > 0) {
			snprintf(names[n++], sizeof(names[0]), "%s", ent.d_name);
			memset(&ent, 0, sizeof(ent));
		}
		sceIoDclose(d);
		if (n == 0) break;
		for (int i = 0; i < n; i++) {
			char sub[256];
			snprintf(sub, sizeof(sub), "%s/%s", path, names[i]);
			rm_tree(sub);
		}
	}
	sceIoRmdir(path);
}

/* Returns 0 when the app is gone. */
static int app_uninstall(const char *id)
{
	char p[48];
	if (strlen(id) != 9 || !strcmp(id, OWN_TITLEID) || strchr(id, '/') || strchr(id, '.')) return -1;
	int r = -2;
	if (vs_init()) {                                                  /* VitaShell's patch module, then the same modules the updater loads */
		static uint32_t argp[] = { 0x180000, (uint32_t)-1, (uint32_t)-1, 1, (uint32_t)-1, (uint32_t)-1 };
		int result = -1;
		uint32_t opt[4] = { sizeof(opt), (uint32_t)(uintptr_t)&result, (uint32_t)-1, (uint32_t)-1 };
		r = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, (const SceSysmoduleOpt *)opt);
		if (r >= 0) {
			r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
			if (r >= 0) r = scePromoterUtilityInit();
			if (r >= 0) {
				r = scePromoterUtilityDeletePkg(id);                  /* the bubble and its entry in LiveArea's database */
				scePromoterUtilityExit();
			}
			sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
			uint32_t unload = 0;
			sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, (const SceSysmoduleOpt *)&unload);
		}
	}
	trace("uninstall %s: promoter -> %08x\n", id, r);
	snprintf(p, sizeof(p), "ux0:app/%s", id);
	rm_tree(p);
	snprintf(p, sizeof(p), "ux0:patch/%s", id);
	rm_tree(p);
	snprintf(p, sizeof(p), "ux0:app/%s", id);
	return psp_exists(p) ? -1 : 0;
}
