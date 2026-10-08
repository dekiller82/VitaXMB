/*
 * psptheme.h - loads PSP custom themes (.ctf / .ptf) for VitaXMB.
 *
 * A .ctf is what the CXMB plugin uses on a PSP: the theme's own .ptf part (preview image and an
 * optional 480x272 wallpaper), followed by the PSP system files the theme replaces, with a table of
 * their names at the very end. The layout is taken from CXMB (GPL-3.0, Poison et al.) and the RCO
 * layout from RCOMage (LGPL-2.1, ZiNgA BuRgA).
 *
 * What is read (pt_load):
 *   01-12.BMP                the twelve 60x34 month skies, and the colours taken from them
 *   wallpaper, preview       the optional 480x272 picture and the 300x170 picture shown in the settings list
 *   topmenu_icon.rco         the category and list icons, by label; topmenu_plugin.rco the bar position and icon scale
 *   system_plugin_fg.rco     battery, clock and button glyphs; system_plugin.rco / savedata_plugin.rco the shared pictures (resources.h)
 *   game_plugin.rco and the music, photo and video browser RCOs   list position and Options panel
 *   system_plugin_bg.rco     the background wave (bgscene.h); opening_plugin.rco the boot intro (boot.h)
 *   system_plugin.rco        also the UI sounds (themesound.h)
 *   ltn0.pgf                 the font
 *   paf.prx, vshmain.prx, common_gui.prx   the byte patches, read by ptpatch.h and applied in pt_apply_patches
 * Not read yet: see the coverage list in LOCAL_CHANGES.md (the other plugins' RCOs, most patch sites, the full
 * plugin modules a theme ships, the special skies).
 *
 * Included once from main.c (needs texture_from_rgba() and defer_free()).
 */

#include <zlib.h>

#define PT_MAX 32
#define PT_DIR_APP "ux0:data/VitaXMB/themes"
#define PT_DIR_PSP "ux0:pspemu/PSP/THEME"
#define PT_SEL_PATH "ux0:data/VitaXMB/theme.txt"

typedef struct { char path[128]; char name[48]; } PtEntry;
static PtEntry pt_list[PT_MAX];
static int pt_n;
static int pt_active = -1;                      /* index in pt_list, -1 = none */

static vita2d_texture *pt_sky, *pt_wall, *pt_preview, *pt_bat;
static float pt_clock_w;                          /* width of the clock text box (>0 with a full-width battery strip: Euphoria, Large Black put the clock at the right edge) */
static vita2d_texture *pt_focus;                  /* the bar behind the selected row of a text list (topmenu_icon.rco, label FL) */
static float pt_bat_x = 463.0f, pt_bat_y = 12.0f;
static float pt_clock_x = 208.0f;                   /* the clock text's anchor, centre-based PSP pixels (the stock value) */    /* centre of the battery picture, in PSP pixels (the stock spot) */
static vita2d_texture *pt_cat[CAT_COUNT];
static vita2d_texture *pt_blade[CAT_COUNT];      /* full-height category panels (themes like Xbox 360) */
static int pt_clock_code_set;                     /* the theme patches vshmain, so the clock x is the code's value (0x31108), not the RCO's */
static float pt_clock_code_x = 203.0f;
static float pt_sub_ratio = 1.0f, pt_fold_ratio = 1.0f;   /* how far the bar slides left for a list / a game folder, against the stock slide (vshmain states 2 and 3) */
static float pt_gap = 5.0f;                       /* extra distance either side of the open category, PSP pixels */
static float pt_ms_left = 200.0f, pt_ms_right = 200.0f;   /* how long a category change takes, milliseconds */
/* The Options menu of each screen: shift (screen pixels) and size, from that screen's plugin. */
enum { POPT_GAME, POPT_MUSIC, POPT_PHOTO, POPT_VIDEO, POPT_COUNT };
static struct { float dx, dy, scale; } pt_opt[POPT_COUNT] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
static float pt_list_dx = 0.0f;                    /* game list shift, screen pixels */
static float pt_pitch = 80.0f;                   /* spacing of the category bar in PSP pixels (the PSP's default is 80) */
static uint32_t pt_fw_magic;                      /* the CXMB firmware tag of the loaded theme */
#define pt_row_dx ((pt_blade_mode || pt_strip_mode) ? 0.0f : pt_blade_dx)     /* the same shift for the plain icon row and the lists under it */
static float pt_blade_dx;                         /* how far the theme moves the category bar from the PSP's stock spot, in Vita pixels */
static uint8_t *pt_bmps;                          /* the theme's 01-12.bmp: twelve BMPs, one per month */
static uint32_t pt_bmps_len;
static int pt_sky_month = -1;
static int pt_blade_mode;
static vita2d_texture *pt_strip[CAT_COUNT];      /* wide, low category strips (label bar along the bottom) */
static int pt_strip_mode;
static int pt_have_colors;
static unsigned char pt_sky_top[3], pt_sky_bot[3], pt_wave_top[3], pt_wave_bot[3];

static uint32_t pt_rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t pt_rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* ---- small file helpers ---- */

static float pt_rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

static uint8_t *pt_read(FILE *f, long off, long size)
{
	if (size <= 0 || size > 16 * 1024 * 1024 || fseek(f, off, SEEK_SET) != 0) return NULL;
	uint8_t *b = malloc((size_t)size);
	if (!b) return NULL;
	if (fread(b, 1, (size_t)size, f) != (size_t)size) { free(b); return NULL; }
	return b;
}

#include "ptpatch.h"

static uint8_t *pt_inflate(const uint8_t *src, uint32_t clen, uint32_t dlen)
{
	if (dlen == 0 || dlen > 16 * 1024 * 1024) return NULL;
	uint8_t *out = malloc(dlen);
	if (!out) return NULL;
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit(&zs) != Z_OK) { free(out); return NULL; }
	zs.next_in = (Bytef *)src; zs.avail_in = clen;
	zs.next_out = out; zs.avail_out = dlen;
	inflate(&zs, Z_FINISH);                  /* some theme tools leave off the end of the stream: the data is still whole */
	uLong got = zs.total_out;
	inflateEnd(&zs);
	if (got != dlen) { free(out); return NULL; }
	return out;
}

/* ---- GIM (the PSP's texture format) -> RGBA8888 ---- */

static void pt_gim_walk(const uint8_t *d, uint32_t off, uint32_t end, const uint8_t **img, const uint8_t **pal)
{
	while (off + 16 <= end) {
		uint32_t id = pt_rd16(d + off), size = pt_rd32(d + off + 4), doff = pt_rd32(d + off + 12);
		if (size < 16 || off + size > end) break;
		if (id == 2 || id == 3) pt_gim_walk(d, off + doff, end, img, pal);     /* some tools size a block smaller than its contents */
		else if (id == 4 && !*img) *img = d + off + doff;
		else if (id == 5 && !*pal) *pal = d + off + doff;
		off += size;
	}
}

static void pt_pix(uint32_t v, int fmt, uint8_t *o)
{
	switch (fmt) {
	case 0: o[0] = (v & 31) * 255 / 31; o[1] = ((v >> 5) & 63) * 255 / 63; o[2] = ((v >> 11) & 31) * 255 / 31; o[3] = 255; break;
	case 1: o[0] = (v & 31) * 255 / 31; o[1] = ((v >> 5) & 31) * 255 / 31; o[2] = ((v >> 10) & 31) * 255 / 31; o[3] = (v & 0x8000) ? 255 : 0; break;
	case 2: o[0] = (v & 15) * 17; o[1] = ((v >> 4) & 15) * 17; o[2] = ((v >> 8) & 15) * 17; o[3] = ((v >> 12) & 15) * 17; break;
	default: o[0] = v & 255; o[1] = (v >> 8) & 255; o[2] = (v >> 16) & 255; o[3] = (v >> 24) & 255; break;
	}
}

