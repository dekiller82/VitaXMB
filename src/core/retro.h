#pragma once

/* ---- retro games: the ROMs RetroArch's playlists list, and the ROMs in RetroFlow's folders ----
 * Neither source is read for its own sake: the records only wait here. A ROM becomes an item (and so a row in a column or a folder) when the sort
 * wizard places it, and from then on its place is remembered like any app's (folders.txt, by the id "R" + a hash of the file's path). Until it is placed
 * it is in no list, so a library of thousands of ROMs does not bury Game.
 *
 * Sources:
 *   ux0:data/retroarch/playlists/<system>.lpl   RetroArch's playlists (JSON, or the old six lines per game): path, label and the core of each game
 *   ux0:data/RetroFlow/ROMS/<system folder>/    the folders RetroFlow Launcher uses, one per system
 * Starting a game: RetroArch (title id RETROVITA) takes the core and the file as the parameters of its URI, Emu4Vita+ (EMU4VPLUS) a core and a rom;
 * the same two URIs RetroFlow Launcher builds. The core of a playlist game is the playlist's own, else the recommended one of the system. */

#include <ctype.h>

typedef struct {
	const char *db;          /* the libretro name of the system: the playlist's file name, and the folder of RetroArch's thumbnails */
	const char *label;       /* the short name shown in the bar and in folders */
	const char *rf_dir;      /* RetroFlow Launcher's ROM folder, or NULL */
	const char *ra_core;     /* the recommended RetroArch core (file name without .self), or NULL */
	const char *emu4_core;   /* the recommended Emu4Vita+ core, or NULL */
	const char *exts;        /* extensions of its ROMs, comma separated, for the folder scan */
	const char *icon;        /* its icon (tools/make_system_icons.py): the name in sys_icon_slug[] */
} RetroSys;

