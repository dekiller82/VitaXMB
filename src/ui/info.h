#pragma once

/* ---- key/value info pages (System Information, game Information) ---- */
typedef struct { char label[40]; char value[88]; } InfoRow;
static InfoRow info_rows[14];
static int info_n;

static void info_add(const char *label, const char *fmt, ...)
{
	if (info_n >= 14) return;
	snprintf(info_rows[info_n].label, sizeof(info_rows[0].label), "%s", label);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(info_rows[info_n].value, sizeof(info_rows[0].value), fmt, ap);
	va_end(ap);
	info_n++;
}

static void sysinfo_gather(void)
{
	info_n = 0;
	SceKernelSystemSwVersion v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	if (sceKernelGetSystemSwVersion(&v) >= 0) info_add(T("System Software"), "%s", v.versionString);
	else info_add(T("System Software"), "%s", T("Unknown"));

	char nick[64] = "";
	if (sceRegMgrGetKeyStr("/CONFIG/SYSTEM", "username", nick, sizeof(nick)) < 0 || !nick[0]) snprintf(nick, sizeof(nick), "-");
	info_add(T("Nickname"), "%s", nick);

	static int net_ready;
	if (!net_ready) {
		sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		SceNetInitParam np = { malloc(256 * 1024), 256 * 1024, 0 };
		if (np.memory) sceNetInit(&np);
		net_ready = 1;
	}
	SceNetEtherAddr mac;
	memset(&mac, 0, sizeof(mac));
	if (sceNetGetMacAddress(&mac, 0) == 0) {
		char ms[24];
		sceNetEtherNtostr(&mac, ms, sizeof(ms));
		for (char *c = ms; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
		info_add(T("MAC Address"), "%s", ms);
	} else {
		info_add(T("MAC Address"), "%s", T("Unavailable"));
	}

	info_add(T("Model"), "%s", sceKernelGetModelForCDialog() == 0x20000 ? "PlayStation TV" : "PlayStation Vita");

	uint64_t mx = 0, fr = 0;
	if (sceAppMgrGetDevInfo("ux0:", &mx, &fr) >= 0 && mx)
		info_add(T("Memory Stick™"), T("%.1f GB free of %.1f GB"), fr / 1073741824.0, mx / 1073741824.0);
	if (extra_storage) {
		char devs[4][8];
		int nd = extra_devs(devs);
		for (int i = 0; i < nd; i++) {
			uint64_t emx = 0, efr = 0;
			char label[40];
			snprintf(label, sizeof(label), T("Storage (%s)"), devs[i]);
			if (dev_space(devs[i], &emx, &efr))
				info_add(label, T("%.1f GB free of %.1f GB"), efr / 1073741824.0, emx / 1073741824.0);
			else
				info_add(label, "%s", T("mounted"));
		}
	}

	int pct = scePowerGetBatteryLifePercent();
	info_add(T("Battery"), "%d%%%s", pct < 0 ? 0 : pct, scePowerIsBatteryCharging() ? T(" (charging)") : "");
	info_add(T("Processor"), "%d MHz", scePowerGetArmClockFrequency());
	info_add("VitaXMB", "%s", APP_VERSION);
}

/* Details for a game or a saved-data entry. */
static void gameinfo_gather(const Item *it, int is_save)
{
	info_n = 0;
	char path[128], val[96];
	if (it->rix >= 1 && it->rix <= n_retro && it->kind == KIND_APP) {          /* a retro game: its system and its file */
		info_add(T("Title"), "%s", it->title);
		info_add(T("System"), "%s", retro_sys[retro_rec[it->rix - 1].sys].db);
		info_add(T("Location"), "%s", retro_rec[it->rix - 1].rom);
		return;
	}
	if (it->org == ORG_PSM && it->kind == KIND_APP) {                          /* a PlayStation Mobile game */
		info_add(T("Title"), "%s", it->title);
		info_add(T("Title ID"), "%s", it->id);
		info_add(T("Location"), "ux0:psm/%s", it->id);
		return;
	}
	if (it->path[0] && it->kind == KIND_APP) {                         /* a PSP or PS1 game of Adrenaline's folders */
		info_add(T("Title"), "%s", it->title);
		if (it->sub[0]) info_add(T("Disc ID"), "%s", it->sub);
		info_add(T("Location"), "%s", it->path);
		return;
	}
	if (is_save) snprintf(path, sizeof(path), "ux0:user/00/savedata/%s/sce_sys/param.sfo", it->id);
	else snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", it->id);
	info_add(T("Title"), "%s", it->title);
	info_add(T("Title ID"), "%s", it->id);
	if (!is_save && sfo_get_string(path, "APP_VER", val, sizeof(val))) info_add(T("Version"), "%s", val);
	if (!is_save && sfo_get_string(path, "CATEGORY", val, sizeof(val)))
		info_add(T("Category"), "%s", !strcmp(val, "gd") ? T("Game") : (!strcmp(val, "gda") ? T("Game (application)") : val));
	if (is_save && it->sub[0]) info_add(T("Details"), "%s", item_sub(it));
	info_add(T("Location"), is_save ? "ux0:user/00/savedata/%s" : "ux0:app/%s", it->id);
}

static void draw_info_page(float alpha)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	float y0 = 272.0f - (info_n - 1) * 20.0f;
	for (int i = 0; i < info_n; i++) {
		float y = y0 + i * 40.0f;
		ptext_right_vc(468, y, WHITE(a), 28, info_rows[i].label);
		ptext_vc_fit(490 - text_bearing(28, info_rows[i].value), y, WHITE(a), 28, info_rows[i].value, 940.0f - 490.0f);
	}
	glyph_ring(532, 522, 10, a);
	ptext_vc(552, 522, BTN_LABEL(a), 28, T("Back"));
}