static uint8_t *pt_gim_decode(const uint8_t *d, uint32_t len, int *ow, int *oh)
{
	if (len < 64 || memcmp(d, "MIG.00.1PSP", 11) != 0) return NULL;
	const uint8_t *img = NULL, *pal = NULL;
	pt_gim_walk(d, 16, len, &img, &pal);
	if (!img) return NULL;
	int fmt = pt_rd16(img + 4), order = pt_rd16(img + 6), w = pt_rd16(img + 8), h = pt_rd16(img + 10);
	uint32_t ps = pt_rd32(img + 0x1c), pe = pt_rd32(img + 0x20);
	if (w < 1 || h < 1 || w > 2048 || h > 2048 || pe <= ps || (size_t)(img - d) + pe > len) return NULL;
	int bpp = fmt == 3 ? 32 : (fmt == 4 ? 4 : (fmt == 5 ? 8 : (fmt <= 2 ? 16 : 0)));
	if (!bpp) return NULL;
	int pitch = (w * bpp + 7) / 8, pitch_al = (pitch + 15) & ~15, hh = (h + 7) & ~7;
	const uint8_t *src = img + ps;
	size_t avail = pe - ps;
	uint8_t *lin = calloc((size_t)pitch_al * hh, 1);        /* some tools leave off the unused end of the last block row */
	if (!lin) return NULL;
	if (order == 1) {                                   /* swizzled: 16 bytes x 8 rows blocks */
		int rb = pitch_al / 16;
		size_t at = 0;
		for (int by = 0; by < hh; by += 8)
			for (int bx = 0; bx < rb; bx++)
				for (int r = 0; r < 8; r++, at += 16)
					if (at + 16 <= avail) memcpy(lin + (size_t)(by + r) * pitch_al + bx * 16, src + at, 16);
	} else {
		memcpy(lin, src, avail < (size_t)pitch_al * hh ? avail : (size_t)pitch_al * hh);
	}
	src = lin;
	uint8_t palette[256][4];
	if (fmt >= 4) {
		if (!pal) { free(lin); return NULL; }
		memset(palette, 0, sizeof(palette));
		int pf = pt_rd16(pal + 4);
		uint32_t pps = pt_rd32(pal + 0x1c), ppe = pt_rd32(pal + 0x20);
		int n = (ppe - pps) / (pf == 3 ? 4 : 2);
		if (n > 256) n = 256;
		for (int i = 0; i < n; i++) {
			const uint8_t *q = pal + pps + i * (pf == 3 ? 4 : 2);
			pt_pix(pf == 3 ? pt_rd32(q) : pt_rd16(q), pf, palette[i]);
		}
	}
	uint8_t *out = malloc((size_t)w * h * 4);
	if (!out) { free(lin); return NULL; }
	for (int y = 0; y < h; y++) {
		const uint8_t *r = src + (size_t)y * pitch_al;
		for (int x = 0; x < w; x++) {
			uint8_t *o = out + ((size_t)y * w + x) * 4;
			if (fmt == 3) { memcpy(o, r + x * 4, 4); }
			else if (fmt == 5) { memcpy(o, palette[r[x]], 4); }
			else if (fmt == 4) { memcpy(o, palette[(x & 1) ? (r[x / 2] >> 4) : (r[x / 2] & 15)], 4); }
			else pt_pix(pt_rd16(r + x * 2), fmt, o);
		}
	}
	free(lin);
	*ow = w; *oh = h;
	return out;
}

/* ---- BMP (24 / 32 bit or 8 bit palette) -> RGBA8888 ---- */

static uint8_t *pt_bmp_decode(const uint8_t *d, uint32_t len, int *ow, int *oh)
{
	if (len < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
	uint32_t off = pt_rd32(d + 10), hs = pt_rd32(d + 14);
	int w = (int)pt_rd32(d + 18), h = (int)pt_rd32(d + 22), bpp = pt_rd16(d + 28);
	int flip = 1;
	if (h < 0) { h = -h; flip = 0; }
	if (w < 1 || h < 1 || w > 2048 || h > 2048 || (bpp != 24 && bpp != 32 && bpp != 8)) return NULL;
	int stride = ((w * bpp + 31) / 32) * 4;
	if ((uint64_t)off + (uint64_t)stride * h > len) return NULL;
	uint8_t *out = malloc((size_t)w * h * 4);
	if (!out) return NULL;
	for (int y = 0; y < h; y++) {
		const uint8_t *r = d + off + (size_t)(flip ? h - 1 - y : y) * stride;
		for (int x = 0; x < w; x++) {
			uint8_t *o = out + ((size_t)y * w + x) * 4;
			if (bpp == 8) {
				const uint8_t *c = d + 14 + hs + r[x] * 4;
				o[0] = c[2]; o[1] = c[1]; o[2] = c[0];
			} else {
				const uint8_t *c = r + x * (bpp / 8);
				o[0] = c[2]; o[1] = c[1]; o[2] = c[0];
			}
			o[3] = 255;
		}
	}
	*ow = w; *oh = h;
	return out;
}

static vita2d_texture *pt_tex(uint8_t *pix, int w, int h)
{
	vita2d_texture *t = pix ? texture_from_rgba(pix, w, h) : NULL;
	free(pix);
	return t;
}

/* ---- the .ptf part: preview and wallpaper ---- */

static void pt_read_ptf(FILE *f, long ptf_end)
{
	long pos = 0x140;
	uint8_t h[0x20];
	for (int n = 0; n < 4 && pos + 0x20 <= ptf_end; n++) {
		if (fseek(f, pos, SEEK_SET) != 0 || fread(h, 1, 0x20, f) != 0x20) return;
		uint32_t idx = pt_rd32(h), clen = pt_rd32(h + 8), dlen = pt_rd32(h + 12);
		if (clen == 0 || pos + 0x20 + (long)clen > ptf_end) return;
		if (idx == 1 && !pt_preview) {
			uint8_t *z = pt_read(f, pos + 0x20, clen);
			uint8_t *g = z ? pt_inflate(z, clen, dlen) : NULL;
			int w, hh;
			uint8_t *pix = g ? pt_gim_decode(g, dlen, &w, &hh) : NULL;
			pt_preview = pt_tex(pix, w, hh);
			free(g); free(z);
		}
		pos += 0x20 + clen;
		if (idx >= 2) break;
	}
	/* optional wallpaper: a group header, then one chunk of type 4 (BMP) */
	if (pos + 0x40 <= ptf_end && fseek(f, pos, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8 && (pt_rd32(h) & 0xFFFF) == 1) {
		uint8_t c[0x20];
		if (fseek(f, pos + 0x20, SEEK_SET) != 0 || fread(c, 1, 0x20, f) != 0x20) return;
		uint32_t flags = pt_rd32(c + 4), clen = pt_rd32(c + 8), dlen = pt_rd32(c + 12);
		if ((flags & 0xFFFF) == 4 && clen && pos + 0x40 + (long)clen <= ptf_end) {
			uint8_t *z = pt_read(f, pos + 0x40, clen);
			uint8_t *b = z ? ((flags >> 16) == 2 ? pt_inflate(z, clen, dlen) : NULL) : NULL;
			if (!b && z && (flags >> 16) == 0) { b = z; z = NULL; dlen = clen; }
			int w, hh;
			uint8_t *pix = b ? pt_bmp_decode(b, dlen, &w, &hh) : NULL;
			if (pix && w == 480 && hh == 272) pt_wall = pt_tex(pix, w, hh); else free(pix);
			free(b); free(z);
		}
	}
}

/* ---- the system files bundled in a .ctf ---- */

typedef struct { char name[64]; uint32_t start, size; } PtFile;

static int pt_table(FILE *f, long fsize, PtFile *out, int max, long *ptf_end)
{
	uint8_t h[0x20];
	if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 0x20, f) != 0x20) return 0;
	if (h[0] != 0 || memcmp(h + 1, "PTF", 3) != 0) return -1;            /* not a theme file at all */
	uint32_t n = pt_rd32(h + 0x18), ptf = pt_rd32(h + 0x1c);
	pt_fw_magic = pt_rd32(h + 0x10);
	*ptf_end = fsize;
	if (n == 0 || n > 200 || ptf == 0 || ptf > (uint32_t)fsize || (long)n * 72 > fsize - (long)ptf) return 0;   /* a plain .ptf */
	*ptf_end = ptf;
	long tbl = fsize - (long)n * 72;
	uint8_t *t = pt_read(f, tbl, (long)n * 72);
	if (!t) return 0;
	int cnt = 0;
	for (uint32_t i = 0; i < n && cnt < max; i++) {
		memcpy(out[cnt].name, t + i * 72, 64);
		out[cnt].name[63] = 0;
		out[cnt].start = pt_rd32(t + i * 72 + 64);
		out[cnt].size = pt_rd32(t + i * 72 + 68);
		if ((long)out[cnt].start + out[cnt].size <= tbl) cnt++;
	}
	free(t);
	return cnt;
}