static const RetroSys retro_def[] = {
	{ "Nintendo - Nintendo Entertainment System", "NES", "Nintendo - Nintendo Entertainment System", "quicknes_libretro", "fceumm", "nes,fds,unf,unif,zip", "nes" },
	{ "Nintendo - Super Nintendo Entertainment System", "SNES", "Nintendo - Super Nintendo Entertainment System", "snes9x2005_libretro", "snes9x2005_plus", "smc,sfc,swc,fig,bs,st,zip", "snes" },
	{ "Nintendo - Game Boy", "Game Boy", "Nintendo - Game Boy", "gambatte_libretro", "gambatte", "gb,zip", "gb" },
	{ "Nintendo - Game Boy Color", "Game Boy Color", "Nintendo - Game Boy Color", "gambatte_libretro", "gambatte", "gbc,zip", "gbc" },
	{ "Nintendo - Game Boy Advance", "Game Boy Advance", "Nintendo - Game Boy Advance", "gpsp_libretro", "gpsp", "gba,zip", "gba" },
	{ "Sega - Mega Drive - Genesis", "Mega Drive", "Sega - Mega Drive - Genesis", "genesis_plus_gx_libretro", "genesis_plus_gx", "md,smd,gen,bin,zip", "md" },
	{ "Sega - Master System - Mark III", "Master System", "Sega - Master System - Mark III", "smsplus_libretro", "genesis_plus_gx", "sms,zip", "sms" },
	{ "Sega - Game Gear", "Game Gear", "Sega - Game Gear", "smsplus_libretro", "genesis_plus_gx", "gg,zip", "gg" },
	{ "Sega - Mega-CD - Sega CD", "Sega CD", "Sega - Mega-CD - Sega CD", "genesis_plus_gx_libretro", "genesis_plus_gx", "cue,chd,iso,m3u", "scd" },
	{ "Sega - 32X", "Sega 32X", "Sega - 32X", "picodrive_libretro", "picodrive", "32x,zip", "s32x" },
	{ "NEC - PC Engine - TurboGrafx 16", "PC Engine", "NEC - PC Engine", "mednafen_pce_fast_libretro", "mednafen_pce_fast", "pce,zip", "pce" },
	{ "NEC - PC Engine CD - TurboGrafx-CD", "PC Engine CD", "NEC - PC Engine CD", "mednafen_pce_fast_libretro", "mednafen_pce_fast", "cue,chd,m3u", "pcecd" },
	{ "Sony - PlayStation", "PlayStation", "Sony - PlayStation - RetroArch", "pcsx_rearmed_libretro", "pcsx_rearmed", "pbp,cue,chd,img,m3u,iso", "psx" },
	{ "Atari - 2600", "Atari 2600", "Atari - 2600", "stella2014_libretro", "stella2014", "a26,bin,zip", "a2600" },
	{ "Atari - 5200", "Atari 5200", "Atari - 5200", "atari800_libretro", "atari800", "a52,bin,zip", "a5200" },
	{ "Atari - 7800", "Atari 7800", "Atari - 7800", "prosystem_libretro", "prosystem", "a78,bin,zip", "a7800" },
	{ "Atari - Lynx", "Atari Lynx", "Atari - Lynx", "handy_libretro", "handy", "lnx,zip", "lynx" },
	{ "Atari - ST", "Atari ST", "Atari - ST", "hatari_libretro", NULL, "st,msa,zip", "atarist" },
	{ "Bandai - WonderSwan", "WonderSwan", "Bandai - WonderSwan", "mednafen_wswan_libretro", "mednafen_wswan", "ws,zip", "ws" },
	{ "Bandai - WonderSwan Color", "WonderSwan Color", "Bandai - WonderSwan Color", "mednafen_wswan_libretro", "mednafen_wswan", "wsc,zip", "wsc" },
	{ "SNK - Neo Geo Pocket Color", "Neo Geo Pocket", "SNK - Neo Geo Pocket Color", "mednafen_ngp_libretro", "mednafen_ngp", "ngc,ngp,zip", "ngp" },
	{ "SNK - Neo Geo", "Neo Geo", "SNK - Neo Geo - FBA 2012", "fbalpha2012_neogeo_libretro", "fbneo", "zip", "neogeo" },
	{ "FB Alpha - Arcade Games", "Arcade", "FBA 2012", "fbalpha2012_libretro", "fbalpha2012", "zip", "arcade" },
	{ "FBNeo - Arcade Games", "Arcade (FBNeo)", NULL, "fbalpha2012_libretro", "fbneo", "zip", "arcade" },
	{ "MAME", "MAME", "MAME 2003 Plus", "mame2003_plus_libretro", "mame2003_plus", "zip", "mame" },
	{ "Commodore - Amiga", "Amiga", "Commodore - Amiga", "puae_libretro", "uae4arm", "adf,ipf,lha,zip", "amiga" },
	{ "Commodore - 64", "Commodore 64", "Commodore - 64", "vice_x64_libretro", "vice", "d64,t64,prg,crt,tap,zip", "c64" },
	{ "Microsoft - MSX", "MSX", "Microsoft - MSX", "fmsx_libretro", "fmsx", "rom,mx1,dsk,zip", "msx" },
	{ "Microsoft - MSX2", "MSX2", "Microsoft - MSX2", "fmsx_libretro", "fmsx", "rom,mx2,dsk,zip", "msx2" },
	{ "Sinclair - ZX Spectrum", "ZX Spectrum", "Sinclair - ZX Spectrum", "fuse_libretro", "fuse", "tzx,tap,z80,sna,zip", "zx" },
	{ "Coleco - ColecoVision", "ColecoVision", "Coleco - ColecoVision", "bluemsx_libretro", "bluemsx", "col,rom,zip", "coleco" },
	{ "GCE - Vectrex", "Vectrex", "GCE - Vectrex", "vecx_libretro", "vecx", "vec,bin,zip", "vectrex" },
};
#define RETRO_N_DEF ((int)(sizeof(retro_def) / sizeof(retro_def[0])))
#define RETRO_SYS_MAX 64

static struct { char db[64]; char label[40]; const RetroSys *def; } retro_sys[RETRO_SYS_MAX];     /* the systems: the known ones first, then those only a playlist names */
static int n_retro_sys;

typedef struct { char id[12]; unsigned char sys; unsigned char live; char title[64]; char rom[192]; char core[48]; } RetroRec;     /* live: it is an app of the master list now */
static RetroRec *retro_rec;
static int n_retro, retro_dropped;
static uint32_t retro_seen[16384];

