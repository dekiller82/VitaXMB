#pragma once

/* ---- NoLiveArea: the optional taiHEN plugin that keeps the LiveArea out of sight ----
 * The plugin (source in plugin/nolivearea, built file bundled as assets/plugin/NoLiveArea.suprx) runs inside the system shell:
 * it blacks the screen during launches so no LiveArea shows, starts VitaXMB at boot and again when a game closes.
 * Installing = copy the file to ur0:tai, add it under *main in ur0:tai/config.txt, arm its flag files in ux0:data/NoLiveArea.
 * Removing = take the line out again. taiHEN reads the config at boot, so either change needs a restart.
 * Tested on firmware 3.65 only. */

#define NLA_BUNDLED  "app0:assets/plugin/NoLiveArea.suprx"
#define NLA_PLUGIN   "ur0:tai/NoLiveArea.suprx"
#define NLA_LINE     "ur0:tai/NoLiveArea.suprx"
#define NLA_CONFIG   "ur0:tai/config.txt"
#define NLA_BACKUP   "ur0:tai/config.txt.bak_nolivearea"
#define NLA_DIR      "ux0:data/NoLiveArea"

static char note_msg[120];       /* one line shown at the top of the screen for a few seconds */
static float note_s;
/* what the install dialog tells the user; every line is something the plugin really does (see plugin/nolivearea/nolivearea.c) */
#define NLA_ASK_TEXT "Keeps the LiveArea out of sight:\n" \
	"- Turning the Vita on: VitaXMB opens by itself, no LiveArea.\n" \
	"- Starting a game: the screen stays black while it loads.\n" \
	"- Closing a game: VitaXMB opens again.\n" \
	"Pressing the PS button in a game still shows the LiveArea."
/* shown when an update replaced the plugin file: what changed in the plugin (update this text with every release that changes it) */
#define NLA_NEWS "VitaXMB was updated and replaced the NoLiveArea plugin.\n" \
	"New: it brings VitaXMB back even when a game closes\n" \
	"unnoticed, and tries again if only the LiveArea opened.\n" \
	"Smaller too: the debug hooks are gone.\n" \
	"The old version stays loaded until the Vita restarts.\n" \
	"Restart now to load the new version."
static int nla_reboot_ask;        /* set after the plugin was installed / removed: the Vita must restart to apply it */

static void note_show(const char *fmt, const char *arg)
{
	snprintf(note_msg, sizeof(note_msg), fmt, arg ? arg : "");
	note_s = 0.0f;
}

static char *nla_read(const char *path, int *len)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return NULL;
	int sz = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	char *b = sz >= 0 && sz < 256 * 1024 ? (char *)malloc((size_t)sz + 2) : NULL;
	if (b && sceIoRead(fd, b, (SceSize)sz) != sz) { free(b); b = NULL; }
	sceIoClose(fd);
	if (!b) return NULL;
	b[sz] = 0;
	*len = sz;
	return b;
}

static int nla_write(const char *path, const void *data, int len)
{
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int w = len ? sceIoWrite(fd, data, (SceSize)len) : 0;
	sceIoClose(fd);
	return w == len ? 0 : -1;
}

static int nla_bundled(void)
{
	SceIoStat st;
	return sceIoGetstat(NLA_BUNDLED, &st) >= 0 && st.st_size > 0;
}

/* The config line: first whole line equal to NLA_LINE, or NULL. */
static char *nla_find_line(char *cfg)
{
	size_t n = strlen(NLA_LINE);
	for (char *p = cfg; *p; ) {
		char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
		if ((size_t)(e - p) == n && memcmp(p, NLA_LINE, n) == 0) return p;
		while (*e == '\n' || *e == '\r') e++;
		p = e;
	}
	return NULL;
}

static int nla_enabled(void)
{
	int len;
	char *cfg = nla_read(NLA_CONFIG, &len);
	if (!cfg) return 0;
	int on = nla_find_line(cfg) != NULL;
	free(cfg);
	return on;
}

static void nla_flag(const char *name, const char *content)
{
	char p[96];
	snprintf(p, sizeof(p), NLA_DIR "/%s", name);
	if (content) nla_write(p, content, (int)strlen(content));
	else sceIoRemove(p);
}