static PtFile *pt_find(PtFile *files, int n, const char *name)
{
	for (int i = 0; i < n; i++) if (!strcasecmp(files[i].name, name)) return &files[i];
	return NULL;
}

/* ---- icons from topmenu_icon.rco, found by their label ----
 * The archive keeps the labels of the PSP's own icon set (AJ, AK, ... GF) in every theme, so the same
 * label is the same icon whatever the theme draws on it. */

typedef struct { char label[16]; uint32_t fmt, comp, packed, off, unpacked; } PtImg;

static int pt_parse_images(const uint8_t *r, uint32_t size, PtImg *out, int max, uint32_t *p_data)
{
	if (size < 0xA4 || memcmp(r, "\0PRF", 4) != 0 || pt_rd32(r + 12) != 0) return 0;     /* only the uncompressed layout */
	uint32_t p_img = pt_rd32(r + 9 * 4), p_label = pt_rd32(r + 16 * 4);
	*p_data = pt_rd32(r + 32 * 4);
	if (p_img == 0xFFFFFFFFu || p_img + 0x28 >= size || p_label >= size) return 0;
	uint32_t count = pt_rd32(r + p_img + 16), p = p_img + 0x28;
	int n = 0;
	for (uint32_t i = 0; i < count && n < max && p + 0x34 <= size; i++) {
		uint32_t lab = pt_rd32(r + p + 4), comp = pt_rd16(r + p + 0x2A), next = pt_rd32(r + p + 0x14);
		PtImg *im = &out[n];
		memset(im, 0, sizeof(*im));
		if (lab != 0xFFFFFFFFu && p_label + lab < size) snprintf(im->label, sizeof(im->label), "%.15s", (const char *)r + p_label + lab);
		im->fmt = pt_rd16(r + p + 0x28);
		im->comp = comp;
		im->packed = pt_rd32(r + p + 0x2C);
		im->off = pt_rd32(r + p + 0x30);
		im->unpacked = comp ? pt_rd32(r + p + 0x34) : im->packed;
		n++;
		p += next ? next : (comp ? 0x38 : 0x34);
	}
	return n;
}

/* Decodes the image with this label into RGBA (NULL if it is missing, not a GIM or not icon-shaped). */
static uint8_t *pt_label_pixels(const uint8_t *r, uint32_t size, const PtImg *imgs, int n, uint32_t p_data,
                                const char *label, int *w, int *h, int icon_only)
{
	for (int i = 0; i < n; i++) {
		const PtImg *im = &imgs[i];
		if (strcmp(im->label, label) != 0) continue;
		if (im->fmt != 5 || im->comp > 1 || (uint64_t)p_data + im->off + im->packed > size) return NULL;
		uint8_t *g;
		if (im->comp == 1) g = pt_inflate(r + p_data + im->off, im->packed, im->unpacked);
		else { g = malloc(im->packed); if (g) memcpy(g, r + p_data + im->off, im->packed); }
		if (!g) return NULL;
		uint8_t *px = pt_gim_decode(g, im->unpacked, w, h);
		free(g);
		if (!px) return NULL;
		int big = *w > *h ? *w : *h, small = *w > *h ? *h : *w;
		if (icon_only ? (big > 128 || small < 20 || big > small * 2) : (big > 600 || small < 1)) { free(px); return NULL; }   /* panels and placeholders */
		return px;
	}
	return NULL;
}

/* Stock icon (the PSP-extracted PNG the app ships) -> label of the same icon in a theme. */
typedef struct { vita2d_texture **stock; const char *label; } PtMap;
static const PtMap pt_map[] = {
	{ &tex_theme, "BY" }, { &tex_psp, "CF" }, { &tex_exit, "CA" }, { &tex_photo_s, "BW" }, { &tex_music_s, "CB" },
	{ &tex_video_s, "BV" }, { &tex_net_s, "CD" }, { &tex_game_s, "BK" }, { &tex_savedata_s, "EJ" }, { &tex_ms_s, "BI" },
	{ &tex_launch, "BT" }, { &tex_browser, "ET" }, { &tex_remote, "EW" }, { &tex_sharing, "EI" }, { &tex_date, "BZ" },
	{ &tex_usb, "BU" }, { &tex_rss, "EV" }, { &tex_manual, "EX" }, { &tex_folder, "AX" },
};
#define PT_MAP_N ((int)(sizeof(pt_map) / sizeof(pt_map[0])))
static vita2d_texture *pt_ov[PT_MAP_N];            /* themed replacement for pt_map[i] */
static const char *pt_cat_label[CAT_COUNT] = { "AJ", "AL", "AM", "AN", "AO", "AP" };   /* Settings Photo Music Video Game Network */

static void sound_apply_theme(void);                       /* themesound.h */
static void menu_scale_reload(void);                       /* menuscale.h */
static void bgs_reload(void);                              /* bgscene.h */
static void bgs_free(void);
static vita2d_texture *res_swap(vita2d_texture *t);       /* resources.h */
static int res_owned(vita2d_texture *t);
static void res_reset(void);

/* The texture to draw for a stock icon: the theme's, if it has one. */
static vita2d_texture *pt_swap(vita2d_texture *t)
{
	for (int i = 0; i < PT_MAP_N; i++) if (pt_ov[i] && *pt_map[i].stock == t) return pt_ov[i];
	return res_swap(t);
}

/* True for textures that came from a theme (their size is not the stock 64px). */
static int pt_owned(vita2d_texture *t)
{
	for (int i = 0; i < PT_MAP_N; i++) if (pt_ov[i] == t) return 1;
	for (int c = 0; c < CAT_COUNT; c++) if (pt_cat[c] == t) return 1;
	return res_owned(t);
}

static void glow_release(vita2d_texture *src);        /* main.c */
static vita2d_texture *glow_for(vita2d_texture *src);