static void retro_sys_init(void)
{
	if (n_retro_sys) return;
	for (int i = 0; i < RETRO_N_DEF; i++) {
		snprintf(retro_sys[i].db, sizeof(retro_sys[0].db), "%s", retro_def[i].db);
		snprintf(retro_sys[i].label, sizeof(retro_sys[0].label), "%s", retro_def[i].label);
		retro_sys[i].def = &retro_def[i];
	}
	n_retro_sys = RETRO_N_DEF;
}

/* the system of a playlist's or a ROM folder's name; -1 when it is none of the known ones */
static int retro_sys_find(const char *name)
{
	for (int i = 0; i < n_retro_sys; i++) {
		if (!strcasecmp(retro_sys[i].db, name)) return i;
		if (retro_sys[i].def && retro_sys[i].def->rf_dir && !strcasecmp(retro_sys[i].def->rf_dir, name)) return i;
	}
	return -1;
}

static int retro_sys_for_playlist(const char *name)
{
	int s = retro_sys_find(name);
	if (s >= 0 || n_retro_sys >= RETRO_SYS_MAX) return s;
	s = n_retro_sys++;                                                 /* a system the table does not know: its name is its label */
	snprintf(retro_sys[s].db, sizeof(retro_sys[0].db), "%s", name);
	const char *dash = strstr(name, " - ");
	snprintf(retro_sys[s].label, sizeof(retro_sys[0].label), "%s", dash ? dash + 3 : name);
	retro_sys[s].def = NULL;
	return s;
}

static int retro_ext_ok(const RetroSys *d, const char *file)
{
	if (!d || !d->exts) return 1;
	const char *dot = strrchr(file, '.');
	if (!dot) return 0;
	dot++;
	size_t n = strlen(dot);
	for (const char *p = d->exts; *p;) {
		const char *e = strchr(p, ',');
		size_t l = e ? (size_t)(e - p) : strlen(p);
		if (l == n && !strncasecmp(p, dot, n)) return 1;
		if (!e) break;
		p = e + 1;
	}
	return 0;
}

static void retro_add(int sys, const char *rom, const char *label, const char *core)
{
	if (!retro_rec) retro_rec = (RetroRec *)calloc(GAME_MAX, sizeof(RetroRec));
	if (!retro_rec || !rom[0]) return;
	uint32_t h = psp_hash(rom);
	if (!h) h = 1;
	unsigned k = h & 16383;
	while (retro_seen[k]) { if (retro_seen[k] == h) return; k = (k + 1) & 16383; }            /* the same file from two sources */
	if (n_retro >= GAME_MAX) { retro_dropped++; return; }
	retro_seen[k] = h;
	RetroRec *r = &retro_rec[n_retro++];
	memset(r, 0, sizeof(*r));
	snprintf(r->id, sizeof(r->id), "R%08X", h);
	r->sys = (unsigned char)sys;
	snprintf(r->rom, sizeof(r->rom), "%s", rom);
	snprintf(r->core, sizeof(r->core), "%s", core ? core : "");
	if (label && label[0]) snprintf(r->title, sizeof(r->title), "%s", label);
	else {
		const char *b = strrchr(rom, '/');
		b = b ? b + 1 : rom;
		snprintf(r->title, sizeof(r->title), "%s", b);
		char *dot = strrchr(r->title, '.');
		if (dot && strlen(dot) <= 5) *dot = 0;
	}
	psp_utf8_fix(r->title);
	clean_title(r->title);
}

/* ---- RetroArch's playlists ---- */

/* a JSON string at p (the opening quote), decoded into out; returns the position after the closing quote */
static const char *retro_js_str(const char *p, const char *end, char *out, size_t on)
{
	size_t o = 0;
	p++;
	while (p < end && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < end) {
			c = *p++;
			if (c == 'n' || c == 'r' || c == 't') c = ' ';
			else if (c == 'u') { c = '?'; for (int i = 0; i < 4 && p < end && *p != '"'; i++) p++; }
		}
		if (o + 1 < on) out[o++] = c;
	}
	out[o] = 0;
	return p < end ? p + 1 : p;
}