/* New config text: the plugin line added at the end of the *main section (or a *main section at the end), line endings kept. */
static char *nla_add_line(const char *cfg, int len, int *outlen)
{
	const char *eol = memchr(cfg, '\r', (size_t)len) ? "\r\n" : "\n";
	char *out = (char *)malloc((size_t)len + 128);
	if (!out) return NULL;
	const char *ins = NULL;                       /* where the new line goes: just after the last real line of *main */
	int in_main = 0;
	for (const char *p = cfg; *p; ) {
		const char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
		const char *next = e; while (*next == '\n' || *next == '\r') next++;
		if (*p == '*') { in_main = (e - p == 5 && memcmp(p, "*main", 5) == 0); if (in_main) ins = next; }
		else if (in_main && p != e && *p != '#') ins = next;
		p = next;
	}
	int w = 0;
	if (ins) {
		w = (int)(ins - cfg);
		memcpy(out, cfg, (size_t)w);
		if (w && cfg[w - 1] != '\n') { memcpy(out + w, eol, strlen(eol)); w += (int)strlen(eol); }
		w += sprintf(out + w, "%s%s", NLA_LINE, eol);
		memcpy(out + w, ins, (size_t)(len - (int)(ins - cfg)));
		w += len - (int)(ins - cfg);
	} else {
		memcpy(out, cfg, (size_t)len);
		w = len;
		if (w && cfg[w - 1] != '\n') { memcpy(out + w, eol, strlen(eol)); w += (int)strlen(eol); }
		w += sprintf(out + w, "*main%s%s%s", eol, NLA_LINE, eol);
	}
	out[w] = 0;
	*outlen = w;
	return out;
}

static char *nla_drop_line(const char *cfg, int len, int *outlen)
{
	char *out = (char *)malloc((size_t)len + 1);
	if (!out) return NULL;
	int w = 0;
	size_t n = strlen(NLA_LINE);
	for (const char *p = cfg; *p; ) {
		const char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
		const char *next = e; while (*next == '\n' || *next == '\r') next++;
		if (!((size_t)(e - p) == n && memcmp(p, NLA_LINE, n) == 0)) { memcpy(out + w, p, (size_t)(next - p)); w += (int)(next - p); }
		p = next;
	}
	out[w] = 0;
	*outlen = w;
	return out;
}

/* Replaces the config; if the write fails or reads back wrong, the old text is put back. 0 on success. */
static int nla_store_config(const char *oldcfg, int oldlen, const char *newcfg, int newlen)
{
	if (nla_write(NLA_CONFIG, newcfg, newlen) == 0) {
		int l2; char *chk = nla_read(NLA_CONFIG, &l2);
		int ok = chk && l2 == newlen && memcmp(chk, newcfg, (size_t)newlen) == 0;
		free(chk);
		if (ok) return 0;
	}
	nla_write(NLA_CONFIG, oldcfg, oldlen);
	return -1;
}

static int nla_install(void)
{
	int plen;
	char *plug = nla_read(NLA_BUNDLED, &plen);
	if (!plug) return -1;
	int len;
	char *cfg = nla_read(NLA_CONFIG, &len);
	if (!cfg) { free(plug); return -2; }
	int rc = 0;
	if (nla_write(NLA_PLUGIN, plug, plen) != 0) rc = -3;
	else {
		int l2; char *chk = nla_read(NLA_PLUGIN, &l2);
		if (!chk || l2 != plen || memcmp(chk, plug, (size_t)plen) != 0) rc = -3;
		free(chk);
	}
	if (rc == 0 && !nla_find_line(cfg)) {
		SceIoStat st;
		if (sceIoGetstat(NLA_BACKUP, &st) < 0) nla_write(NLA_BACKUP, cfg, len);          /* keep the very first copy */
		int nl; char *nc = nla_add_line(cfg, len, &nl);
		if (!nc || nla_store_config(cfg, len, nc, nl) != 0) rc = -4;
		free(nc);
	}
	free(plug); free(cfg);
	if (rc) return rc;
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(NLA_DIR, 0777);
	nla_flag("disable", NULL);
	nla_flag("cover", "1");
	nla_flag("earlylaunch", "8000");
	return 0;
}