/* The status bar parts in system_plugin_fg.rco: the battery is one picture with four frames, full to empty. */
static void pt_read_status(FILE *f, const PtFile *pf)
{
	uint8_t *r = pt_read(f, pf->start, pf->size);
	if (!r) return;
	static PtImg imgs[40];
	uint32_t p_data = 0;
	int n = pt_parse_images(r, pf->size, imgs, 40, &p_data);
	int w, h;
	uint8_t *px = pt_label_pixels(r, pf->size, imgs, n, p_data, "tex_battery", &w, &h, 0);
	if (px && w <= 512 && h >= 8 && h <= 256 && h % 4 == 0) pt_bat = pt_tex(px, w, h); else free(px);      /* frames up to 64 px tall: a status bar part, not a big graphic */
	/* where the theme puts it: the "battery" plane in the object table (centre-based coordinates, y up) */
	uint32_t p_obj = pf->size > 0xA4 ? pt_rd32(r + 12 * 4) : 0xFFFFFFFFu, p_label = pf->size > 0xA4 ? pt_rd32(r + 16 * 4) : 0;
	if (pt_bat && p_obj != 0xFFFFFFFFu && p_label < pf->size)
		for (uint32_t q = p_obj; q + 0x60 < pf->size; q += 4)
			if (pt_rd32(r + q) == 0x00000802 && pt_rd32(r + q + 8) == 0x28) {              /* a plane ... */
				uint32_t lab = pt_rd32(r + q + 4);
				if (lab == 0xFFFFFFFFu || p_label + lab + 8 >= pf->size || strncmp((const char *)r + p_label + lab, "battery", 8) != 0) continue;
				uint32_t xb = pt_rd32(r + q + 0x28), yb = pt_rd32(r + q + 0x2C);
				float fx, fy;
				memcpy(&fx, &xb, 4); memcpy(&fy, &yb, 4);
				if (fx > -400.0f && fx < 400.0f && fy > -300.0f && fy < 300.0f) { pt_bat_x = 240.0f + fx; pt_bat_y = 136.0f - fy; }
				break;
			}
	if (p_obj != 0xFFFFFFFFu && p_label < pf->size)
		for (uint32_t q = p_obj; q + 0x60 < pf->size; q += 4)
			if (pt_rd32(r + q) == 0x0000080D && pt_rd32(r + q + 8) == 0x28) {              /* ... and the clock text */
				uint32_t lab = pt_rd32(r + q + 4);
				if (lab == 0xFFFFFFFFu || p_label + lab + 6 >= pf->size || strncmp((const char *)r + p_label + lab, "clock", 6) != 0) continue;
				uint32_t xb = pt_rd32(r + q + 0x28);
				float fx;
				memcpy(&fx, &xb, 4);
				if (fx > -400.0f && fx < 400.0f) pt_clock_x = fx;
				uint32_t wb = pt_rd32(r + q + 0x28 + 28);
				float fw;
				memcpy(&fw, &wb, 4);
				pt_clock_w = fw > 0.0f && fw < 400.0f ? fw : 0.0f;
				break;
			}
	free(r);
}

/* topmenu_plugin.rco: the x position the theme gives the category bar (XMenu, type 4) -> shift from the stock -120. */
/* The patch records of the three modules are in pp_* (ptpatch.h). What VitaXMB reads from them, with the stock
 * immediate of each site (6.61 disassembly; paf.prx unless said):
 *   0x1066f4  spacing between categories, PSP pixels (XMenu constructor, 80.0)
 *   0x106708  gap beside the open category, both sides (5.0)
 *   0x106908  duration of a move to the previous category, ms (200.0)     0x10693c  to the next one
 *   0x10a23c  duration of an XList scroll up, ms (200.0)                  0x10a27c  down
 *   0x10a580  the XList style setter: each case loads its row pitch and gap with `lui` (table below)
 * An item with visible rank r, when rank f is open, sits at (r - f) * spacing, plus the gap on its right
 * and minus it on its left. */

/* XList styles (paf.prx 0x10a580): the `lui` that loads a style's pitch (+0x340) and gap (+0x34c, +0x350), the
 * stock immediates, and the styles that run that code. gap kind: 0 none (both gaps 0), 1 one value for both, 2 the
 * pitch value is the gap above too (styles 0 and 2), 3 gap above only (style 1). */
typedef struct { uint32_t pitch_at; uint16_t pitch_imm; uint32_t gap_at; uint16_t gap_imm, gap_lo; int kind; uint32_t styles; } PtXStyleSite;
#define XSB(n) (1u << (n))
static const PtXStyleSite pt_xsites[] = {
	{ 0x10a5cc, 0x4282, 0,        0,      0,      2, XSB(0) | XSB(2) },
	{ 0x10a6f4, 0x4282, 0x10a700, 0x4270, 0,      3, XSB(1) },
	{ 0x10a720, 0x4234, 0x10a72c, 0x4120, 0,      1, XSB(4) },
	{ 0x10a748, 0x4258, 0x10a754, 0x4150, 0,      1, XSB(5) | XSB(10) | XSB(17) | XSB(18) | XSB(19) | XSB(20) },
	{ 0x10a764, 0x422c, 0x10a770, 0x4188, 0,      1, XSB(6) },
	{ 0x10a780, 0x4220, 0x10a78c, 0x4234, 0,      1, XSB(7) },
	{ 0x10a79c, 0x4258, 0x10a7a8, 0x4151, 0x999a, 1, XSB(8) },
	{ 0x10a7c8, 0x4260, 0,        0,      0,      0, XSB(12) },
	{ 0x10a8d4, 0x42b2, 0,        0,      0,      0, XSB(15) },
	{ 0x10a9dc, 0x4234, 0x10a9e8, 0x41f4, 0,      1, XSB(9) | XSB(11) | XSB(16) | XSB(21) },
	{ 0x10a9fc, 0x4270, 0,        0,      0,      0, XSB(22) },
	{ 0x10aafc, 0x4254, 0x10ab08, 0x4150, 0,      1, XSB(23) },
};