static void retro_playlist_json(const char *buf, int len, int sys)
{
	const char *end = buf + len, *p = strstr(buf, "\"items\"");
	if (!p) return;
	p = strchr(p, '[');
	if (!p) return;
	p++;
	while (p < end) {
		while (p < end && *p != '{' && *p != ']') p++;
		if (p >= end || *p == ']') break;
		p++;
		char path[200] = "", label[64] = "", core[48] = "";
		while (p < end && *p != '}') {
			if (*p != '"') { p++; continue; }
			char key[24], val[256];
			p = retro_js_str(p, end, key, sizeof(key));
			while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
			if (p < end && *p == '"') {
				p = retro_js_str(p, end, val, sizeof(val));
				if (!strcmp(key, "path")) snprintf(path, sizeof(path), "%s", val);
				else if (!strcmp(key, "label")) snprintf(label, sizeof(label), "%s", val);
				else if (!strcmp(key, "core_path")) snprintf(core, sizeof(core), "%s", val);
			} else {
				while (p < end && *p != ',' && *p != '}') p++;                    /* a number or null */
			}
		}
		if (p < end) p++;
		if (path[0]) retro_add(sys, path, label, core);
	}
}

/* the old format: six lines per game (path, label, core path, core name, crc, database) */
static void retro_playlist_lines(char *buf, int sys)
{
	char *f[6];
	int k = 0;
	for (char *p = buf; p && *p;) {
		char *nl = strchr(p, '\n');
		if (nl) { *nl = 0; if (nl > p && nl[-1] == '\r') nl[-1] = 0; }
		f[k++] = p;
		if (k == 6) { if (f[0][0]) retro_add(sys, f[0], f[1], f[2]); k = 0; }
		p = nl ? nl + 1 : NULL;
	}
}