static int nla_remove(void)
{
	int len;
	char *cfg = nla_read(NLA_CONFIG, &len);
	if (!cfg) return -2;
	int rc = 0;
	if (nla_find_line(cfg)) {
		int nl; char *nc = nla_drop_line(cfg, len, &nl);
		if (!nc || nla_store_config(cfg, len, nc, nl) != 0) rc = -4;
		free(nc);
	}
	free(cfg);
	if (rc) return rc;
	nla_flag("disable", "1");              /* stops the copy still running in this session */
	nla_flag("cover", NULL);
	nla_flag("hide", NULL);
	nla_flag("earlylaunch", NULL);
	sceIoRemove(NLA_PLUGIN);
	return 0;
}

/* ---- plugin refresh after a VitaXMB update ----
 * The plugin is a separate file in ur0:tai. When VitaXMB is updated (VitaShell installs the new app) the old plugin file
 * stays. If the plugin is installed and the bundled one differs, it is copied over (and read back). taiHEN keeps the old
 * build loaded until the next console start, so a restart is then required. The marker lives in tmp0:, which is cleared when
 * the console starts: while it exists the restart has not happened yet. */
#define NLA_SWAPPED "tmp0:/VitaXMB_plugin_swapped"
static int nla_restart_needed;

static void nla_refresh(void)
{
	int bl, il;
	char *b = nla_read(NLA_BUNDLED, &bl);
	if (!b) return;
	char *i = nla_read(NLA_PLUGIN, &il);
	int same = i && il == bl && memcmp(i, b, (size_t)bl) == 0;
	free(i);
	if (!same && nla_write(NLA_PLUGIN, b, bl) == 0) {
		char *chk = nla_read(NLA_PLUGIN, &il);
		if (chk && il == bl && memcmp(chk, b, (size_t)bl) == 0) {
			nla_restart_needed = 1;
			nla_write(NLA_SWAPPED, "1", 1);
		}
		free(chk);
	}
	free(b);
}

static int nla_restart_pending(void)
{
	SceIoStat st;
	return nla_restart_needed || sceIoGetstat(NLA_SWAPPED, &st) >= 0;
}

/* The toggle: returns the message to show. */
static void nla_toggle(void)
{
	if (nla_enabled()) {
		int r = nla_remove();
		if (r) note_show("Could not remove the plugin (%s)", r == -2 ? "no config.txt" : "config not writable");
		else { note_show("NoLiveArea removed.", NULL); nla_reboot_ask = 1; }
	} else {
		int r = nla_install();
		if (r == -1) note_show("The plugin file is missing from this install%s", "");
		else if (r) note_show("Could not install the plugin (code %s)", r == -2 ? "no config.txt" : (r == -3 ? "copy failed" : "config not writable"));
		else { note_show("NoLiveArea installed.", NULL); nla_reboot_ask = 1; }
	}
}

/* ---- the start-up animation plays once per console start ----
 * tmp0: is cleared when the console boots, so a marker file there says "VitaXMB already ran since power on". If the app cannot
 * write there, the boot time (clock minus time since boot) is compared with the one saved last time. */
static int console_first_run(void)
{
	const char *mark = "tmp0:/VitaXMB_ran";
	SceIoStat st;
	if (sceIoGetstat(mark, &st) >= 0) return 0;
	if (nla_write(mark, "1", 1) == 0 && sceIoGetstat(mark, &st) >= 0) return 1;

	SceRtcTick now;
	sceRtcGetCurrentTick(&now);
	long long boot_us = (long long)now.tick - (long long)sceKernelGetSystemTimeWide();
	long long saved = 0;
	SceUID fd = sceIoOpen(CONFIG_DIR "/boottime.bin", SCE_O_RDONLY, 0);
	if (fd >= 0) { if (sceIoRead(fd, &saved, sizeof(saved)) != sizeof(saved)) saved = 0; sceIoClose(fd); }
	long long d = boot_us - saved;
	if (d < 0) d = -d;
	if (saved && d < 120LL * 1000000LL) return 0;
	nla_write(CONFIG_DIR "/boottime.bin", &boot_us, sizeof(boot_us));
	return 1;
}