static void pt_apply_patches(void)
{
	if (pt_fw_magic != 0xDEAD0660u && pt_fw_magic != 0xDEAD0661u) return;    /* the offsets are those of 6.60 / 6.61 */

	float v = pp_hi(PM_PAF, 0x1066f4, 0x42a0);
	if (v >= 0.0f && v <= 2000.0f) pt_pitch = v;
	v = pp_hi(PM_PAF, 0x106708, 0x40a0);
	if (v >= -2000.0f && v <= 2000.0f) pt_gap = v;
	v = pp_hi(PM_PAF, 0x106908, 0x4348);
	if (v >= 0.0f && v <= 5000.0f) pt_ms_left = v;
	v = pp_hi(PM_PAF, 0x10693c, 0x4348);
	if (v >= 0.0f && v <= 5000.0f) pt_ms_right = v;

	/* vshmain 0x31038 places the clock at runtime (x 203 with the battery shown, 235 without; y 123), over what the RCO says;
	 * the mute and hold icons are placed to its left (0x30f58, 0x30e84). */
	pt_clock_code_x = pp_hi(PM_VSH, 0x31108, 0x434b);
	pt_clock_code_set = 1;
	/* vshmain 0x1d7a4 sets the bar's x for each menu state: 0 the home view (-130), 2 a list open (-190), 3 a list inside it (-240).
	 * The slide against the home x is what VitaXMB animates, so a theme's value is taken as a ratio to the stock slide. */
	float x0 = pp_hi(PM_VSH, 0x1d824, 0xc302), x2 = pp_hi(PM_VSH, 0x1d944, 0xc33e), x3 = pp_hi(PM_VSH, 0x1da64, 0xc370);
	v = (x2 - x0) / -60.0f;
	if (v >= 0.0f && v <= 8.0f) pt_sub_ratio = v;
	v = (x3 - x0) / -110.0f;
	if (v >= 0.0f && v <= 8.0f) pt_fold_ratio = v;

	v = pp_hi(PM_PAF, 0x10a23c, 0x4348);
	if (v >= 0.0f && v <= 5000.0f) xl_ms_up = v;
	v = pp_hi(PM_PAF, 0x10a27c, 0x4348);
	if (v >= 0.0f && v <= 5000.0f) xl_ms_down = v;

	for (unsigned i = 0; i < sizeof(pt_xsites) / sizeof(pt_xsites[0]); i++) {
		const PtXStyleSite *s = &pt_xsites[i];
		float pitch = pp_hi(PM_PAF, s->pitch_at, s->pitch_imm), gap = 0.0f;
		if (s->kind == 1 || s->kind == 3)
			gap = s->gap_lo ? pp_hilo(PM_PAF, s->gap_at, s->gap_imm, s->gap_lo) : pp_hi(PM_PAF, s->gap_at, s->gap_imm);
		else if (s->kind == 2) gap = pitch;
		if (pitch < 0.0f || pitch > 5000.0f || gap < -5000.0f || gap > 5000.0f) continue;
		for (int st = 0; st < XS_COUNT; st++) {
			if (!(s->styles & XSB(st))) continue;
			xstyles[st].pitch = pitch;
			xstyles[st].gap_above = gap;
			xstyles[st].gap_below = s->kind >= 2 ? 0.0f : gap;
		}
	}
	pp_report();
}

static void pt_read_layout(FILE *f, const PtFile *pf)
{
	uint8_t *r = pt_read(f, pf->start, pf->size);
	if (!r) return;
	if (pf->size > 0xA4 && memcmp(r, "\0PRF", 4) == 0 && pt_rd32(r + 12) == 0) {
		uint32_t p_obj = pt_rd32(r + 12 * 4);
		if (p_obj != 0xFFFFFFFFu)
			for (uint32_t p = p_obj; p + 0x30 < pf->size; p += 4)
				if (pt_rd32(r + p) == 0x00000804 && pt_rd32(r + p + 8) == 0x28) {       /* object table, type 4 = XMenu */
					uint32_t w = pt_rd32(r + p + 0x28);
					float x;
					memcpy(&x, &w, 4);
					if (x > -400.0f && x < 400.0f) pt_blade_dx = (x - (-130.0f)) * 2.0f;      /* -130 is the PSP's own value (read from its topmenu_plugin.rco) */
					break;
				}
	}
	free(r);
}

static float pt_rdf(const uint8_t *p);

/* Reads an Options mlist's position and scale (attributes start at a): posX, posY, and scaleWidth/Height, against the stock posX. */
static void pt_opt_fill(int ctx, const uint8_t *a, float stock_x)
{
	float x = pt_rdf(a), y = pt_rdf(a + 4), sh = pt_rdf(a + 44);
	if (x > -400.0f && x < 400.0f) pt_opt[ctx].dx = (x - stock_x) * 2.0f;
	if (y > -300.0f && y < 300.0f) pt_opt[ctx].dy = -(y - 4.0f) * 2.0f;          /* stock posY is 4, y runs up */
	if (sh > 0.3f && sh < 2.0f) pt_opt[ctx].scale = sh;
}

/* The Options mlist called name in another plugin's RCO (music, photo, video: their own labels). */
static void pt_read_opt(FILE *f, const PtFile *pf, int ctx, const char *name, float stock_x)
{
	uint8_t *r = pt_read(f, pf->start, pf->size);
	if (!r) return;
	if (pf->size > 0xA4 && memcmp(r, "\0PRF", 4) == 0) {
		uint32_t p_obj = pt_rd32(r + 12 * 4), p_label = pt_rd32(r + 16 * 4);
		if (p_obj != 0xFFFFFFFFu && p_label < pf->size)
			for (uint32_t q = p_obj; q + 0x30 < pf->size; q += 4)
				if (pt_rd32(r + q) == 0x00000809 && pt_rd32(r + q + 8) == 0x28) {
					uint32_t lab = pt_rd32(r + q + 4);
					if (lab == 0xFFFFFFFFu || p_label + lab + 40 >= pf->size) continue;
					if (strcmp((const char *)r + p_label + lab, name) != 0) continue;
					pt_opt_fill(ctx, r + q + 0x28, stock_x);
					break;
				}
	}
	free(r);
}

/* posX of the game list ("xlist_ms_game", a type 6 object) in game_plugin.rco, as a shift from the stock 0. */
static void pt_read_list(FILE *f, const PtFile *pf)
{
	uint8_t *r = pt_read(f, pf->start, pf->size);
	if (!r) return;
	if (pf->size > 0xA4 && memcmp(r, "\0PRF", 4) == 0) {
		uint32_t p_obj = pt_rd32(r + 12 * 4), p_label = pt_rd32(r + 16 * 4);
		if (p_obj != 0xFFFFFFFFu && p_label < pf->size)
			for (uint32_t q = p_obj; q + 0x30 < pf->size; q += 4)
				if ((pt_rd32(r + q) == 0x00000806 || pt_rd32(r + q) == 0x00000809) && pt_rd32(r + q + 8) == 0x28) {
					uint32_t lab = pt_rd32(r + q + 4);
					if (lab == 0xFFFFFFFFu || p_label + lab + 32 >= pf->size) continue;
					const char *nm = (const char *)r + p_label + lab;
					uint32_t w = pt_rd32(r + q + 0x28);
					float x;
					memcpy(&x, &w, 4);
					if (x < -400.0f || x > 400.0f) continue;
					if (strcmp(nm, "xlist_ms_game") == 0) pt_list_dx = x * 2.0f;
					else if (strcmp(nm, "mlist_ms_all_view_option") == 0) pt_opt_fill(POPT_GAME, r + q + 0x28, -235.0f);
				}
	}
	free(r);
}

static void pt_read_icons(FILE *f, const PtFile *pf)
{
	uint8_t *r = pt_read(f, pf->start, pf->size);
	if (!r) return;
	static PtImg imgs[200];
	uint32_t p_data = 0;
	int n = pt_parse_images(r, pf->size, imgs, 200, &p_data);
	for (int c = 0; c < CAT_COUNT; c++) {
		int w, h;
		uint8_t *px = pt_label_pixels(r, pf->size, imgs, n, p_data, pt_cat_label[c], &w, &h, 1);
		if (px) pt_cat[c] = pt_tex(px, w, h);
	}
	for (int c = 0; c < CAT_COUNT; c++) {                   /* a tall panel where the icon should be: this theme draws the whole bar as artwork */
		if (pt_cat[c]) continue;
		int w, h;
		uint8_t *px = pt_label_pixels(r, pf->size, imgs, n, p_data, pt_cat_label[c], &w, &h, 0);
		if (px && w >= 48 && h >= 200) { pt_blade[c] = pt_tex(px, w, h); pt_blade_mode |= pt_blade[c] != NULL; }
		else if (px && w >= 300 && h <= 120 && w >= 3 * h) { pt_strip[c] = pt_tex(px, w, h); pt_strip_mode |= pt_strip[c] != NULL; }
		else free(px);
	}
	{
		int w, h;
		uint8_t *px = pt_label_pixels(r, pf->size, imgs, n, p_data, "FL", &w, &h, 0);
		if (px && w > 2 && h > 2) pt_focus = pt_tex(px, w, h);
		else free(px);
	}
	for (int i = 0; i < PT_MAP_N; i++) {
		int w, h;
		uint8_t *px = pt_label_pixels(r, pf->size, imgs, n, p_data, pt_map[i].label, &w, &h, 1);
		if (px) { pt_ov[i] = pt_tex(px, w, h); if (pt_ov[i]) glow_for(pt_ov[i]); }
	}
	free(r);
}