static void retro_scan_playlists(void)
{
	SceUID d = sceIoDopen("ux0:data/retroarch/playlists");
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0) {
		size_t l = strlen(e.d_name);
		if (!SCE_S_ISDIR(e.d_stat.st_mode) && l > 4 && !strcasecmp(e.d_name + l - 4, ".lpl") && strncasecmp(e.d_name, "content_", 8) != 0) {
			char name[64], path[128];
			snprintf(name, sizeof(name), "%.*s", (int)(l - 4) > 63 ? 63 : (int)(l - 4), e.d_name);
			snprintf(path, sizeof(path), "ux0:data/retroarch/playlists/%s", e.d_name);
			int sys = retro_sys_for_playlist(name);
			int size = (int)e.d_stat.st_size;
			if (sys >= 0 && size > 0 && size < 6 * 1024 * 1024) {
				char *buf = (char *)malloc((size_t)size + 1);
				SceUID fd = buf ? sceIoOpen(path, SCE_O_RDONLY, 0) : -1;
				if (fd >= 0) {
					int n = sceIoRead(fd, buf, (SceSize)size);
					sceIoClose(fd);
					if (n > 0) {
						buf[n] = 0;
						const char *s = buf;
						while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t' || (unsigned char)*s == 0xEF || (unsigned char)*s == 0xBB || (unsigned char)*s == 0xBF) s++;
						if (*s == '{') retro_playlist_json(buf, n, sys);
						else retro_playlist_lines(buf, sys);
					}
				}
				free(buf);
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

/* ---- RetroFlow's ROM folders: a system's own folder, and the folders inside it ---- */

static void retro_scan_dir(int sys, const char *dir, int depth)
{
	SceUID d = sceIoDopen(dir);
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0) {
		char p[256];
		snprintf(p, sizeof(p), "%s/%s", dir, e.d_name);
		if (SCE_S_ISDIR(e.d_stat.st_mode)) {
			if (depth < 1 && e.d_name[0] != '.') retro_scan_dir(sys, p, depth + 1);
		} else if (retro_ext_ok(retro_sys[sys].def, e.d_name)) {
			retro_add(sys, p, "", "");
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

static void retro_scan_folders(void)
{
	for (int i = 0; i < RETRO_N_DEF; i++) {
		if (!retro_def[i].rf_dir) continue;
		char dir[160];
		snprintf(dir, sizeof(dir), "ux0:data/RetroFlow/ROMS/%s", retro_def[i].rf_dir);
		retro_scan_dir(i, dir, 0);
	}
}

static void retro_scan(void)
{
	retro_sys_init();
	n_retro = retro_dropped = 0;
	memset(retro_seen, 0, sizeof(retro_seen));
	retro_scan_playlists();
	retro_scan_folders();
}

/* ---- items ---- */

/* An item of the master list for a record: it shows the system under the title, and its picture comes from RetroArch's thumbnails (retro_art). */
static void retro_item(Item *it, int i)
{
	const RetroRec *r = &retro_rec[i];
	memset(it, 0, sizeof(*it));
	it->kind = KIND_APP;
	it->submenu = -1;
	it->stock = tex_game_s;
	snprintf(it->title, sizeof(it->title), "%s", r->title);
	snprintf(it->sub, sizeof(it->sub), "%s", retro_sys[r->sys].label);
	snprintf(it->id, sizeof(it->id), "%s", r->id);
	snprintf(it->icon_path, sizeof(it->icon_path), "retro:");             /* not a file: it only makes the loader ask retro_art() */
	it->meta_resolved = 2;
	it->org = ORG_RETRO0 + r->sys;
	it->rix = i + 1;
}

/* the records with a place (the user's sort) become items after the installed apps */
static void retro_add_placed(void)
{
	if (!retro_rec || !all_apps) return;
	for (int i = 0; i < n_retro && n_all < GAME_MAX; i++)
		if (!retro_rec[i].live && app_entry(retro_rec[i].id) >= 0) { retro_item(&all_apps[n_all++], i); retro_rec[i].live = 1; }
}

/* The picture of a retro game: RetroArch's box art, RetroFlow's cover, then a screenshot or the title screen. NULL when there is none. */
static uint8_t *retro_art(const Item *it, int maxw, int *w, int *h)
{
	if (it->rix < 1 || it->rix > n_retro) return NULL;
	const RetroRec *r = &retro_rec[it->rix - 1];
	char t[64];
	snprintf(t, sizeof(t), "%s", r->title);
	for (char *c = t; *c; c++) if (strchr("&*/:`\"<>?\\|", *c)) *c = '_';                 /* the characters RetroArch does not put in a file name */
	/* RetroArch's names: thumbnails/<system>/Named_Boxarts/<title>.png; RetroFlow's: COVERS/<system>/<title>.png */
	static const char *const pat[] = {
		"ux0:data/retroarch/thumbnails/%s/Named_Boxarts/%s.png", "ux0:data/RetroFlow/COVERS/%s/%s.png",
		"ux0:data/retroarch/thumbnails/%s/Named_Snaps/%s.png", "ux0:data/retroarch/thumbnails/%s/Named_Titles/%s.png" };
	for (int k = 0; k < 4; k++) {
		char p[256];
		snprintf(p, sizeof(p), pat[k], retro_sys[r->sys].db, t);
		uint8_t *pix = png_decode_scaled(p, maxw, w, h);
		if (pix) return pix;
	}
	return NULL;
}

/* ---- starting one ---- */

static void retro_uri_enc(char *out, size_t n, const char *s)
{
	size_t o = strlen(out);
	for (; *s && o + 4 < n; s++) {
		unsigned char c = (unsigned char)*s;
		if (isalnum(c) || strchr("-_.~:/", c)) out[o++] = (char)c;
		else o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
	}
	out[o] = 0;
}

/* the URI that starts a retro game; 0 when the item is not one */
static int retro_launch_uri(const Item *it, char *uri, size_t n)
{
	if (it->rix < 1 || it->rix > n_retro) return 0;
	const RetroRec *r = &retro_rec[it->rix - 1];
	const RetroSys *d = retro_sys[r->sys].def;
	int have_ra = psp_exists("ux0:app/RETROVITA"), have_e4 = psp_exists("ux0:app/EMU4VPLUS");
	int e4ok = have_e4 && d && d->emu4_core;
	int use_e4 = retro_emu ? e4ok : (!have_ra && e4ok);                      /* the chosen launcher, else the one that is installed */
	if (use_e4) {
		snprintf(uri, n, "psgm:play?titleid=EMU4VPLUS&core=%s&rom=", d->emu4_core);
		retro_uri_enc(uri, n, r->rom);
	} else {
		char core[80];
		if (r->core[0] && strcmp(r->core, "DETECT") != 0) snprintf(core, sizeof(core), "%s", r->core);
		else snprintf(core, sizeof(core), "app0:/%s.self", d && d->ra_core ? d->ra_core : "");
		snprintf(uri, n, "psgm:play?titleid=RETROVITA&param=%s&param2=%s", core, r->rom);
	}
	return 1;
}