/* ---- the theme's font (PGF, the PSP's bitmap font format; layout from PPSSPP's PGF reader) ---- */

typedef struct {
	uint8_t *file;
	int first, cml, cpl, cmb, cpb;
	int ndim, nx, ny, nadv;
	int32_t *dim[2], *xadj[2], *yadj[2], *adv[2];
	uint32_t *charmap, *charptr;
	const uint8_t *fd;
	size_t fdsize;
	int cap;                                   /* height of a capital H in the font's own pixels */
} PtFont;
static PtFont *pt_font;

typedef struct { int w, h, left, top, flags; int32_t adv; size_t ptr; } PtGlyph;

static uint32_t pt_bits(const uint8_t *buf, size_t nbytes, int n, size_t pos)
{
	uint32_t v = 0;
	if (pos + (size_t)n > nbytes * 8) return 0;
	for (int i = 0; i < n; i++) {
		size_t b = pos + i;
		v |= (uint32_t)((buf[b >> 3] >> (b & 7)) & 1) << i;
	}
	return v;
}

static void pt_font_destroy(PtFont *f)
{
	if (!f) return;
	for (int k = 0; k < 2; k++) { free(f->dim[k]); free(f->xadj[k]); free(f->yadj[k]); free(f->adv[k]); }
	free(f->charmap); free(f->charptr); free(f->file); free(f);
}

static void pt_font_free(void) { pt_font_destroy(pt_font); pt_font = NULL; }

static int32_t *pt_tab(const uint8_t *p, int n, int k)
{
	int32_t *t = malloc(sizeof(int32_t) * (n ? n : 1));
	for (int i = 0; i < n; i++) t[i] = (int32_t)pt_rd32(p + i * 8 + k * 4);
	return t;
}

static int pt_glyph(const PtFont *f, uint32_t cp, PtGlyph *g);

static PtFont *pt_font_parse(uint8_t *data, size_t size)
{
	if (size < 400 || memcmp(data + 4, "PGF0", 4) != 0) return NULL;
	int rev = (int)pt_rd32(data + 8);
	int cml = (int)pt_rd32(data + 16), cpl = (int)pt_rd32(data + 20), cmb = (int)pt_rd32(data + 24), cpb = (int)pt_rd32(data + 28);
	int shl = (int)pt_rd32(data + 364), shb = (int)pt_rd32(data + 368);
	if (cml < 0 || cml > 0x100000 || cpl < 0 || cpl > 0x100000 || cmb < 1 || cmb > 32 || cpb < 1 || cpb > 32 || shl < 0 || shl > 0x10000 || shb < 0 || shb > 32) return NULL;
	size_t pos = 392;
	int c1 = 0, c2 = 0;
	if (rev == 3) { c1 = (int)(pt_rd32(data + 392 + 4) & 0xFFFF); c2 = (int)(pt_rd32(data + 392 + 12) & 0xFFFF); pos += 20; }
	int nd = data[258], nx = data[259], ny = data[260], na = data[261];
	size_t tabs = (size_t)(nd + nx + ny + na) * 8;
	size_t sh_sz = (((size_t)shl * shb + 31) & ~(size_t)31) / 8, cm_sz = (((size_t)cml * cmb + 31) & ~(size_t)31) / 8, cp_sz = (((size_t)cpl * cpb + 31) & ~(size_t)31) / 8;
	size_t need = pos + tabs + sh_sz + (size_t)(c1 + c2) * 4 + cm_sz + cp_sz;
	if (need >= size) return NULL;
	PtFont *f = calloc(1, sizeof(*f));
	if (!f) return NULL;
	f->file = data;
	f->first = pt_rd16(data + 182);
	f->cml = cml; f->cpl = cpl; f->cmb = cmb; f->cpb = cpb;
	f->ndim = nd; f->nx = nx; f->ny = ny; f->nadv = na;
	const uint8_t *p = data + pos;
	for (int k = 0; k < 2; k++) {
		f->dim[k] = pt_tab(p, nd, k);
		f->xadj[k] = pt_tab(p + nd * 8, nx, k);
		f->yadj[k] = pt_tab(p + (nd + nx) * 8, ny, k);
		f->adv[k] = pt_tab(p + (nd + nx + ny) * 8, na, k);
	}
	p += tabs + sh_sz + (size_t)(c1 + c2) * 4;
	f->charmap = malloc(sizeof(uint32_t) * (cml ? cml : 1));
	f->charptr = malloc(sizeof(uint32_t) * (cpl ? cpl : 1));
	if (!f->charmap || !f->charptr) { f->file = NULL; pt_font_destroy(f); return NULL; }
	for (int i = 0; i < cml; i++) f->charmap[i] = pt_bits(p, cm_sz, cmb, (size_t)i * cmb);
	p += cm_sz;
	for (int i = 0; i < cpl; i++) f->charptr[i] = pt_bits(p, cp_sz, cpb, (size_t)i * cpb);
	p += cp_sz;
	f->fd = p;
	f->fdsize = size - (size_t)(p - data);
	PtGlyph g;
	f->cap = pt_glyph(f, 'H', &g) ? g.top : 14;
	if (f->cap < 6 || f->cap > 40) f->cap = 14;
	return f;
}

static int pt_glyph(const PtFont *f, uint32_t cp, PtGlyph *g)
{
	if ((int)cp < f->first) return 0;
	uint32_t c = cp - f->first;
	if (c < (uint32_t)f->cml) c = f->charmap[c];
	if (c >= (uint32_t)f->cpl) return 0;
	size_t bp = (size_t)f->charptr[c] * 32;
	if (bp + 1024 > f->fdsize * 8) return 0;
	const uint8_t *fd = f->fd;
	size_t n = f->fdsize;
#define TAKE(bits) (bp += (bits), pt_bits(fd, n, (bits), bp - (bits)))
	bp += 14;
	g->w = (int)TAKE(7); g->h = (int)TAKE(7);
	int l = (int)TAKE(7), t = (int)TAKE(7);
	g->left = l >= 64 ? l - 128 : l;
	g->top = t >= 64 ? t - 128 : t;
	g->flags = (int)TAKE(6);
	TAKE(2); TAKE(2); TAKE(3); TAKE(9);
	int fl = g->flags;
	/* dimension, x and y bearing: an 8 bit index into a table, or two raw 32 bit values */
	if ((fl & 0x04) == 0x04) TAKE(8); else { TAKE(32); TAKE(32); }
	if ((fl & 0x08) == 0x08) TAKE(8); else { TAKE(32); TAKE(32); }
	if ((fl & 0x10) == 0x10) TAKE(8); else { TAKE(32); TAKE(32); }
	if ((fl & 0x20) == 0x20) {
		int i = (int)TAKE(8);
		g->adv = i < f->nadv ? f->adv[0][i] : 0;
	} else {
		g->adv = (int32_t)TAKE(32);
		TAKE(32);
	}
#undef TAKE
	g->ptr = bp / 8;
	return 1;
}

/* One glyph's coverage, w*h bytes, row-major. Returns NULL for an empty or unreadable glyph. */
static uint8_t *pt_glyph_bitmap(const PtFont *f, const PtGlyph *g)
{
	if (g->w <= 0 || g->h <= 0) return NULL;
	int total = g->w * g->h;
	uint8_t *raw = malloc(total), *out = malloc(total);
	if (!raw || !out) { free(raw); free(out); return NULL; }
	size_t bp = g->ptr * 8;
	int i = 0;
	while (i < total && bp + 8 < f->fdsize * 8) {
		int nib = (int)pt_bits(f->fd, f->fdsize, 4, bp); bp += 4;
		int count, v = 0;
		if (nib < 8) { v = (int)pt_bits(f->fd, f->fdsize, 4, bp); bp += 4; count = nib + 1; }
		else count = 16 - nib;
		for (int k = 0; k < count && i < total; k++) {
			if (nib >= 8) { v = (int)pt_bits(f->fd, f->fdsize, 4, bp); bp += 4; }
			raw[i++] = (uint8_t)(v * 17);
		}
	}
	while (i < total) raw[i++] = 0;
	if ((g->flags & 3) == 1) memcpy(out, raw, total);                 /* rows */
	else for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) out[y * g->w + x] = raw[x * g->h + y];   /* columns */
	free(raw);
	return out;
}

/* The UI's text sizes were drawn for the PSP's stock font, whose capital H is 14 px at size 20. */
static float pt_font_scale(unsigned int size) { return (float)size / 20.0f; }

/* Advance of a character in Vita pixels at this size, or -1 when the font has no glyph for it. */
static float pt_font_adv(uint32_t cp, unsigned int size)
{
	PtGlyph g;
	if (!pt_font || !pt_glyph(pt_font, cp, &g)) return -1.0f;
	return (g.adv / 64.0f) * pt_font_scale(size);
}

static void text_cache_flush(void);                  /* main.c */

/* ---- loading / unloading ---- */

static void pt_unload(void)
{
	if (pt_sky) { defer_free(pt_sky); pt_sky = NULL; }
	if (pt_wall) { defer_free(pt_wall); pt_wall = NULL; }
	if (pt_bat) { defer_free(pt_bat); pt_bat = NULL; }
	if (pt_focus) { defer_free(pt_focus); pt_focus = NULL; }
	pt_bat_x = 463.0f; pt_bat_y = 12.0f; pt_clock_x = 208.0f; pt_clock_w = 0.0f;
	if (pt_font) { pt_font_free(); text_cache_flush(); }
	if (pt_preview) { defer_free(pt_preview); pt_preview = NULL; }
	for (int c = 0; c < CAT_COUNT; c++) if (pt_cat[c]) { defer_free(pt_cat[c]); pt_cat[c] = NULL; }
	for (int c = 0; c < CAT_COUNT; c++) if (pt_blade[c]) { defer_free(pt_blade[c]); pt_blade[c] = NULL; }
	for (int c = 0; c < CAT_COUNT; c++) if (pt_strip[c]) { defer_free(pt_strip[c]); pt_strip[c] = NULL; }
	pt_strip_mode = 0;
	pt_blade_mode = 0; pt_blade_dx = 0.0f; pt_pitch = 80.0f; pt_list_dx = 0.0f; for (int i = 0; i < POPT_COUNT; i++) { pt_opt[i].dx = 0.0f; pt_opt[i].dy = 0.0f; pt_opt[i].scale = 1.0f; } pt_gap = 5.0f; pt_ms_left = pt_ms_right = 200.0f; pt_sub_ratio = pt_fold_ratio = 1.0f;
	pp_clear(); xs_reset(); xl_ms_up = xl_ms_down = 200.0f; pt_clock_code_set = 0;
	for (int i = 0; i < PT_MAP_N; i++) if (pt_ov[i]) { glow_release(pt_ov[i]); defer_free(pt_ov[i]); pt_ov[i] = NULL; }
	pt_have_colors = 0;
	wave_set_model(NULL, 0);
	wave_off = 0;
	bgs_free();
	res_reset();
	free(pt_bmps); pt_bmps = NULL; pt_bmps_len = 0; pt_sky_month = -1;
	pt_active = -1;
	sound_apply_theme();                                      /* back to the app's own sounds */
	menu_scale_reload();
}

static void pt_make_colors(const uint8_t *rgba, int w, int h)
{
	/* average of the top and of the bottom rows of the gradient */
	long t[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
	int rows = h >= 6 ? 3 : 1;
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < w; x++)
			for (int k = 0; k < 3; k++) {
				t[k] += rgba[((size_t)y * w + x) * 4 + k];
				b[k] += rgba[((size_t)(h - 1 - y) * w + x) * 4 + k];
			}
	for (int k = 0; k < 3; k++) {
		int top = (int)(t[k] / (rows * w)), bot = (int)(b[k] / (rows * w));
		pt_sky_top[k] = (unsigned char)top;
		pt_sky_bot[k] = (unsigned char)bot;
		pt_wave_top[k] = (unsigned char)(bot + (255 - bot) * 0.10f);
		pt_wave_bot[k] = (unsigned char)(bot + (255 - bot) * 0.28f);
	}
	pt_have_colors = 1;
}

/* The preview picture of theme idx, without loading the theme (NULL if it has none). */
static vita2d_texture *pt_peek(int idx)
{
	if (idx < 0 || idx >= pt_n) return NULL;
	FILE *f = fopen(pt_list[idx].path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	long ptf_end = fsize;
	static PtFile files[80];
	int nf = pt_table(f, fsize, files, 80, &ptf_end);
	vita2d_texture *keep_p = pt_preview, *keep_w = pt_wall, *r = NULL;
	pt_preview = NULL; pt_wall = NULL;
	if (nf >= 0) pt_read_ptf(f, ptf_end);
	r = pt_preview;
	if (pt_wall) defer_free(pt_wall);
	pt_preview = keep_p; pt_wall = keep_w;
	fclose(f);
	return r;
}

/* 01-12.bmp holds twelve BMPs one after the other, January to December (the PSP picks one by the month, or by the Color setting). */
static int pt_bmp_count(void)
{
	int n = 0;
	for (uint32_t p = 0; p + 54 <= pt_bmps_len && pt_bmps[p] == 'B' && pt_bmps[p + 1] == 'M'; n++) {
		uint32_t sz = pt_rd32(pt_bmps + p + 2);
		if (sz < 54) break;
		p += sz;
	}
	return n;
}

/* Shows the sky picture of a month (0..11): decodes that BMP and takes the colours from it. */
static void pt_set_month(int month)
{
	if (!pt_bmps || month == pt_sky_month) return;
	int n = pt_bmp_count();
	if (n < 1) return;
	pt_sky_month = month;
	int idx = month < n ? month : n - 1;
	uint32_t p = 0;
	for (int i = 0; i < idx; i++) p += pt_rd32(pt_bmps + p + 2);
	uint32_t sz = pt_rd32(pt_bmps + p + 2);
	if (p + sz > pt_bmps_len) sz = pt_bmps_len - p;
	int w = 0, h = 0;
	uint8_t *pix = pt_bmp_decode(pt_bmps + p, sz, &w, &h);
	if (!pix) return;
	pt_make_colors(pix, w, h);
	vita2d_texture *t = texture_from_rgba(pix, w, h);
	free(pix);
	if (t) {
		if (pt_sky) defer_free(pt_sky);
		pt_sky = t;
	}
}

static int pt_load(int idx)
{
	pt_unload();
	if (idx < 0 || idx >= pt_n) return 0;
	FILE *f = fopen(pt_list[idx].path, "rb");
	if (!f) { trace("theme: cannot open %s\n", pt_list[idx].path); return 0; }
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	static PtFile files[80];
	long ptf_end = fsize;
	int nf = pt_table(f, fsize, files, 80, &ptf_end);
	trace("theme: %s size %ld files %d\n", pt_list[idx].path, fsize, nf);
	if (nf < 0) { fclose(f); return 0; }
	pt_read_ptf(f, ptf_end);
	if (nf > 0) {
		/* a PSP-3000 and later (what Adrenaline runs as) loads 01-12_03g.bmp; its October sky fits a capture of the real XMB, 01-12.bmp's does not */
		PtFile *bg = pt_find(files, nf, "/vsh/resource/01-12_03g.bmp");
		if (!bg) bg = pt_find(files, nf, "/vsh/resource/01-12.bmp");
		if (bg) {
			pt_bmps = pt_read(f, bg->start, bg->size);
			pt_bmps_len = pt_bmps ? bg->size : 0;
			pt_sky_month = -1;
			SceDateTime now;
			sceRtcGetCurrentClockLocalTime(&now);
			pt_set_month(theme ? theme - 1 : (now.month >= 1 && now.month <= 12 ? now.month - 1 : 0));
		}
		static const struct { const char *name; int mod; } pmods[] = {
			{ "/vsh/module/paf.prx", PM_PAF }, { "/vsh/module/vshmain.prx", PM_VSH }, { "/vsh/module/common_gui.prx", PM_CGUI } };
		for (unsigned i = 0; i < sizeof(pmods) / sizeof(pmods[0]); i++) {
			PtFile *pp = pt_find(files, nf, pmods[i].name);
			if (pp) pp_load(f, pp->start, pp->size, pmods[i].mod);       /* size = number of records here */
		}
		pt_apply_patches();
		PtFile *tl = pt_find(files, nf, "/vsh/resource/topmenu_plugin.rco");
		if (tl) pt_read_layout(f, tl);
		PtFile *gl = pt_find(files, nf, "/vsh/resource/game_plugin.rco");
		if (gl) pt_read_list(f, gl);
		PtFile *mb = pt_find(files, nf, "/vsh/resource/music_browser_plugin.rco");
		if (mb) pt_read_opt(f, mb, POPT_MUSIC, "AW", -235.0f);
		PtFile *pb = pt_find(files, nf, "/vsh/resource/photo_browser_plugin.rco");
		if (pb) pt_read_opt(f, pb, POPT_PHOTO, "AJ", -235.0f);
		PtFile *vb = pt_find(files, nf, "/vsh/resource/msvideo_main_plugin.rco");
		if (vb) pt_read_opt(f, vb, POPT_VIDEO, "msvideo_option_menu_list", -182.0f);
		PtFile *ic = pt_find(files, nf, "/vsh/resource/topmenu_icon.rco");
		if (ic) pt_read_icons(f, ic);
		PtFile *st = pt_find(files, nf, "/vsh/resource/system_plugin_fg.rco");
		if (st) pt_read_status(f, st);
		if (pt_clock_code_set) pt_clock_x = pt_clock_code_x + 5.0f;       /* the code's 203 is the stock 208 of the clock text's anchor */
		PtFile *fn = pt_find(files, nf, "/font/ltn0.pgf");
		if (fn) {
			uint8_t *fb = pt_read(f, fn->start, fn->size);
			if (fb) {
				pt_font = pt_font_parse(fb, fn->size);
				if (!pt_font) free(fb);
				text_cache_flush();
			}
		}
	}
	fclose(f);
	pt_active = idx;
	bgs_reload();
	sound_apply_theme();
	menu_scale_reload();
	return 1;
}

/* A file from inside the active theme (e.g. "/vsh/resource/opening_plugin.rco"), or NULL; free() it. */
static uint8_t *pt_active_file(const char *name, size_t *size)
{
	if (pt_active < 0 || pt_active >= pt_n) return NULL;
	FILE *f = fopen(pt_list[pt_active].path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f), ptf_end = fsize;
	static PtFile files[80];
	int nf = pt_table(f, fsize, files, 80, &ptf_end);
	uint8_t *r = NULL;
	if (nf > 0) {
		PtFile *pf = pt_find(files, nf, name);
		if (pf && pf->size > 0) { r = pt_read(f, pf->start, pf->size); *size = r ? pf->size : 0; }
	}
	fclose(f);
	return r;
}

/* ---- finding themes ---- */

static int pt_cmp(const void *a, const void *b) { return strcasecmp(((const PtEntry *)a)->name, ((const PtEntry *)b)->name); }

static void pt_scan(void)
{
	char keep[48] = "";
	if (pt_active >= 0 && pt_active < pt_n) snprintf(keep, sizeof(keep), "%s", pt_list[pt_active].name);
	pt_n = 0;
	static const char *dirs[2] = { PT_DIR_APP, PT_DIR_PSP };
	for (int d = 0; d < 2; d++) {
		SceUID fd = sceIoDopen(dirs[d]);
		if (fd < 0) continue;
		SceIoDirent e;
		memset(&e, 0, sizeof(e));
		while (sceIoDread(fd, &e) > 0 && pt_n < PT_MAX) {
			const char *dot = strrchr(e.d_name, '.');
			if (!SCE_S_ISDIR(e.d_stat.st_mode) && dot && (!strcasecmp(dot, ".ctf") || !strcasecmp(dot, ".ptf"))) {
				snprintf(pt_list[pt_n].path, sizeof(pt_list[0].path), "%s/%s", dirs[d], e.d_name);
				snprintf(pt_list[pt_n].name, sizeof(pt_list[0].name), "%.*s", (int)(dot - e.d_name), e.d_name);
				pt_n++;
			}
			memset(&e, 0, sizeof(e));
		}
		sceIoDclose(fd);
	}
	qsort(pt_list, pt_n, sizeof(PtEntry), pt_cmp);
	pt_active = -1;
	for (int i = 0; i < pt_n; i++) if (keep[0] && !strcmp(pt_list[i].name, keep)) pt_active = i;
}

static void pt_save_selection(void)
{
	FILE *f = fopen(PT_SEL_PATH, "wb");
	if (!f) return;
	if (pt_active >= 0 && pt_active < pt_n) fprintf(f, "%s\n", pt_list[pt_active].name);
	fclose(f);
}

/* At startup: scan and re-apply the theme chosen last time. */
static void pt_init(void)
{
	sceIoMkdir(PT_DIR_APP, 0777);
	pt_scan();
	char name[64] = "";
	FILE *f = fopen(PT_SEL_PATH, "rb");
	if (f) {
		if (fgets(name, sizeof(name), f)) name[strcspn(name, "\r\n")] = 0;
		fclose(f);
	}
	if (!name[0]) return;
	for (int i = 0; i < pt_n; i++)
		if (!strcmp(pt_list[i].name, name)) { pt_load(i); break; }
}

/* Settings row: Off, then every theme found, then back to Off. */
static void pt_cycle(int dir)
{
	pt_scan();
	int cur = pt_active;                                   /* -1 = off */
	int total = pt_n + 1;
	int v = ((cur + 1) + dir + total) % total;
	if (v == 0) pt_unload(); else pt_load(v - 1);
	pt_save_selection();
}

static void pt_text(char *out, size_t n)
{
	if (pt_active >= 0 && pt_active < pt_n) snprintf(out, n, "%.40s", pt_list[pt_active].name);
	else snprintf(out, n, pt_n ? "Off" : "Off (none found)");
}
