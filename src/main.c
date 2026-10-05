/*
 * VitaXMB - a PSP-style XMB launcher for the PS Vita.
 * Scans ux0:app for installed games/homebrew and launches them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/audioout.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
#include <psp2/display.h>
#include <psp2/registrymgr.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <setjmp.h>
#include <png.h>
#include <taihen.h>
#include <psp2/vshbridge.h>
#include "vitashell_user.h"
#define MINIMP3_NO_STDIO
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <harfbuzz/hb.h>
#include <vita2d.h>

#define SCREEN_W   960
#define SCREEN_H   544
#define OWN_TITLEID "VXMB00001"
#define CONFIG_DIR  "ux0:data/VitaXMB"
#define CONFIG_PATH CONFIG_DIR "/config.bin"

#define MAX_ITEMS  256
#define ICON_KEEP  7        /* icons kept loaded within +/- this many rows */

#define WHITE(a) RGBA8(255, 255, 255, (a))

/* Textures the GPU may still be reading must not be freed mid-frame (that is what produces
 * GPUCRASH dumps). They are parked here and released after the GPU has gone idle. */
static vita2d_texture *free_list[256];
static int free_count;

static void defer_free(vita2d_texture *t)
{
	if (!t) return;
	if (free_count < 256) { free_list[free_count++] = t; return; }
	vita2d_wait_rendering_done();
	vita2d_free_texture(t);
}

static void trace(const char *fmt, ...);
static void flush_free(void)
{
	static int age;
	if (!free_count) { age = 0; return; }
	if (free_count < 8 && ++age < 45) return;      /* let a few pile up: every flush waits for the GPU */
	age = 0;
	trace("free %d textures\n", free_count);
	vita2d_wait_rendering_done();
	for (int i = 0; i < free_count; i++) vita2d_free_texture(free_list[i]);
	free_count = 0;
}

/* ------------------------------------------------------------------ */
/* Data                                                                */
/* ------------------------------------------------------------------ */

enum { CAT_SETTINGS, CAT_PHOTO, CAT_MUSIC, CAT_VIDEO, CAT_GAME, CAT_NETWORK, CAT_COUNT };
static const char *cat_names[CAT_COUNT] = { "Settings", "Photo", "Music", "Video", "Game", "Network" };

enum { KIND_INFO, KIND_APP, KIND_EXIT, KIND_FOLDER, KIND_VALUE, KIND_URI, KIND_PAGE, KIND_TRACK };
/* values for KIND_VALUE rows */
enum { SET_THEME, SET_CLOCK, SET_SOUND, SET_STARTUP, SET_CONFIRM, SET_LAUNCH, SET_ART, SET_EXTRA };
/* values for KIND_PAGE rows */
enum { PAGE_NONE, PAGE_SYSINFO, PAGE_GAMEINFO, PAGE_PLAYER };

/* Menus: the first CAT_COUNT are the category roots, the rest are nested lists. */
enum { M_MEMCARD = CAT_COUNT, M_SAVES, M_VITAXMB, M_SYSSET, M_THEMESET, M_NETSET, M_VIDEOS, M_TRACKS, M_COUNT };
enum { LAY_COLUMN, LAY_GAME, LAY_SUB };
#define MAX_DEPTH 4

typedef struct {
	char title[64];
	char sub[48];
	char id[16];
	char icon_path[112];    /* optional icon file shown for this row */
	char icon_path2[112];   /* fallback icon file */
	char pic_path[112];     /* fallback full-screen background (pic0.png) */
	char meta_dir[64];      /* ur0:appmeta/<id>/livearea/contents/ for installed apps */
	char gate_path[112];    /* LiveArea gate (rectangle) image */
	char bg_path[112];      /* LiveArea background image */
	int meta_resolved;
	int dec_queued;         /* artwork decryption was requested for this app */
	int icon_rect;          /* icon texture is the rectangular gate image */
	int kind;
	int submenu;            /* KIND_FOLDER: menu to open */
	vita2d_texture *icon;   /* loaded from icon_path, owned */
	vita2d_texture *stock;  /* shared XMB icon, not owned */
	int icon_tried;
	volatile int load_state;   /* 0 idle, 1 wanted, 2 loading (worker), 3 decoded, 4 done */
	uint8_t *pending_pix;      /* decoded RGBA waiting for upload on the render thread */
	int pending_w, pending_h, pending_rect;
	int value_id;              /* KIND_VALUE: SET_*, KIND_PAGE: PAGE_* */
	float glow;                /* 0..1, eases to 1 on the selected row (snappier than the scroll animation) */
	char uri[96];              /* KIND_URI: what to launch */
	char path[112];            /* files: where it lives (videos, ...) */
} Item;

typedef struct {
	Item items[MAX_ITEMS];
	int count;
	int sel;
	float pos;              /* animated selection position */
} Menu;

static Menu menus[M_COUNT];

/* Theme: "monthly" (0) follows the PSP behaviour, 1..12 forces a month. */
static int theme = 0;
static int launch_mode = 0;   /* see launch_request() */
static int art_decrypt = 0;   /* opt-in: decrypt missing LiveArea art via VitaShell modules */
static int clock24 = 1;       /* 24-hour clock like the PSP capture */
static int sound_on = 1;      /* UI sound effects */
static int startup_anim = 1;  /* fade-in / slide-in at launch */
static int confirm_dialogs = 1; /* ask before leaving the XMB */
static int extra_storage = 0;   /* EXPERIMENTAL: also look on a second card (uma0:, imc0:, xmc0:, grw0:) */

/* Base colours per month, as {top, bottom} RGB. */
static const unsigned char month_cols[12][6] = {
	{ 0xc8,0x9a,0x2a,  0x5a,0x3a,0x08 }, /* Jan */
	{ 0x9a,0x7a,0xc8,  0x3a,0x2a,0x68 }, /* Feb */
	{ 0x6a,0xc0,0x7a,  0x1a,0x58,0x34 }, /* Mar */
	{ 0xe8,0x8a,0xb0,  0x7a,0x2a,0x50 }, /* Apr */
	{ 0x4a,0xc0,0x4a,  0x0a,0x4a,0x18 }, /* May */
	{ 0x4a,0xd0,0xc0,  0x0a,0x54,0x6a }, /* Jun */
	{ 0x3a,0x90,0xe8,  0x08,0x2a,0x7a }, /* Jul */
	{ 0x30,0x70,0xd8,  0x08,0x20,0x6a }, /* Aug */
	{ 0xe0,0xa0,0x30,  0x6a,0x30,0x08 }, /* Sep */
	{ 0xe0,0x70,0x30,  0x6a,0x20,0x08 }, /* Oct */
	{ 0x90,0x60,0x40,  0x38,0x20,0x14 }, /* Nov */
	{ 0xd8,0x40,0x50,  0x62,0x10,0x1c }, /* Dec */
};


/* ------------------------------------------------------------------ */
/* Assets                                                              */
/* ------------------------------------------------------------------ */

static vita2d_texture *xicon(const char *name)
{
	char path[96];
	snprintf(path, sizeof(path), "app0:assets/icons/tex_%s.png", name);
	vita2d_texture *t = vita2d_load_PNG_file(path);
	if (t) vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return t;
}

static vita2d_texture *cat_tex[CAT_COUNT];
static vita2d_texture *tex_theme, *tex_psp, *tex_exit, *tex_photo_s, *tex_music_s, *tex_video_s, *tex_net_s, *tex_game_s, *tex_savedata_s, *tex_ms_s, *tex_launch, *tex_badge, *tex_browser, *tex_remote, *tex_sharing, *tex_date, *tex_usb, *tex_rss, *tex_manual, *tex_lftv;

static void load_icons(void)
{
	cat_tex[CAT_SETTINGS] = xicon("system");
	cat_tex[CAT_PHOTO]    = xicon("photo");
	cat_tex[CAT_MUSIC]    = xicon("music");
	cat_tex[CAT_VIDEO]    = xicon("video");
	cat_tex[CAT_GAME]     = xicon("game");
	cat_tex[CAT_NETWORK]  = xicon("network");
	tex_theme   = xicon("cnf_theme");
	tex_psp     = xicon("cnf_psp");
	tex_exit    = xicon("cnf_save_energy");
	tex_photo_s = xicon("cnf_photo");
	tex_music_s = xicon("cnf_sound");
	tex_video_s = xicon("cnf_video");
	tex_net_s   = xicon("cnf_network");
	tex_game_s  = xicon("game");
	tex_savedata_s = xicon("savedata");
	tex_ms_s    = xicon("ms");
	tex_launch  = xicon("cnf_update");
	tex_badge   = xicon("badge");
	tex_browser = xicon("browser");
	tex_remote  = xicon("remote");
	tex_sharing = xicon("sharing");
	tex_date    = xicon("cnf_date");
	tex_usb     = xicon("cnf_usb");
	tex_rss     = xicon("rss");
	tex_manual  = xicon("manual");
	tex_lftv    = xicon("lftv");
}

/* ------------------------------------------------------------------ */
/* Sound: pre-decoded 48 kHz s16 stereo PCM mixed on a worker thread   */
/* ------------------------------------------------------------------ */

enum { SND_OPENING, SND_CANCEL, SND_CURSOR, SND_CONFIRM, SND_STARTGAME, SND_COUNT };
static const char *snd_files[SND_COUNT] = { "opening", "cancel", "cursor", "confirm", "startgame" };

typedef struct { int16_t *data; int frames; volatile int pos; } Sound;
static Sound sounds[SND_COUNT];
static volatile int audio_run = 1;

#define AUDIO_GRAIN 1024

/* Music: the decoder thread fills this ring with 48 kHz stereo; the mixer drains it. */
#define MUS_RING 65536
static int16_t mus_ring[MUS_RING * 2];
static volatile unsigned mus_w, mus_r;
static volatile int mus_playing;
static volatile uint64_t mus_consumed;
#define VIS_N 1024
static int16_t vis_buf[VIS_N];
static volatile unsigned vis_w;

static void sound_load_all(void)
{
	for (int i = 0; i < SND_COUNT; i++) {
		char path[96];
		snprintf(path, sizeof(path), "app0:assets/sounds/%s.pcm", snd_files[i]);
		SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (fd < 0) continue;
		int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
		sceIoLseek32(fd, 0, SCE_SEEK_SET);
		int16_t *buf = malloc(size);
		if (buf && sceIoRead(fd, buf, size) == size) {
			sounds[i].data = buf;
			sounds[i].frames = size / 4;
			sounds[i].pos = sounds[i].frames; /* idle */
		} else {
			free(buf);
		}
		sceIoClose(fd);
	}
}

static void sound_play(int id)
{
	if (sound_on && sounds[id].data) sounds[id].pos = 0;
}

static int sound_playing(int id)
{
	return sounds[id].data && sounds[id].pos < sounds[id].frames;
}

static int audio_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) return 0;
	static int16_t mix[AUDIO_GRAIN * 2];
	int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

	while (audio_run) {
		int32_t acc[AUDIO_GRAIN * 2];
		memset(acc, 0, sizeof(acc));
		for (int i = 0; i < SND_COUNT; i++) {
			Sound *s = &sounds[i];
			int pos = s->pos;
			if (!s->data || pos >= s->frames) continue;
			int n = s->frames - pos;
			if (n > AUDIO_GRAIN) n = AUDIO_GRAIN;
			for (int k = 0; k < n * 2; k++) acc[k] += s->data[pos * 2 + k];
			s->pos = pos + n;
		}
		if (mus_playing) {
			unsigned avail = mus_w - mus_r;
			unsigned n = avail < AUDIO_GRAIN ? avail : AUDIO_GRAIN;
			for (unsigned i = 0; i < n; i++) {
				unsigned idx = (mus_r + i) & (MUS_RING - 1);
				int l = mus_ring[idx * 2], r = mus_ring[idx * 2 + 1];
				acc[i * 2] += l * 85 / 100;
				acc[i * 2 + 1] += r * 85 / 100;
				vis_buf[(vis_w + i) % VIS_N] = (int16_t)((l + r) / 2);
			}
			vis_w += n; mus_r += n; mus_consumed += n;
		}
		for (int k = 0; k < AUDIO_GRAIN * 2; k++)
			mix[k] = acc[k] > 32767 ? 32767 : (acc[k] < -32768 ? -32768 : acc[k]);
		sceAudioOutOutput(port, mix);
	}
	sceAudioOutReleasePort(port);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

static void config_load(void)
{
	SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	int v[8] = { 0, 0, 0, 1, 1, 1, 1, 0 };
	int n = sceIoRead(fd, v, sizeof(v));
	sceIoClose(fd);
	if (n >= 4 && v[0] >= 0 && v[0] <= 12) theme = v[0];
	if (n >= 8 && v[1] >= 0 && v[1] <= 3) launch_mode = v[1];
	if (n >= 12 && (v[2] == 0 || v[2] == 1)) art_decrypt = v[2];
	if (n >= 16 && (v[3] == 0 || v[3] == 1)) clock24 = v[3];
	if (n >= 20 && (v[4] == 0 || v[4] == 1)) sound_on = v[4];
	if (n >= 24 && (v[5] == 0 || v[5] == 1)) startup_anim = v[5];
	if (n >= 28 && (v[6] == 0 || v[6] == 1)) confirm_dialogs = v[6];
	if (n >= 32 && (v[7] == 0 || v[7] == 1)) extra_storage = v[7];
}

static void ensure_dirs(void)
{
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(CONFIG_DIR, 0777);
}

static void config_save(void)
{
	ensure_dirs();
	SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return;
	int v[8] = { theme, launch_mode, art_decrypt, clock24, sound_on, startup_anim, confirm_dialogs, extra_storage };
	sceIoWrite(fd, v, sizeof(v));
	sceIoClose(fd);
}

/* ------------------------------------------------------------------ */
/* param.sfo parsing                                                   */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t magic, version, key_off, data_off, count; } SfoHeader;
typedef struct { uint16_t key_off, fmt; uint32_t len, max_len, data_off; } SfoEntry;

static int sfo_get_string(const char *path, const char *key, char *out, size_t outlen)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;

	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	if (size < (int)sizeof(SfoHeader) || size > 64 * 1024) { sceIoClose(fd); return 0; }

	uint8_t *buf = malloc(size);
	if (!buf) { sceIoClose(fd); return 0; }
	int rd = sceIoRead(fd, buf, size);
	sceIoClose(fd);

	int found = 0;
	if (rd == size) {
		const SfoHeader *h = (const SfoHeader *)buf;
		if (h->magic == 0x46535000 && h->count < 128 &&
		    sizeof(SfoHeader) + h->count * sizeof(SfoEntry) <= (size_t)size) {
			const SfoEntry *e = (const SfoEntry *)(buf + sizeof(SfoHeader));
			for (uint32_t i = 0; i < h->count; i++) {
				if (h->key_off + e[i].key_off >= (uint32_t)size) continue;
				const char *k = (const char *)buf + h->key_off + e[i].key_off;
				if (strncmp(k, key, 32) != 0) continue;
				if (h->data_off + e[i].data_off + e[i].len > (uint32_t)size) break;
				size_t n = e[i].len < outlen - 1 ? e[i].len : outlen - 1;
				memcpy(out, buf + h->data_off + e[i].data_off, n);
				out[n] = 0;
				found = 1;
				break;
			}
		}
	}
	free(buf);
	return found;
}

/* ------------------------------------------------------------------ */
/* PNG loading (own libpng path: handles palette/gray/16-bit icons)    */
/* ------------------------------------------------------------------ */

#include <stdarg.h>
static void xlog(const char *fmt, ...)
{
	static int count = 0;
	if (count++ > 200) return;
	SceUID fd = sceIoOpen(CONFIG_DIR "/log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
	if (fd < 0) return;
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0) sceIoWrite(fd, buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
	sceIoClose(fd);
}

/* Breadcrumbs for freezes: the last line written is the last thing that happened. */
static void trace(const char *fmt, ...)
{
#ifndef VITAXMB_DEBUG
	(void)fmt;
	return;
#endif
	static int n;
	SceUID fd = sceIoOpen(CONFIG_DIR "/trace.txt",
	                      SCE_O_WRONLY | SCE_O_CREAT | (n++ == 0 ? SCE_O_TRUNC : SCE_O_APPEND), 0777);
	if (fd < 0) return;
	char buf[200];
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len > 0) sceIoWrite(fd, buf, len < (int)sizeof(buf) ? len : (int)sizeof(buf) - 1);
	sceIoClose(fd);
}

static char png_err[96];
static void png_err_fn(png_structp png, png_const_charp msg)
{
	snprintf(png_err, sizeof(png_err), "%s", msg);
	longjmp(png_jmpbuf(png), 1);
}
static void png_warn_fn(png_structp png, png_const_charp msg) { (void)png; (void)msg; }

typedef struct { const uint8_t *p; size_t len, pos; } MemRd;

static void png_mem_read(png_structp png, png_bytep out, png_size_t n)
{
	MemRd *m = png_get_io_ptr(png);
	if (m->pos + n > m->len) png_error(png, "eof");
	memcpy(out, m->p + m->pos, n);
	m->pos += n;
}

/* Decodes to a malloc'd RGBA buffer (w*h*4). Safe to call from a worker thread. */
static uint8_t *png_decode(const char *path, int *out_w, int *out_h)
{
	trace("decode %s\n", path);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) { xlog("open fail %08x %s\n", fd, path); return NULL; }
	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	if (size <= 8 || size > 8 * 1024 * 1024) { xlog("bad size %d %s\n", size, path); sceIoClose(fd); return NULL; }
	uint8_t *volatile buf = malloc(size);
	if (!buf || sceIoRead(fd, buf, size) != size) { xlog("read fail %s\n", path); free(buf); sceIoClose(fd); return NULL; }
	sceIoClose(fd);

	uint8_t *volatile pix = NULL;
	png_bytep *volatile rows = NULL;
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, png_err_fn, png_warn_fn);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!png || !info) { free(buf); if (png) png_destroy_read_struct(&png, NULL, NULL); return NULL; }

	if (setjmp(png_jmpbuf(png))) {
		xlog("png error '%s' %s\n", png_err, path);
		free(pix); pix = NULL;
		goto done;
	}

	png_set_crc_action(png, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);
#ifdef PNG_BENIGN_READ_ERRORS_SUPPORTED
	png_set_benign_errors(png, 1);
#endif
	MemRd rd = { buf, (size_t)size, 0 };
	png_set_read_fn(png, &rd, png_mem_read);
	png_read_info(png, info);

	png_uint_32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
	int bit = png_get_bit_depth(png, info), ct = png_get_color_type(png, info);
	if (w == 0 || h == 0 || w > 2048 || h > 2048) { xlog("bad dims %s\n", path); goto done; }

	if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
	if (ct == PNG_COLOR_TYPE_GRAY && bit < 8) png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
	if (bit == 16) png_set_strip_16(png);
	if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
	png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	png_set_interlace_handling(png);
	png_read_update_info(png, info);

	pix = malloc((size_t)w * h * 4);
	rows = malloc(sizeof(png_bytep) * h);
	if (!pix || !rows) { free(pix); pix = NULL; goto done; }
	for (png_uint_32 i = 0; i < h; i++) rows[i] = pix + (size_t)i * w * 4;
	png_read_image(png, rows);
	*out_w = (int)w; *out_h = (int)h;

done:
	png_destroy_read_struct(&png, &info, NULL);
	free(rows);
	free(buf);
	return pix;
}

static vita2d_texture *texture_from_rgba(const uint8_t *pix, int w, int h)
{
	trace("texture %dx%d\n", w, h);
	vita2d_texture *tex = vita2d_create_empty_texture(w, h);
	if (!tex) return NULL;
	uint8_t *data = vita2d_texture_get_datap(tex);
	int stride = vita2d_texture_get_stride(tex);
	for (int y = 0; y < h; y++) memcpy(data + y * stride, pix + (size_t)y * w * 4, (size_t)w * 4);
	vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return tex;
}

static vita2d_texture *load_png_any(const char *path)
{
	int w = 0, h = 0;
	uint8_t *pix = png_decode(path, &w, &h);
	if (!pix) return NULL;
	vita2d_texture *t = texture_from_rgba(pix, w, h);
	free(pix);
	if (!t) xlog("texture alloc fail %dx%d %s\n", w, h, path);
	return t;
}

/* Decodes and shrinks big images (box filter) so a list icon doesn't cost 2 MB of VRAM.
 * CPU only, so the loader thread can run it. */
static uint8_t *png_decode_scaled(const char *path, int max_w, int *out_w, int *out_h)
{
	int w = 0, h = 0;
	uint8_t *pix = png_decode(path, &w, &h);
	if (!pix) return NULL;
	int f = w / max_w;
	if (f < 2) { *out_w = w; *out_h = h; return pix; }
	int nw = w / f, nh = h / f;
	uint8_t *out = malloc((size_t)nw * nh * 4);
	if (!out) { free(pix); return NULL; }
	for (int y = 0; y < nh; y++)
		for (int x = 0; x < nw; x++) {
			unsigned sum[4] = { 0, 0, 0, 0 };
			for (int dy = 0; dy < f; dy++)
				for (int dx = 0; dx < f; dx++) {
					const uint8_t *px = pix + ((size_t)(y * f + dy) * w + (x * f + dx)) * 4;
					for (int c = 0; c < 4; c++) sum[c] += px[c];
				}
			for (int c = 0; c < 4; c++) out[((size_t)y * nw + x) * 4 + c] = (uint8_t)(sum[c] / (f * f));
		}
	free(pix);
	*out_w = nw; *out_h = nh;
	return out;
}

/* ---- background image loader: decode off the render thread ---- */
static char          bg_req_path[112];
static volatile int  bg_state;          /* 0 idle, 1 requested, 2 working, 3 done */
static uint8_t      *bg_pix;
static int           bg_w, bg_h;
static char          bg_done_path[112];
static volatile int  bg_run = 1;

static int bg_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	while (bg_run) {
		if (bg_state == 1) {
			bg_state = 2;
			char path[112];
			snprintf(path, sizeof(path), "%s", bg_req_path);
			int w = 0, h = 0;
			uint8_t *pix = png_decode(path, &w, &h);
			bg_pix = pix; bg_w = w; bg_h = h;
			snprintf(bg_done_path, sizeof(bg_done_path), "%s", path);
			bg_state = 3;
		}
		sceKernelDelayThread(8000);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Item population                                                     */
/* ------------------------------------------------------------------ */

static Item *add_item(int m, int kind, const char *title, const char *sub, const char *id,
                      vita2d_texture *stock)
{
	Menu *mn = &menus[m];
	if (mn->count >= MAX_ITEMS) return NULL;
	Item *it = &mn->items[mn->count++];
	memset(it, 0, sizeof(*it));
	it->kind = kind;
	it->submenu = -1;
	it->stock = stock;
	snprintf(it->title, sizeof(it->title), "%s", title);
	snprintf(it->sub, sizeof(it->sub), "%s", sub ? sub : "");
	snprintf(it->id, sizeof(it->id), "%s", id ? id : "");
	return it;
}

/* ---- VitaShell's modules: mount a game's encrypted PFS so its files read as plain ---- */

#define VS_DIR "ux0:VitaShell/module/"
#define CACHE_ROOT CONFIG_DIR "/cache"

static SceUID vs_patch_id = -1, vs_kernel_id = -1, vs_user_id = -1;
static int vs_state;                    /* 0 untried, 1 ready, -1 unavailable */
static char vs_mp[64];

static int vs_load_kernel(const char *name, const char *file, SceUID *id)
{
	int unk[2];
	if (_vshKernelSearchModuleByName(name, unk) >= 0) return 1;     /* already loaded */
	SceUID m = taiLoadKernelModule(file, 0, NULL);
	if (m < 0) { xlog("tai load %s: %08x\n", file, m); return 0; }
	int r = taiStartKernelModule(m, 0, NULL, 0, NULL, NULL);
	if (r < 0) {
		xlog("tai start %s: %08x\n", file, r);
		taiStopUnloadKernelModule(m, 0, NULL, 0, NULL, NULL);
		return 0;
	}
	*id = m;
	return 1;
}

static int vs_init(void)
{
	if (vs_state) return vs_state > 0;
	vs_state = -1;
	SceIoStat st;
	if (sceIoGetstat(VS_DIR "user.suprx", &st) < 0 || sceIoGetstat(VS_DIR "kernel.skprx", &st) < 0 ||
	    sceIoGetstat(VS_DIR "patch.skprx", &st) < 0) {
		xlog("VitaShell modules not found in " VS_DIR "\n");
		return 0;
	}
	vs_load_kernel("VitaShellPatch", VS_DIR "patch.skprx", &vs_patch_id);
	if (!vs_load_kernel("VitaShellKernel2", VS_DIR "kernel.skprx", &vs_kernel_id)) return 0;
	vs_user_id = sceKernelLoadStartModule(VS_DIR "user.suprx", 0, NULL, 0, NULL, NULL);
	if (vs_user_id < 0) { xlog("user.suprx: %08x\n", vs_user_id); return 0; }
	vs_state = 1;
	return 1;
}

/* Unload whatever this app loaded, so VitaShell can load its own copies later. */
static void vs_shutdown(void)
{
	if (vs_user_id >= 0) { sceKernelStopUnloadModule(vs_user_id, 0, NULL, 0, NULL, NULL); vs_user_id = -1; }
	if (vs_kernel_id >= 0) { taiStopUnloadKernelModule(vs_kernel_id, 0, NULL, 0, NULL, NULL); vs_kernel_id = -1; }
	if (vs_patch_id >= 0) { taiStopUnloadKernelModule(vs_patch_id, 0, NULL, 0, NULL, NULL); vs_patch_id = -1; }
	vs_state = 0;
}

static int vs_mount(const char *path)
{
	static const int ids[] = { 0x6E, 0x12E, 0x12F, 0x3ED };
	char klicensee[0x10];
	memset(klicensee, 0, sizeof(klicensee));
	vs_mp[0] = 0;
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		ShellMountIdArgs args;
		memset(&args, 0, sizeof(args));
		args.id = ids[i];
		args.process_titleid = "VITASHELL";
		args.path = path;
		args.klicensee = klicensee;
		args.mount_point = vs_mp;
		int r = shellUserMountById(&args);
		if (r >= 0) return r;
	}
	return sceAppMgrGameDataMount(path, 0, 0, vs_mp);
}

static void vs_umount(void)
{
	if (vs_mp[0]) { sceAppMgrUmount(vs_mp); vs_mp[0] = 0; }
}

/* ---- helpers ---- */

static int is_png_file(const char *path)
{
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	uint8_t b[8];
	int n = sceIoRead(fd, b, 8);
	sceIoClose(fd);
	return n == 8 && memcmp(b, sig, 8) == 0;
}

static int copy_file(const char *src, const char *dst)
{
	SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (in < 0) return 0;
	SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (out < 0) { sceIoClose(in); return 0; }
	static uint8_t buf[64 * 1024];            /* only the decrypt thread copies */
	int ok = 1, n;
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0)
		if (sceIoWrite(out, buf, n) != n) { ok = 0; break; }
	if (n < 0) ok = 0;
	sceIoClose(in);
	sceIoClose(out);
	return ok;
}

/* Finds the text of the first <tag ...>TEXT</tag> after `section` in `xml`. */
static int xml_first(const char *xml, const char *section, const char *tag, char *out, size_t cap)
{
	const char *p = section ? strstr(xml, section) : xml;
	if (!p) return 0;
	char open[40];
	snprintf(open, sizeof(open), "<%s", tag);
	p = strstr(p, open);
	if (!p) return 0;
	p = strchr(p, '>');
	if (!p) return 0;
	p++;
	const char *e = strchr(p, '<');
	if (!e) return 0;
	while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
	size_t n = (size_t)(e - p);
	while (n && (p[n - 1] == ' ' || p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == '\t')) n--;
	if (n == 0 || n >= cap) return 0;
	memcpy(out, p, n);
	out[n] = 0;
	return 1;
}

static int has_ci(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return 1;
	return 0;
}

/* Looks in `dir` (trailing slash) for a LiveArea gate image and background. Fills only the
 * outputs that are still empty, and only with files that really are PNGs (the retail ones
 * under ux0:app are encrypted). template.xml names them; a name scan is the fallback. */
static void livearea_scan(const char *dir, char *gate, char *bg)
{
	char full[112], name[64];
	if (gate[0] && bg[0]) return;

	char *xml = malloc(16384);
	if (xml) {
		char path[160];
		snprintf(path, sizeof(path), "%stemplate.xml", dir);
		SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (fd >= 0) {
			int n = sceIoRead(fd, xml, 16383);
			sceIoClose(fd);
			if (n > 0) {
				xml[n] = 0;
				if (!bg[0] && xml_first(xml, "<livearea-background", "image", name, sizeof(name))) {
					snprintf(full, sizeof(full), "%s%s", dir, name);
					if (is_png_file(full)) snprintf(bg, 112, "%s", full);
				}
				if (!gate[0] && xml_first(xml, "<gate", "startup-image", name, sizeof(name))) {
					snprintf(full, sizeof(full), "%s%s", dir, name);
					if (is_png_file(full)) snprintf(gate, 112, "%s", full);
				}
			}
		}
		free(xml);
	}

	if (!gate[0] || !bg[0]) {
		SceUID d = sceIoDopen(dir);
		if (d >= 0) {
			SceIoDirent e;
			memset(&e, 0, sizeof(e));
			while (sceIoDread(d, &e) > 0) {
				if (has_ci(e.d_name, ".png")) {
					int is_gate = has_ci(e.d_name, "gate") || has_ci(e.d_name, "startup");
					int is_bg = !is_gate && (has_ci(e.d_name, "bg") || has_ci(e.d_name, "background"));
					snprintf(full, sizeof(full), "%s%s", dir, e.d_name);
					if (is_gate && !gate[0] && is_png_file(full)) snprintf(gate, 112, "%s", full);
					else if (is_bg && !bg[0] && is_png_file(full)) snprintf(bg, 112, "%s", full);
				}
				memset(&e, 0, sizeof(e));
			}
			sceIoDclose(d);
		}
	}
}

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
	if (mn->count == 0)
		add_item(M_MEMCARD, KIND_INFO, "No games found", "Nothing in ux0:app", NULL, tex_game_s);
	if (mn->sel >= mn->count) mn->sel = 0;
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

static void rescan_media(void);

static const char *theme_names[13] = { "Automatic (by month)", "January", "February", "March", "April",
	"May", "June", "July", "August", "September", "October", "November", "December" };
static const char *launch_names[4] = { "A: standard", "B: open flag", "C: launch flag", "D: stay open" };

static void setting_text(int id, char *out, size_t n)
{
	const char *onoff[2] = { "Off", "On" };
	switch (id) {
	case SET_THEME:   snprintf(out, n, "%s", theme_names[theme]); break;
	case SET_CLOCK:   snprintf(out, n, "%s", clock24 ? "24-hour" : "12-hour"); break;
	case SET_SOUND:   snprintf(out, n, "%s", onoff[sound_on]); break;
	case SET_STARTUP: snprintf(out, n, "%s", onoff[startup_anim]); break;
	case SET_CONFIRM: snprintf(out, n, "%s", onoff[confirm_dialogs]); break;
	case SET_LAUNCH:  snprintf(out, n, "%s", launch_names[launch_mode]); break;
	case SET_ART:     snprintf(out, n, "%s", onoff[art_decrypt]); break;
	case SET_EXTRA: {
		if (!extra_storage) { snprintf(out, n, "Off"); break; }
		char devs[4][8];
		int nd = extra_devs(devs);
		if (nd == 0) snprintf(out, n, "On (none)");
		else if (nd == 1) snprintf(out, n, "On (%s)", devs[0]);
		else snprintf(out, n, "On (%d cards)", nd);
		break; }
	default: out[0] = 0;
	}
}

/* Re-reads every value row's text after a setting changed. */
static void theme_item_update(void)
{
	for (int m = 0; m < M_COUNT; m++) {
		Menu *mn = &menus[m];
		for (int i = 0; i < mn->count; i++)
			if (mn->items[i].kind == KIND_VALUE)
				setting_text(mn->items[i].value_id, mn->items[i].sub, sizeof(mn->items[i].sub));
	}
}

static void setting_change(int id, int dir)
{
	switch (id) {
	case SET_THEME:   theme = (theme + dir + 13) % 13; break;
	case SET_CLOCK:   clock24 = !clock24; break;
	case SET_SOUND:   sound_on = !sound_on; break;
	case SET_STARTUP: startup_anim = !startup_anim; break;
	case SET_CONFIRM: confirm_dialogs = !confirm_dialogs; break;
	case SET_LAUNCH:  launch_mode = (launch_mode + dir + 4) % 4; break;
	case SET_EXTRA:
		extra_storage = !extra_storage;
		rescan_media();
		break;
	case SET_ART:
		art_decrypt = !art_decrypt;
		if (art_decrypt) {
			Menu *am = &menus[M_MEMCARD];
			for (int i = 0; i < am->count; i++) { am->items[i].meta_resolved = 0; am->items[i].dec_queued = 0; }
		}
		break;
	}
	theme_item_update();
	config_save();
}

static Item *add_value(int m, const char *title, int id, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_VALUE, title, "", NULL, icon);
	if (it) it->value_id = id;
	return it;
}

static Item *add_uri(int m, const char *title, const char *uri, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_URI, title, "", NULL, icon);
	if (it) snprintf(it->uri, sizeof(it->uri), "%s", uri);
	return it;
}

static Item *add_folder(int m, const char *title, const char *sub, int submenu, vita2d_texture *icon)
{
	Item *it = add_item(m, KIND_FOLDER, title, sub, NULL, icon);
	if (it) it->submenu = submenu;
	return it;
}

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

static void build_menus(void)
{
	/* Settings: the PSP's own groups, wired to what exists on a Vita */
	add_folder(CAT_SETTINGS, "System Settings", "", M_SYSSET, tex_psp);
	add_folder(CAT_SETTINGS, "Theme Settings", "", M_THEMESET, tex_theme);
	add_folder(CAT_SETTINGS, "Network Settings", "", M_NETSET, tex_net_s);
	add_folder(CAT_SETTINGS, "VitaXMB Settings", "", M_VITAXMB, tex_launch);
	add_item(CAT_SETTINGS, KIND_EXIT, "Exit to LiveArea", "", NULL, tex_exit);

	Item *pg = add_item(M_SYSSET, KIND_PAGE, "System Information", "", NULL, tex_psp);
	if (pg) pg->value_id = PAGE_SYSINFO;
	add_uri(M_SYSSET, "Vita System Settings", "settings_dlg:", tex_psp);
	add_uri(M_SYSSET, "System Update", "settings_dlg:updater", tex_psp);

	add_value(M_THEMESET, "Color", SET_THEME, tex_theme);
	add_uri(M_THEMESET, "Vita Themes", "settings_dlg:custom_themes", tex_theme);

	add_uri(M_NETSET, "Wi-Fi Settings", "settings_dlg:wifi", tex_net_s);
	add_uri(M_NETSET, "Sign In to PlayStation Network", "settings_dlg:signin", tex_net_s);

	add_value(M_VITAXMB, "Clock Format", SET_CLOCK, tex_date);
	add_value(M_VITAXMB, "Sound Effects", SET_SOUND, tex_music_s);
	add_value(M_VITAXMB, "Startup Animation", SET_STARTUP, tex_launch);
	add_value(M_VITAXMB, "Confirmation Dialogs", SET_CONFIRM, tex_usb);
	add_value(M_VITAXMB, "Game Launch Method", SET_LAUNCH, tex_launch);
	add_value(M_VITAXMB, "Extra Storage (beta)", SET_EXTRA, tex_ms_s);
	add_value(M_VITAXMB, "Decrypt Artwork (beta)", SET_ART, tex_photo_s);
	theme_item_update();

	/* Photo / Music / Video open the Vita's own apps */
	{
		Item *ph = add_uri(CAT_PHOTO, "Memory Card", "photo:browse?category=ALL", tex_ms_s);
		if (ph) snprintf(ph->sub, sizeof(ph->sub), "Open in Photos");
	}
	add_folder(CAT_MUSIC, "Memory Card", "", M_TRACKS, tex_ms_s);
	scan_music_wrapper();
	add_folder(CAT_VIDEO, "Memory Card", "", M_VIDEOS, tex_ms_s);
	scan_videos();

	/* Network: the Vita's online apps */
	/* System apps are opened with their own URI schemes: starting them by title ID
	 * (psgm:play?titleid=NPXS...) makes the system show error C2-12570-5. */
	add_uri(CAT_NETWORK, "Internet Browser", "wbapp0:", tex_browser);
	add_uri(CAT_NETWORK, "PlayStation Store", "psns:browse?category=STORE-MSF73008-VITAGAMES", tex_net_s);
	add_uri(CAT_NETWORK, "Party", "pspy:", tex_sharing);
	add_uri(CAT_NETWORK, "Messages", "psnmsg:", tex_rss);
	add_uri(CAT_NETWORK, "Near", "near:", tex_remote);

	Item *it;
	it = add_item(CAT_GAME, KIND_FOLDER, "Saved Data Utility", "", NULL, tex_savedata_s);
	if (it) it->submenu = M_SAVES;
	it = add_item(CAT_GAME, KIND_FOLDER, "Memory Card", "", NULL, tex_ms_s);
	if (it) it->submenu = M_MEMCARD;
	menus[CAT_GAME].sel = 1;

	scan_apps();
	scan_saves();
	update_game_counts();
}

/* Icon art is decoded on a worker thread (file I/O + PNG decode + downscale are far too slow for
 * the render thread: they caused 40-370 ms hitches while scrolling). This function only decides
 * which rows want art, and uploads finished pixels, a couple per frame. */
static void update_icons(int m, int keep_other)
{
	int uploads = 0;
	for (int k = 0; k < M_COUNT; k++) {
		Menu *mn = &menus[k];
		int visible = (k == m || k == keep_other);
		for (int i = 0; i < mn->count; i++) {
			Item *it = &mn->items[i];
			if (!it->icon_path[0]) continue;
			int near = visible && abs(i - mn->sel) <= ICON_KEEP;
			int st = it->load_state;

			if (st == 3) {                                   /* decoded: upload or discard */
				if (near && uploads < 2) {
					if (it->pending_pix) {
						it->icon = texture_from_rgba(it->pending_pix, it->pending_w, it->pending_h);
						it->icon_rect = it->icon ? it->pending_rect : 0;
						free(it->pending_pix);
						it->pending_pix = NULL;
					}
					it->load_state = 4;
					uploads++;
				} else if (!near) {
					free(it->pending_pix);
					it->pending_pix = NULL;
					it->load_state = 0;
					it->icon_tried = 0;
				}
				continue;
			}
			if (near) {
				if (st == 0 && !it->icon && !it->icon_tried) { it->icon_tried = 1; it->load_state = 1; }
			} else {
				if (st == 1) { it->load_state = 0; it->icon_tried = 0; }
				if (st == 4 || it->icon) {
					if (it->icon) defer_free(it->icon);
					it->icon = NULL;
					it->icon_rect = 0;
					it->icon_tried = 0;
					it->load_state = 0;
				}
			}
		}
	}
}

static void load_item_art(Item *it)
{
	int w = 0, h = 0, rect = 0;
	uint8_t *pix = NULL;
	if (it->kind == KIND_APP) {
		resolve_meta(it);
		if (it->gate_path[0]) { pix = png_decode_scaled(it->gate_path, 320, &w, &h); rect = pix != NULL; }
	}
	if (!pix) pix = png_decode_scaled(it->icon_path, 320, &w, &h);
	if (!pix && it->icon_path2[0]) pix = png_decode_scaled(it->icon_path2, 320, &w, &h);
	it->pending_pix = pix;
	it->pending_w = w; it->pending_h = h;
	it->pending_rect = rect;
}

static volatile int loader_run = 1;

static int icon_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	while (loader_run) {
		if (loader_pause) { sceKernelDelayThread(2000); continue; }
		Item *best = NULL;
		int bestd = 1 << 30;
		for (int k = M_MEMCARD; k <= M_SAVES; k++) {         /* the only menus whose rows carry art */
			Menu *mn = &menus[k];
			for (int i = 0; i < mn->count; i++) {
				Item *it = &mn->items[i];
				if (it->load_state != 1) continue;
				int d = abs(i - mn->sel);                    /* nearest the cursor first */
				if (d < bestd) { bestd = d; best = it; }
			}
		}
		if (!best) { sceKernelDelayThread(3000); continue; }
		loader_busy = 1;
		best->load_state = 2;
		load_item_art(best);
		__sync_synchronize();
		best->load_state = 3;
		loader_busy = 0;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* ------------------------------------------------------------------ */
/* Text: FreeType glyphs (light hinting) in a padded atlas, with a     */
/* pre-blurred shadow bitmap per glyph. Font: FOT-NewRodin Pro DB.     */
/* ------------------------------------------------------------------ */

#define ATLAS_W 2048
#define ATLAS_H 1024
#define GLYPH_SLOTS 4096        /* power of two */
#define GLYPH_PAD 2
#define SHADOW_PAD 4

typedef struct {
	uint32_t key;               /* (size << 24) | codepoint, 0 = empty */
	short ax, ay, aw, ah;       /* main bitmap rect in the atlas */
	short sx, sy, sw, sh;       /* shadow bitmap rect in the atlas */
	short left, top;            /* main bitmap offset from pen / baseline */
	short sleft, stop;          /* shadow bitmap offset */
	float adv;
} Glyph;

static hb_font_t *hb_fnt;
static int text_flat;           /* test page: no shadow pass */
static int text_hint_mode = 2;   /* 0 light, 1 none, 2 light+autohint (even baseline), see remote hint:N */
static FT_Library ft_lib;
static FT_Face    ft_face;
static uint8_t   *ft_data;
static vita2d_texture *atlas;
static Glyph      glyphs[GLYPH_SLOTS];
static int        atlas_x, atlas_y, atlas_row_h;
static int        atlas_dirty;  /* full: clear at the next frame start */

static int text_init(const char *path)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	ft_data = malloc(size);
	if (!ft_data || sceIoRead(fd, ft_data, size) != size) { sceIoClose(fd); return 0; }
	sceIoClose(fd);

	if (FT_Init_FreeType(&ft_lib)) return 0;
	if (FT_New_Memory_Face(ft_lib, ft_data, size, 0, &ft_face)) return 0;
	{
		hb_blob_t *blob = hb_blob_create((const char *)ft_data, size, HB_MEMORY_MODE_READONLY, NULL, NULL);
		hb_face_t *face = hb_face_create(blob, 0);
		hb_fnt = hb_font_create(face);
		hb_blob_destroy(blob);
	}
	atlas = vita2d_create_empty_texture(ATLAS_W, ATLAS_H);
	if (!atlas) return 0;
	vita2d_texture_set_filters(atlas, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	memset(vita2d_texture_get_datap(atlas), 0, vita2d_texture_get_stride(atlas) * ATLAS_H);
	return 1;
}

/* Call at the start of a frame, before any text is drawn. */
static void text_frame_begin(void)
{
	if (!atlas_dirty) return;
	vita2d_wait_rendering_done();       /* the GPU may still be reading last frame's glyphs */
	memset(vita2d_texture_get_datap(atlas), 0, vita2d_texture_get_stride(atlas) * ATLAS_H);
	memset(glyphs, 0, sizeof(glyphs));
	atlas_x = atlas_y = atlas_row_h = 0;
	atlas_dirty = 0;
}

static int atlas_alloc(int w, int h, int *ox, int *oy)
{
	if (atlas_x + w > ATLAS_W) { atlas_x = 0; atlas_y += atlas_row_h; atlas_row_h = 0; }
	if (atlas_y + h > ATLAS_H) return 0;
	*ox = atlas_x; *oy = atlas_y;
	atlas_x += w;
	if (h > atlas_row_h) atlas_row_h = h;
	return 1;
}

static void atlas_put(int x, int y, int w, int h, const uint8_t *cov, int cstride)
{
	uint8_t *base = vita2d_texture_get_datap(atlas);
	int stride = vita2d_texture_get_stride(atlas);
	for (int j = 0; j < h; j++) {
		uint8_t *d = base + (y + j) * stride + x * 4;
		for (int i = 0; i < w; i++) {
			d[i * 4 + 0] = 255; d[i * 4 + 1] = 255; d[i * 4 + 2] = 255;
			d[i * 4 + 3] = cov[j * cstride + i];
		}
	}
}

static void box_blur(uint8_t *img, uint8_t *tmp, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int sum = 0;
			for (int k = -1; k <= 1; k++) { int xx = x + k; if (xx >= 0 && xx < w) sum += img[y * w + xx]; }
			tmp[y * w + x] = sum / 3;
		}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int sum = 0;
			for (int k = -1; k <= 1; k++) { int yy = y + k; if (yy >= 0 && yy < h) sum += tmp[yy * w + x]; }
			img[y * w + x] = sum / 3;
		}
}

static Glyph *glyph_get(uint32_t cp, unsigned int size, int sub)
{
	uint32_t key = (size << 24) | ((uint32_t)sub << 21) | (cp & 0x1FFFFF);
	uint32_t h = (key * 2654435761u) >> 20;
	for (int probe = 0; probe < GLYPH_SLOTS; probe++) {
		Glyph *g = &glyphs[(h + probe) & (GLYPH_SLOTS - 1)];
		if (g->key == key) return g;
		if (g->key == 0) {
			if (atlas_dirty) return NULL;

			FT_Set_Pixel_Sizes(ft_face, 0, size);
			FT_UInt idx = (FT_UInt)cp;          /* glyph id from the shaper */
			if (idx == 0) {                      /* .notdef: draw nothing */
				g->key = key;
				g->aw = g->ah = g->sw = g->sh = 0;
				g->adv = 0;
				return g;
			}
			/* Light hinting snaps only vertically: even baseline/x-height, true stem widths. */
			{
				static const int flags[4] = {
					FT_LOAD_TARGET_LIGHT,
					FT_LOAD_NO_HINTING,
					FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT,
					FT_LOAD_NO_HINTING,
				};
				if (FT_Load_Glyph(ft_face, idx, flags[text_hint_mode & 3] | FT_LOAD_NO_BITMAP))
					return NULL;
			}
			/* shift the outline by sub/4 px so glyphs sit at fractional pen positions */
			if (sub && ft_face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
				FT_Outline_Translate(&ft_face->glyph->outline, sub * 16, 0);
			if (FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL))
				return NULL;
			FT_GlyphSlot sl = ft_face->glyph;
			FT_Bitmap *bm = &sl->bitmap;

			g->key = key;
			g->adv = sl->linearHoriAdvance / 65536.0f;
			g->aw = g->ah = g->sw = g->sh = 0;
			if (bm->width && bm->rows) {
				int w = bm->width, hh = bm->rows;
				int pw = w + SHADOW_PAD * 2, ph = hh + SHADOW_PAD * 2;
				int ox, oy, sx, sy;
				if (!atlas_alloc(w + GLYPH_PAD * 2, hh + GLYPH_PAD * 2, &ox, &oy) ||
				    !atlas_alloc(pw + GLYPH_PAD * 2, ph + GLYPH_PAD * 2, &sx, &sy)) {
					g->key = 0;
					atlas_dirty = 1;
					return NULL;
				}
				ox += GLYPH_PAD; oy += GLYPH_PAD; sx += GLYPH_PAD; sy += GLYPH_PAD;
				atlas_put(ox, oy, w, hh, bm->buffer, bm->pitch);

				/* shadow: padded copy, blurred twice, slightly dimmed */
				uint8_t *img = calloc(pw * ph, 1), *tmp = calloc(pw * ph, 1);
				if (img && tmp) {
					for (int j = 0; j < hh; j++)
						memcpy(img + (j + SHADOW_PAD) * pw + SHADOW_PAD, bm->buffer + j * bm->pitch, w);
					box_blur(img, tmp, pw, ph);
					box_blur(img, tmp, pw, ph);
					for (int i = 0; i < pw * ph; i++) img[i] = img[i] * 9 / 10;
					atlas_put(sx, sy, pw, ph, img, pw);
					g->sx = sx; g->sy = sy; g->sw = pw; g->sh = ph;
					g->sleft = sl->bitmap_left - SHADOW_PAD;
					g->stop = sl->bitmap_top + SHADOW_PAD;
				}
				free(img); free(tmp);
				g->ax = ox; g->ay = oy; g->aw = w; g->ah = hh;
				g->left = sl->bitmap_left; g->top = sl->bitmap_top;
			}
			return g;
		}
	}
	return NULL;
}

/* ---- shaping: glyph ids + advances with GPOS kerning ---- */

#define MAX_SHAPED 160
typedef struct { uint32_t gid; float adv; float xoff; } Shaped;

static int text_shape(unsigned int size, const char *s, Shaped *out)
{
	if (!hb_fnt || !*s) return 0;
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, s, -1, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_font_set_scale(hb_fnt, (int)size * 64, (int)size * 64);
	hb_shape(hb_fnt, buf, NULL, 0);
	unsigned int n = 0;
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &n);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &n);
	if (n > MAX_SHAPED) n = MAX_SHAPED;
	for (unsigned int i = 0; i < n; i++) {
		out[i].gid = info[i].codepoint;
		out[i].adv = pos[i].x_advance / 64.0f;
		out[i].xoff = pos[i].x_offset / 64.0f;
	}
	hb_buffer_destroy(buf);
	return (int)n;
}

static float text_width_f(unsigned int size, const char *s)
{
	if (!atlas) return 0;
	Shaped sh[MAX_SHAPED];
	int n = text_shape(size, s, sh);
	float w = 0;
	for (int i = 0; i < n; i++) w += sh[i].adv;
	return w;
}

/* y is the baseline. Shadow is drawn first, offset down-right like the PSP's. */
static void text_draw(float x, float y, unsigned int col, unsigned int size, const char *s)
{
	if (!atlas) return;
	Shaped sh[MAX_SHAPED];
	int n = text_shape(size, s, sh);
	unsigned int a = col >> 24;
	unsigned int shadow = RGBA8(0, 0, 0, a * 75 / 100);
	const float by = y;                 /* fractional on purpose: text glides with the icons */
	const int sdx = 2, sdy = 2;

	for (int pass = text_flat ? 1 : 0; pass < 2; pass++) {
		float pen = x;
		for (int i = 0; i < n; i++) {
			float px = pen + sh[i].xoff;
			float fl = floorf(px);
			int sub = (int)((px - fl) * 4.0f + 0.5f);
			int gx = (int)fl;
			if (sub == 4) { sub = 0; gx++; }
			Glyph *g = glyph_get(sh[i].gid, size, sub);
			if (g) {
				/* vita2d's rotate-style draw takes the CENTRE of the quad, hence +w/2, +h/2 */
				if (pass == 0 && g->sw)
					vita2d_draw_texture_part_tint_scale_rotate(atlas,
						gx + g->sleft + sdx + g->sw * 0.5f, by - g->stop + sdy + g->sh * 0.5f,
						g->sx, g->sy, g->sw, g->sh, 1.0f, 1.0f, 0.0f, shadow);
				else if (pass == 1 && g->aw)
					vita2d_draw_texture_part_tint_scale_rotate(atlas,
						gx + g->left + g->aw * 0.5f, by - g->top + g->ah * 0.5f,
						g->ax, g->ay, g->aw, g->ah, 1.0f, 1.0f, 0.0f, col);
			}
			pen += sh[i].adv;
		}
	}
}

static void ptext(float x, float y, unsigned int col, unsigned int size, const char *s)
{
	text_draw(x, y, col, size, s);
}

/* Left side bearing (px) of the first glyph, for ink-flush left alignment. */
static int text_bearing(unsigned int size, const char *s)
{
	if (!atlas || !*s) return 0;
	Shaped sh[MAX_SHAPED];
	if (text_shape(size, s, sh) < 1) return 0;
	Glyph *g = glyph_get(sh[0].gid, size, 0);
	return g ? g->left : 0;
}

static int ptext_width(unsigned int size, const char *s)
{
	return (int)(text_width_f(size, s) + 0.5f);
}

/* Draw with the text's visual centre (cap height) on cy. */
static void ptext_vc(float x, float cy, unsigned int col, unsigned int size, const char *s)
{
	ptext(x, cy + floorf(size * 0.391f + 0.5f), col, size, s);
}

/* Like ptext_vc, but shortens the string with "..." so it fits in maxw pixels. */
static void ptext_vc_fit(float x, float cy, unsigned int col, unsigned int size, const char *s, float maxw)
{
	if (ptext_width(size, s) <= maxw) { ptext_vc(x, cy, col, size, s); return; }
	char buf[100];
	int len = (int)strlen(s);
	if (len > 90) len = 90;
	memcpy(buf, s, len);
	buf[len] = 0;
	while (len > 1) {
		len--;
		while (len > 0 && (buf[len] & 0xC0) == 0x80) len--;      /* never cut inside a UTF-8 sequence */
		buf[len] = 0;
		char tmp[104];
		snprintf(tmp, sizeof(tmp), "%s...", buf);
		if (ptext_width(size, tmp) <= maxw) { ptext_vc(x, cy, col, size, tmp); return; }
	}
}

static void ptext_right_vc(float xr, float cy, unsigned int col, unsigned int size, const char *s)
{
	ptext_vc(xr - ptext_width(size, s), cy, col, size, s);
}

static void draw_icon_wh(vita2d_texture *t, float cx, float cy, float w, float h, int a)
{
	if (!t) return;
	float sx = w / vita2d_texture_get_width(t), sy = h / vita2d_texture_get_height(t);
	float x = cx - w / 2, y = cy - h / 2;
	static const struct { float dx, dy; int w; } sh[] = { { 3, 4, 22 }, { 5, 6, 12 }, { 1, 5, 12 } };
	for (int i = 0; i < 3; i++)
		vita2d_draw_texture_tint_scale(t, x + sh[i].dx, y + sh[i].dy, sx, sy,
		                               RGBA8(0, 0, 0, a * sh[i].w / 100));
	vita2d_draw_texture_tint_scale(t, x, y, sx, sy, RGBA8(255, 255, 255, a));
}

static void draw_icon(vita2d_texture *t, float cx, float cy, float size, int a)
{
	draw_icon_wh(t, cx, cy, size, size, a);
}

/* ------------------------------------------------------------------ */
/* Background                                                          */
/* ------------------------------------------------------------------ */

typedef struct { unsigned char sky_top[3], sky_bot[3], wave_top[3], wave_bot[3]; } Palette;

/* October is measured from a real PSP XMB capture; other months derive from the older table
 * (sky darker than the water, like the PSP themes) until their captures are available. */
static Palette get_palette(int month)
{
	Palette p;
	if (month == 9) {
		static const Palette oct = { { 255, 191, 91 }, { 255, 160, 48 }, { 255, 203, 64 }, { 255, 223, 73 } };
		return oct;
	}
	const unsigned char *m = month_cols[month];
	for (int i = 0; i < 3; i++) {
		int top = m[i], bot = m[3 + i];
		p.sky_top[i] = (unsigned char)(top * 0.80f);
		p.sky_bot[i] = (unsigned char)(top * 0.92f);
		p.wave_top[i] = (unsigned char)(top + (255 - top) * 0.10f);
		p.wave_bot[i] = (unsigned char)(top + (255 - top) * 0.28f);
		(void)bot;
	}
	return p;
}

/* One strip between two wave-shaped lines, vertex colours blended from c0 to c1. */
static float bg_alpha = 1.0f;
static unsigned int bga(unsigned int c)
{
	unsigned int a = (unsigned int)((c >> 24) * bg_alpha);
	return (c & 0x00FFFFFFu) | (a << 24);
}

static void wave_strip(float base0, float base1, float t, const float p[3], unsigned int c0, unsigned int c1)
{
	const int step = 24;
	int cols = SCREEN_W / step + 2;
	vita2d_color_vertex *v = vita2d_pool_memalign(cols * 2 * sizeof(*v), sizeof(*v));
	if (!v) return;
	for (int i = 0; i < cols; i++) {
		float x = (float)(i * step);
		float w = p[0] * sinf(x * p[1] + t * p[2]) + p[0] * 0.35f * sinf(x * p[1] * 2.3f + 1.7f - t * p[2] * 0.7f);
		v[i * 2].x = x;     v[i * 2].y = base0 + w;     v[i * 2].z = 0.5f; v[i * 2].color = bga(c0);
		v[i * 2 + 1].x = x; v[i * 2 + 1].y = base1 + w; v[i * 2 + 1].z = 0.5f; v[i * 2 + 1].color = bga(c1);
	}
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, cols * 2);
}

static void draw_background(float t, int month)
{
	Palette pal = get_palette(month);
	unsigned int ct = RGBA8(pal.sky_top[0], pal.sky_top[1], pal.sky_top[2], 255);
	unsigned int cb = RGBA8(pal.sky_bot[0], pal.sky_bot[1], pal.sky_bot[2], 255);
	vita2d_color_vertex *g = vita2d_pool_memalign(4 * sizeof(*g), sizeof(*g));
	if (g) {
		g[0] = (vita2d_color_vertex){ 0, 0, 0.5f, bga(ct) };
		g[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, bga(ct) };
		g[2] = (vita2d_color_vertex){ 0, 340, 0.5f, bga(cb) };
		g[3] = (vita2d_color_vertex){ SCREEN_W, 340, 0.5f, bga(cb) };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, g, 4);
		vita2d_draw_rectangle(0, 340, SCREEN_W, SCREEN_H - 340, bga(cb));
	}

	/* soft light patch in the top-right corner */
	vita2d_color_vertex *h = vita2d_pool_memalign(4 * sizeof(*h), sizeof(*h));
	if (h) {
		h[0] = (vita2d_color_vertex){ 420, 0, 0.5f, bga(WHITE(0)) };
		h[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, bga(WHITE(62)) };
		h[2] = (vita2d_color_vertex){ 420, 210, 0.5f, bga(WHITE(0)) };
		h[3] = (vita2d_color_vertex){ SCREEN_W, 210, 0.5f, bga(WHITE(0)) };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, h, 4);
	}

	unsigned int wt = RGBA8(pal.wave_top[0], pal.wave_top[1], pal.wave_top[2], 255);
	unsigned int wb = RGBA8(pal.wave_bot[0], pal.wave_bot[1], pal.wave_bot[2], 255);

	/* main wave: a solid body of "water" with a pale crest */
	static const float main_p[3] = { 24, 0.0046f, 0.20f };
	wave_strip(275, 400, t, main_p, wt, wb);
	wave_strip(400, SCREEN_H + 90, t, main_p, wb, wb);
	wave_strip(268, 276, t, main_p, WHITE(0), WHITE(78));
	wave_strip(276, 290, t, main_p, WHITE(78), WHITE(0));

	/* two faint ribbons deeper down */
	static const float p2[3] = { 20, 0.0061f, -0.16f };
	wave_strip(372, 384, t, p2, WHITE(0), WHITE(34));
	wave_strip(384, 430, t, p2, WHITE(34), WHITE(0));
	static const float p3[3] = { 16, 0.0079f, 0.24f };
	wave_strip(452, 460, t, p3, WHITE(0), WHITE(26));
	wave_strip(460, 500, t, p3, WHITE(26), WHITE(0));
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

/* Layout measured from PSP screenshots (480x272) at 2x. */
#define CAT_SPACING   160.0f
#define CAT_X         218.0f
#define CAT_Y         140.0f
#define ITEM_X        208.0f
#define ITEM_Y        270.0f   /* selected row centre */
#define ITEM_SPACING  130.0f
#define ITEM_ABOVE_GAP 120.0f  /* extra jump over the category row */
#define ITEM_TEXT_X   300.0f

/* Ask the system to start the title. launch_mode picks the variant, so the one that
 * starts a game without detouring through LiveArea can be found on the device:
 *   A  0xFFFFF, then exit  - what VitaShell and most launchers do
 *   B  0x20000, then exit  - "open" flag, used by vitacompanion
 *   C  0x40000, then exit  - "launch" flag, used by vita-launcher for system apps
 *   D  0xFFFFF twice, then stay open on a black screen until the system replaces us */
static void launch_request(const Item *it)
{
	static const int flags[4] = { 0xFFFFF, 0x20000, 0x40000, 0xFFFFF };
	char uri[64];
	snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", it->id);
	vs_shutdown();
	sceKernelDelayThread(10000);
	int r = sceAppMgrLaunchAppByUri(flags[launch_mode], uri);
	trace("launch_request %s flags=%x -> %08x\n", uri, flags[launch_mode], r);
	if (launch_mode == 3) {
		sceKernelDelayThread(10000);
		r = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
		trace("  second call -> %08x\n", r);
		return;
	}
	sceKernelDelayThread(1000);
	sceKernelExitProcess(0);
}

static void draw_status(const SceDateTime *dt)
{
	char buf[32];
	int pct = scePowerGetBatteryLifePercent();
	const float cy = 26.0f;                       /* shared centre line of clock and battery */

	/* PSP battery: outlined body, nub on the LEFT, up to three segments filling from the right */
	const float bx = 908, bw = 42, bh = 24, by = cy - bh / 2;
	unsigned int line = WHITE(235);
	vita2d_draw_rectangle(bx + 1, by + 2, bw, bh, RGBA8(0, 0, 0, 38));            /* soft shadow */
	vita2d_draw_rectangle(bx, by, bw, 2, line);
	vita2d_draw_rectangle(bx, by + bh - 2, bw, 2, line);
	vita2d_draw_rectangle(bx, by, 2, bh, line);
	vita2d_draw_rectangle(bx + bw - 2, by, 2, bh, line);
	vita2d_draw_rectangle(bx - 4, cy - 5, 4, 10, line);
	int segs = pct > 66 ? 3 : (pct > 33 ? 2 : (pct > 8 ? 1 : 0));
	for (int k = 0; k < segs; k++)
		vita2d_draw_rectangle(bx + bw - 6 - 8 * (k + 1) - 2 * k + 2, by + 5, 8, bh - 10, line);

	if (clock24) {
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d", dt->month, dt->day, dt->hour, dt->minute);
	} else {
		int h12 = dt->hour % 12 ? dt->hour % 12 : 12;
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d %s", dt->month, dt->day, h12, dt->minute, dt->hour < 12 ? "AM" : "PM");
	}
	ptext_right_vc(bx - 22, cy, WHITE(240), 24, buf);
}

static float ease_out(float t) { t = clampf(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t); }

typedef struct { vita2d_texture *src, *glow; } GlowEntry;
static GlowEntry glows[40];
static int glow_n;
#define GLOW_PAD 16

/* A soft white bloom shaped like the icon, as on the PSP's selected item. */
static vita2d_texture *glow_for(vita2d_texture *src)
{
	for (int i = 0; i < glow_n; i++) if (glows[i].src == src) return glows[i].glow;
	if (glow_n >= 40) return NULL;
	glows[glow_n].src = src;
	glows[glow_n].glow = NULL;
	int sw = vita2d_texture_get_width(src), sh = vita2d_texture_get_height(src);
	int w = sw + 2 * GLOW_PAD, h = sh + 2 * GLOW_PAD;
	uint8_t *a = calloc((size_t)w * h, 1), *tmp = calloc((size_t)w * h, 1);
	vita2d_texture *g = NULL;
	if (a && tmp) {
		const uint8_t *d = vita2d_texture_get_datap(src);
		int stride = vita2d_texture_get_stride(src);
		for (int y = 0; y < sh; y++)
			for (int x = 0; x < sw; x++) a[(y + GLOW_PAD) * w + x + GLOW_PAD] = d[y * stride + x * 4 + 3];
		for (int pass = 0; pass < 12; pass++) box_blur(a, tmp, w, h);
		g = vita2d_create_empty_texture(w, h);
		if (g) {
			uint8_t *o = vita2d_texture_get_datap(g);
			int ostride = vita2d_texture_get_stride(g);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					int v = a[y * w + x] * 3;               /* boost the faint blur into a visible halo */
					uint8_t *px = o + y * ostride + x * 4;
					px[0] = px[1] = px[2] = 255;
					px[3] = (uint8_t)(v > 255 ? 255 : v);
				}
			vita2d_texture_set_filters(g, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		}
	}
	free(a); free(tmp);
	glows[glow_n++].glow = g;
	return g;
}

static int is_game_folder(int m) { return m == M_MEMCARD || m == M_SAVES; }
static int menu_layout(int m) { return is_game_folder(m) ? LAY_GAME : (m >= M_VITAXMB && m != M_VIDEOS ? LAY_SUB : (m == M_VIDEOS ? LAY_SUB : LAY_COLUMN)); }
static int menu_owner(int m)
{
	if (m < CAT_COUNT) return m;
	if (m == M_VIDEOS) return CAT_VIDEO;
	if (m == M_TRACKS) return CAT_MUSIC;
	if (m == M_MEMCARD || m == M_SAVES) return CAT_GAME;
	return CAT_SETTINGS;
}

#define FOLDER_X 184.0f

/* y of the row at offset d from the selection, in the folder (game list) layout. */
static float folder_y(float d)
{
	static const float yy[] = { 20, 114, 272, 423, 519, 615, 711 };   /* d = -2 .. 4 */
	d = clampf(d, -2.0f, 3.99f);
	int i = (int)floorf(d + 2.0f);
	float f = d + 2.0f - i;
	return lerpf(yy[i], yy[i + 1], f);
}

/* Fits a texture inside a box, preserving aspect. */
static void fit_box(const vita2d_texture *t, float bw, float bh, float *w, float *h)
{
	float tw = vita2d_texture_get_width(t), th = vita2d_texture_get_height(t);
	float sc = fminf(bw / tw, bh / th);
	*w = tw * sc; *h = th * sc;
}

/* The PSP's game-folder view: big landscape icon on the selection, small ones stacked
 * above and below, the title left to the background art (pic_alpha fades the text). */
static void draw_folder_column(int m, float xoff, float amul, float pic_a)
{
	Menu *mn = &menus[m];
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -2.2f || d > 3.8f) continue;
		float y = folder_y(d);
		float t = clampf(fabsf(d), 0.0f, 1.0f);
		float bw = lerpf(288.0f, 162.0f, t), bh = lerpf(160.0f, 91.0f, t);
		int a = (int)(255 * amul);
		if (a <= 4) continue;

		const Item *it = &mn->items[j];
		float ix = FOLDER_X + xoff;
		vita2d_texture *tex = it->icon ? it->icon : it->stock;
		if (!tex) continue;
		float w, h;
		if (it->icon) fit_box(tex, bw, bh, &w, &h);
		else { w = h = fminf(bh, 84.0f); }           /* the 64px stock icons stay small */
		if (it->icon && !it->icon_rect) {            /* no landscape art: frame the square icon like one */
			vita2d_draw_rectangle(ix - bw / 2 + 3, y - bh / 2 + 4, bw, bh, RGBA8(0, 0, 0, a * 25 / 100));
			vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, bh, RGBA8(18, 28, 38, a * 80 / 100));
			vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, 2, WHITE(a * 35 / 100));
		}
		draw_icon_wh(tex, ix, y, w, h, a);

		if (t < 0.5f) {                               /* selected row: label unless art covers it */
			int ta = (int)(a * (1.0f - clampf(pic_a * 1.6f, 0.0f, 1.0f)) * (1.0f - t * 2.0f));
			if (ta > 4) {
				float tx = ix + bw / 2 + 24;
				float maxw = SCREEN_W - 24.0f - tx;
				if (it->sub[0]) {
					ptext_vc_fit(tx - text_bearing(28, it->title), y - 15, WHITE(ta), 28, it->title, maxw);
					ptext_vc_fit(tx - text_bearing(20, it->sub), y + 17, WHITE(ta * 7 / 10), 20, it->sub, maxw);
				} else {
					ptext_vc_fit(tx - text_bearing(28, it->title), y, WHITE(ta), 28, it->title, maxw);
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Settings-style sub lists, button glyphs, info pages, dialogs, panel */
/* ------------------------------------------------------------------ */

static float sub_y(float d)
{
	static const float yy[] = { -23, 67, 157, 270, 383, 473, 563, 653 };   /* d = -3 .. 4 */
	d = clampf(d, -3.0f, 3.99f);
	int i = (int)floorf(d + 3.0f);
	return lerpf(yy[i], yy[i + 1], d + 3.0f - i);
}

/* The 2px rule under a selected title: grey-white on the left, white on the right. */
static void draw_rule(float x0, float x1, float y, int la)
{
	if (la <= 2) return;
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (!v) return;
	unsigned int cl = RGBA8(208, 203, 192, la), cr = RGBA8(253, 247, 238, la);
	v[0] = (vita2d_color_vertex){ x0, y, 0.5f, cl };
	v[1] = (vita2d_color_vertex){ x1, y, 0.5f, cr };
	v[2] = (vita2d_color_vertex){ x0, y + 2, 0.5f, cl };
	v[3] = (vita2d_color_vertex){ x1, y + 2, 0.5f, cr };
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	vita2d_draw_rectangle(x0, y + 2, x1 - x0, 2, RGBA8(0, 0, 0, la * 12 / 100));
}

static void draw_glow(vita2d_texture *tex, float cx, float cy, float size, float strength)
{
	vita2d_texture *gl = tex ? glow_for(tex) : NULL;
	if (!gl) return;
	float sc = size / vita2d_texture_get_width(tex);
	float gw = vita2d_texture_get_width(gl) * sc, gh = vita2d_texture_get_height(gl) * sc;
	vita2d_draw_texture_tint_scale(gl, cx - gw / 2, cy - gh / 2, sc, sc, WHITE((int)(255 * strength)));
}

static void draw_track_row(const Item *it, int number, float y, float xoff, float sel, int a);

/* The child list of a Settings-style page: wrench badge, title, value at the right. */
static void draw_sub_list(int m, float xoff, float amul)
{
	if (amul <= 0.01f) return;
	Menu *mn = &menus[m];
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -3.2f || d > 4.2f) continue;
		float y = sub_y(d);
		const Item *it = &mn->items[j];
		float sel = it->glow;                     /* emphasis follows the selection, not the slide */
		float fade = d < 0 ? clampf(1.0f + d * 0.28f, 0.0f, 1.0f) : clampf(1.0f - d * 0.12f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.42f, 1.0f, sel));
		if (a <= 4) continue;

		if (m == M_TRACKS && it->kind == KIND_TRACK) { draw_track_row(it, j + 1, y, xoff, sel, a); continue; }
		float ix = 262.0f + xoff, tx = 303.0f + xoff;
		vita2d_texture *tex = (m == M_VIDEOS) ? it->stock : tex_badge;
		float isz = (m == M_VIDEOS) ? 84.0f : 64.0f;
		if (tex && it->glow > 0.02f) draw_glow(tex, ix, y, isz, it->glow * 0.95f * (a / 255.0f));
		draw_icon(tex, ix, y, isz, a);

		int two_line = it->sub[0] && it->kind != KIND_VALUE;
		float maxw = (it->kind == KIND_VALUE ? 745.0f : 940.0f) - 303.0f - 14.0f;
		if (two_line) {
			ptext_vc_fit(tx - text_bearing(28, it->title), y - 20, WHITE(a), 28, it->title, maxw);
			ptext_vc_fit(tx - text_bearing(22, it->sub), y + 22, WHITE(a), 22, it->sub, maxw);
			if (sel > 0.3f) draw_rule(tx - 1, 948, y, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		} else {
			ptext_vc_fit(tx - text_bearing(28, it->title), y + 3, WHITE(a), 28, it->title, maxw);
			if (it->kind == KIND_VALUE)
				ptext_vc(760.0f + xoff, y + 7, WHITE(a * 9 / 10), 24, it->sub);
			if (sel > 0.3f) draw_rule(tx - 1, 948, y + 22, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		}
	}
}

/* ---- PSP button glyphs, drawn from primitives (the font has none) ---- */
static void glyph_ring(float cx, float cy, float r, int a)
{
	for (int i = 0; i < 40; i++) {
		float ang = i * (6.2831853f / 40);
		vita2d_draw_fill_circle(cx + cosf(ang) * r, cy + sinf(ang) * r, 1.7f, WHITE(a));
	}
}
static void glyph_cross(float cx, float cy, float r, int a)
{
	for (int k = -1; k <= 1; k++) {
		vita2d_draw_line(cx - r, cy - r + k, cx + r, cy + r + k, WHITE(a));
		vita2d_draw_line(cx - r, cy + r + k, cx + r, cy - r + k, WHITE(a));
	}
}
static void glyph_triangle(float cx, float cy, float r, int a)
{
	float x0 = cx - r, x1 = cx + r, x2 = cx, y0 = cy + r * 0.8f, y2 = cy - r;
	for (int k = 0; k < 2; k++) {
		vita2d_draw_line(x0, y0 + k, x1, y0 + k, WHITE(a));
		vita2d_draw_line(x0, y0 + k, x2, y2 + k, WHITE(a));
		vita2d_draw_line(x1, y0 + k, x2, y2 + k, WHITE(a));
	}
}
static void glyph_arrow_left(float cx, float cy, float h, int a)
{
	for (int i = 0; i < (int)h; i++) {
		float half = (i < h / 2 ? i : h - i);
		vita2d_draw_rectangle(cx - half * 0.7f, cy - h / 2 + i, half * 0.7f + 1, 1.0f, WHITE(a));
	}
}

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
	if (sceKernelGetSystemSwVersion(&v) >= 0) info_add("System Software", "%s", v.versionString);
	else info_add("System Software", "Unknown");

	char nick[64] = "";
	if (sceRegMgrGetKeyStr("/CONFIG/SYSTEM", "username", nick, sizeof(nick)) < 0 || !nick[0]) snprintf(nick, sizeof(nick), "-");
	info_add("Nickname", "%s", nick);

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
		info_add("MAC Address", "%s", ms);
	} else {
		info_add("MAC Address", "Unavailable");
	}

	info_add("Model", "%s", sceKernelGetModelForCDialog() == 0x20000 ? "PlayStation TV" : "PlayStation Vita");

	uint64_t mx = 0, fr = 0;
	if (sceAppMgrGetDevInfo("ux0:", &mx, &fr) >= 0 && mx)
		info_add("Memory Card", "%.1f GB free of %.1f GB", fr / 1073741824.0, mx / 1073741824.0);
	if (extra_storage) {
		char devs[4][8];
		int nd = extra_devs(devs);
		for (int i = 0; i < nd; i++) {
			uint64_t emx = 0, efr = 0;
			char label[40];
			snprintf(label, sizeof(label), "Storage (%s)", devs[i]);
			if (dev_space(devs[i], &emx, &efr))
				info_add(label, "%.1f GB free of %.1f GB", efr / 1073741824.0, emx / 1073741824.0);
			else
				info_add(label, "mounted");
		}
	}

	int pct = scePowerGetBatteryLifePercent();
	info_add("Battery", "%d%%%s", pct < 0 ? 0 : pct, scePowerIsBatteryCharging() ? " (charging)" : "");
	info_add("Processor", "%d MHz", scePowerGetArmClockFrequency());
	info_add("VitaXMB", "1.1.0");
}

/* Details for a game or a saved-data entry. */
static void gameinfo_gather(const Item *it, int is_save)
{
	info_n = 0;
	char path[128], val[96];
	if (is_save) snprintf(path, sizeof(path), "ux0:user/00/savedata/%s/sce_sys/param.sfo", it->id);
	else snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", it->id);
	info_add("Title", "%s", it->title);
	info_add("Title ID", "%s", it->id);
	if (!is_save && sfo_get_string(path, "APP_VER", val, sizeof(val))) info_add("Version", "%s", val);
	if (!is_save && sfo_get_string(path, "CATEGORY", val, sizeof(val)))
		info_add("Category", "%s", !strcmp(val, "gd") ? "Game" : (!strcmp(val, "gda") ? "Game (application)" : val));
	if (is_save && it->sub[0]) info_add("Details", "%s", it->sub);
	info_add("Location", is_save ? "ux0:user/00/savedata/%s" : "ux0:app/%s", it->id);
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
	ptext_vc(552, 522, WHITE(a), 28, "Back");
}

/* ---- confirmation dialog ---- */
static void draw_dialog(float t, const char *l1, const char *l2, int sel)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(195 * t)));
	float cy = 200.0f;
	ptext_vc(480 - ptext_width(28, l1) / 2.0f, cy, WHITE(a), 28, l1);
	if (l2 && l2[0]) ptext_vc(480 - ptext_width(24, l2) / 2.0f, cy + 40, WHITE(a * 8 / 10), 24, l2);
	static const char *opts[2] = { "Yes", "No" };
	for (int i = 0; i < 2; i++) {
		float y = 330.0f + i * 56.0f;
		int oa = i == sel ? a : a * 5 / 10;
		float w = ptext_width(28, opts[i]);
		if (i == sel) {
			vita2d_draw_rectangle(480 - 90, y - 22, 180, 44, RGBA8(255, 255, 255, a * 18 / 100));
			draw_rule(480 - 90, 480 + 90, y + 18, a);
		}
		ptext_vc(480 - w / 2.0f, y, WHITE(oa), 28, opts[i]);
	}
	glyph_cross(405, 508, 8, a);
	ptext_vc(424, 508, WHITE(a), 24, "OK");
	glyph_ring(515, 508, 9, a);
	ptext_vc(534, 508, WHITE(a), 24, "Cancel");
}

/* ---- Options panel (triangle) ---- */
enum { OPT_START, OPT_INFO, OPT_REFRESH };
static const char *opt_names[3] = { "Start", "Information", "Refresh List" };

static void draw_options_panel(float t, const int *ids, int n, int sel, const Palette *pal)
{
	if (t <= 0.01f) return;
	float e = ease_out(t);
	float px = 640.0f + (1.0f - e) * 330.0f;
	int a = (int)(255 * e);
	unsigned int ct = RGBA8(pal->sky_top[0] * 7 / 10, pal->sky_top[1] * 6 / 10, pal->sky_top[2] * 4 / 10, a * 90 / 100);
	unsigned int cb = RGBA8(pal->sky_top[0] * 5 / 10, pal->sky_top[1] * 4 / 10, pal->sky_top[2] * 3 / 10, a * 92 / 100);
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (v) {
		v[0] = (vita2d_color_vertex){ px, 0, 0.5f, ct };
		v[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, ct };
		v[2] = (vita2d_color_vertex){ px, SCREEN_H, 0.5f, cb };
		v[3] = (vita2d_color_vertex){ SCREEN_W, SCREEN_H, 0.5f, cb };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	}
	vita2d_draw_rectangle(px, 0, 2, SCREEN_H, RGBA8(255, 255, 255, a * 30 / 100));
	float y0 = 353.0f - (n - 1) * 20.0f;
	for (int i = 0; i < n; i++) {
		float y = y0 + i * 40.0f;
		if (i == sel) vita2d_draw_rectangle(px + 3, y - 19, SCREEN_W - px - 3, 38, RGBA8(255, 255, 255, a * 20 / 100));
		ptext_vc(px + 11, y, WHITE(i == sel ? a : a * 78 / 100), 28, opt_names[ids[i]]);
		if (ids[i] == OPT_START && i == sel) {
			float bx = px + 11 + ptext_width(28, "Start") + 12;
			vita2d_draw_rectangle(bx, y - 11, 66, 22, RGBA8(0, 0, 0, a * 55 / 100));
			ptext_vc(bx + 6, y, WHITE(a), 17, "START");
		}
	}
}

/* The "Options" pill at the bottom right: a single rounded shape (row by row, so the translucent
 * fill never double-darkens where caps and body would overlap). */
static void draw_options_pill(int a)
{
	if (a <= 3) return;
	const float x = 784, y = 448, w = 164, h = 38, r = h / 2;
	unsigned int bg = RGBA8(16, 16, 20, a * 52 / 100);
	for (int row = 0; row < (int)h; row++) {
		float dy = row + 0.5f - r;
		float inset = r - sqrtf(fmaxf(0.0f, r * r - dy * dy));
		vita2d_draw_rectangle(x + inset, y + row, w - 2 * inset, 1.0f, bg);
	}
	glyph_triangle(x + 22, y + h / 2, 8, a);
	ptext_vc(x + 42, y + h / 2, WHITE(a), 24, "Options");
}

/* ------------------------------------------------------------------ */
/* Music: MP3 decoder thread -> PCM ring -> audio mixer, plus the UI   */
/* ------------------------------------------------------------------ */

enum { MC_NONE, MC_LOAD, MC_SEEK, MC_STOP };
static volatile int mus_cmd;
static char mus_req_path[128];
static volatile int mus_req_seek_ms;
static volatile int mus_total_ms;
static volatile int mus_ready;          /* a file is open and being played/paused */
static volatile int mus_eof;            /* decoder reached the end of the file */
static volatile unsigned mus_base_ms;   /* where playback started (after a seek) */
static volatile int mus_run = 1;
static int music_index = -1;            /* index of the playing row in menus[M_TRACKS] */

static int music_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	trace("music thread running\n");
	static mp3dec_ex_t dec;
	static int16_t pcm[2304 * 2];
	uint8_t *file = NULL;
	int open = 0, hz = 44100, ch = 2;
	double rpos = 0;
	int16_t prev_l = 0, prev_r = 0;

	while (mus_run) {
		int cmd = mus_cmd;
		if (cmd != MC_NONE) {
			mus_cmd = MC_NONE;
			if (cmd == MC_LOAD || cmd == MC_STOP) {
				mus_playing = 0;
				sceKernelDelayThread(50000);                  /* let the mixer finish its current grain */
				if (open) { mp3dec_ex_close(&dec); open = 0; }
				free(file); file = NULL;
				mus_w = mus_r = 0; mus_consumed = 0; mus_ready = 0; mus_eof = 0; mus_total_ms = 0; mus_base_ms = 0;
			}
			if (cmd == MC_LOAD) {
				SceUID fd = sceIoOpen(mus_req_path, SCE_O_RDONLY, 0);
				trace("music load %s -> fd %08x\n", mus_req_path, fd);
				if (fd >= 0) {
					int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
					sceIoLseek32(fd, 0, SCE_SEEK_SET);
					file = size > 0 ? malloc(size) : NULL;
					if (file && sceIoRead(fd, file, size) == size &&
					    mp3dec_ex_open_buf(&dec, file, size, MP3D_SEEK_TO_SAMPLE) == 0) {
						open = 1;
						hz = dec.info.hz ? dec.info.hz : 44100;
						ch = dec.info.channels ? dec.info.channels : 2;
						mus_total_ms = (int)(dec.samples / ch * 1000ULL / hz);
						trace("mp3 opened: %d ms, %d Hz, %d ch\n", (int)mus_total_ms, hz, ch);
						rpos = 0; prev_l = prev_r = 0;
						mus_ready = 1;
						mus_playing = 1;
					} else {
						trace("mp3 open FAILED (alloc %p size %d)\n", (void *)file, size);
						xlog("mp3 open failed %s\n", mus_req_path);
						free(file); file = NULL;
						mus_eof = 1;
					}
					sceIoClose(fd);
				} else {
					mus_eof = 1;
				}
			} else if (cmd == MC_SEEK && open) {
				int was = mus_playing;
				mus_playing = 0;
				sceKernelDelayThread(50000);
				mus_w = mus_r = 0; mus_consumed = 0; mus_eof = 0;
				int ms = mus_req_seek_ms;
				if (ms < 0) ms = 0;
				if (ms > mus_total_ms - 200) ms = mus_total_ms > 200 ? mus_total_ms - 200 : 0;
				mp3dec_ex_seek(&dec, (uint64_t)ms * hz / 1000 * ch);
				mus_base_ms = (unsigned)ms;
				rpos = 0;
				mus_playing = was;
			}
		}

		if (open && !mus_eof && (MUS_RING - (mus_w - mus_r)) > 4096) {
			size_t n = mp3dec_ex_read(&dec, pcm, 2304);
			if (n == 0) { mus_eof = 1; continue; }
			int nf = (int)(n / ch);
			/* to 48 kHz stereo, linear interpolation */
			double step = (double)hz / 48000.0;
			int guard = 0;
			for (;;) {
				int i0 = (int)floor(rpos);
				if (i0 + 1 >= nf || guard++ > 4000) break;
				double f = rpos - i0;
				int l0, r0, l1, r1;
				if (i0 < 0) { l0 = prev_l; r0 = prev_r; }
				else { l0 = pcm[i0 * ch]; r0 = pcm[i0 * ch + (ch > 1 ? 1 : 0)]; }
				l1 = pcm[(i0 + 1) * ch]; r1 = pcm[(i0 + 1) * ch + (ch > 1 ? 1 : 0)];
				unsigned w = mus_w & (MUS_RING - 1);
				mus_ring[w * 2]     = (int16_t)(l0 + (l1 - l0) * f);
				mus_ring[w * 2 + 1] = (int16_t)(r0 + (r1 - r0) * f);
				mus_w++;
				rpos += step;
			}
			rpos -= nf;
			prev_l = pcm[(nf - 1) * ch]; prev_r = pcm[(nf - 1) * ch + (ch > 1 ? 1 : 0)];
		} else {
			sceKernelDelayThread(6000);
		}
	}
	return 0;
}

static unsigned music_elapsed_ms(void)
{
	return mus_base_ms + (unsigned)(mus_consumed * 1000ULL / 48000ULL);
}

static void music_start(int index)
{
	trace("music_start(%d) count=%d\n", index, menus[M_TRACKS].count);
	if (index < 0 || index >= menus[M_TRACKS].count) return;
	music_index = index;
	snprintf(mus_req_path, sizeof(mus_req_path), "%s", menus[M_TRACKS].items[index].path);
	mus_cmd = MC_LOAD;
}
static void music_stop(void) { mus_cmd = MC_STOP; music_index = -1; }

/* Re-reads the music and video lists (the Extra Storage setting changed). */
static void rescan_media(void)
{
	if (mus_ready) music_stop();
	scan_videos();
	scan_music_wrapper();
	menus[M_VIDEOS].pos = (float)menus[M_VIDEOS].sel;
	menus[M_TRACKS].pos = (float)menus[M_TRACKS].sel;
}
static void music_toggle(void) { if (mus_ready) mus_playing = !mus_playing; }
static void music_seek_rel(int delta_ms)
{
	if (!mus_ready) return;
	int target = (int)music_elapsed_ms() + delta_ms;
	mus_req_seek_ms = target;
	mus_cmd = MC_SEEK;
}

/* ---- ID3 tags ---- */
static void id3_text(const uint8_t *d, int len, char *out, size_t cap)
{
	out[0] = 0;
	if (len < 2) return;
	int enc = d[0];
	const uint8_t *p = d + 1;
	int n = len - 1;
	size_t o = 0;
	if (enc == 0 || enc == 3) {                       /* Latin-1 / UTF-8 */
		for (int i = 0; i < n && o + 4 < cap && p[i]; i++) {
			if (enc == 0 && p[i] >= 0x80) { out[o++] = (char)(0xC0 | (p[i] >> 6)); out[o++] = (char)(0x80 | (p[i] & 0x3F)); }
			else out[o++] = (char)p[i];
		}
	} else {                                           /* UTF-16 (BOM or BE) */
		int le = 1, i = 0;
		if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { le = 0; i = 2; }
		else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { le = 1; i = 2; }
		else if (enc == 2) le = 0;
		for (; i + 1 < n && o + 4 < cap; i += 2) {
			unsigned cp = le ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1]);
			if (!cp) break;
			if (cp < 0x80) out[o++] = (char)cp;
			else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
			else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
		}
	}
	out[o] = 0;
}

static void id3_read(const char *path, char *title, size_t tn, char *artist, size_t an)
{
	title[0] = artist[0] = 0;
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	uint8_t h[10];
	if (sceIoRead(fd, h, 10) == 10 && !memcmp(h, "ID3", 3)) {
		int ver = h[3];
		uint32_t size = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
		if (size > 0 && size < 400 * 1024) {
			uint8_t *tag = malloc(size);
			if (tag && sceIoRead(fd, tag, size) == (int)size) {
				uint32_t pos = 0;
				while (pos + 10 < size) {
					const uint8_t *fh = tag + pos;
					if (!fh[0]) break;
					uint32_t fs = ver == 4 ? ((fh[4] << 21) | (fh[5] << 14) | (fh[6] << 7) | fh[7])
					                       : ((fh[4] << 24) | (fh[5] << 16) | (fh[6] << 8) | fh[7]);
					if (fs == 0 || pos + 10 + fs > size) break;
					if (!memcmp(fh, "TIT2", 4)) id3_text(fh + 10, (int)fs, title, tn);
					else if (!memcmp(fh, "TPE1", 4)) id3_text(fh + 10, (int)fs, artist, an);
					pos += 10 + fs;
				}
			}
			free(tag);
		}
	}
	if (!title[0]) {                                  /* ID3v1 fallback */
		int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
		if (size > 128) {
			sceIoLseek32(fd, size - 128, SCE_SEEK_SET);
			uint8_t t[128];
			if (sceIoRead(fd, t, 128) == 128 && !memcmp(t, "TAG", 3)) {
				snprintf(title, tn, "%.30s", (char *)t + 3);
				snprintf(artist, an, "%.30s", (char *)t + 33);
			}
		}
	}
	sceIoClose(fd);
}

static void scan_music_dir(const char *dir, int depth)
{
	SceUID d = sceIoDopen(dir);
	trace("music scan %s -> %08x\n", dir, d);
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0 && menus[M_TRACKS].count < MAX_ITEMS) {
		trace("  entry %s mode=%x\n", e.d_name, (unsigned)e.d_stat.st_mode);
		char full[112];
		snprintf(full, sizeof(full), "%s/%s", dir, e.d_name);
		if (SCE_S_ISDIR(e.d_stat.st_mode)) {
			if (depth < 3) scan_music_dir(full, depth + 1);
		} else {
			const char *dot = strrchr(e.d_name, '.');
			int dup = 0;
			for (int i = 0; i < menus[M_TRACKS].count; i++)
				if (!strcasecmp(menus[M_TRACKS].items[i].path, full)) { dup = 1; break; }
			if (dot && !strcasecmp(dot, ".mp3") && !dup) {
				char title[64], artist[48];
				id3_read(full, title, sizeof(title), artist, sizeof(artist));
				if (!title[0]) snprintf(title, sizeof(title), "%.*s", (int)(dot - e.d_name) > 60 ? 60 : (int)(dot - e.d_name), e.d_name);
				Item *it = add_item(M_TRACKS, KIND_TRACK, title, artist, NULL, tex_music_s);
				if (it) snprintf(it->path, sizeof(it->path), "%s", full);
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

static void scan_music(void)
{
	clear_menu(M_TRACKS);
	scan_music_dir("ux0:music", 0);
	scan_music_dir("ux0:pspemu/MUSIC", 0);
	if (extra_storage) {
		static const char *suffix[2] = { "music", "pspemu/MUSIC" };
		char devs[4][8];
		int n = extra_devs(devs);
		for (int i = 0; i < n; i++)
			for (int k = 0; k < 2; k++) {
				char path[32];
				snprintf(path, sizeof(path), "%s%s", devs[i], suffix[k]);
				scan_music_dir(path, 0);
			}
	}
	qsort(menus[M_TRACKS].items, menus[M_TRACKS].count, sizeof(Item), item_cmp);
	if (menus[M_TRACKS].count == 0) add_item(M_TRACKS, KIND_INFO, "No songs", "Put MP3 files in ux0:music", NULL, tex_music_s);
	if (menus[M_TRACKS].sel >= menus[M_TRACKS].count) menus[M_TRACKS].sel = 0;
	music_index = -1;
}

/* ---- spectrum: 12 bands via Goertzel over the last 1024 played samples ---- */
#define LED_COLS 12
#define LED_ROWS 8
static float led_level[LED_COLS];

static void spectrum_update(int playing)
{
	static const float centers[LED_COLS] = { 60, 110, 190, 330, 520, 800, 1200, 1800, 2800, 4300, 6500, 10000 };
	static float win_tab[VIS_N];
	static int win_ready;
	if (!win_ready) { for (int i = 0; i < VIS_N; i++) win_tab[i] = 0.5f - 0.5f * cosf(6.2831853f * i / (VIS_N - 1)); win_ready = 1; }
	int16_t snap[VIS_N];
	unsigned w = vis_w;
	for (int i = 0; i < VIS_N; i++) snap[i] = vis_buf[(w + i) % VIS_N];
	for (int b = 0; b < LED_COLS; b++) {
		float target = 0.0f;
		if (playing) {
			float coeff = 2.0f * cosf(6.2831853f * centers[b] / 48000.0f);
			float s0, s1 = 0, s2 = 0;
			for (int i = 0; i < VIS_N; i++) {
				s0 = snap[i] * win_tab[i] + coeff * s1 - s2;
				s2 = s1; s1 = s0;
			}
			float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
			float mag = sqrtf(p > 0 ? p : 0) / (VIS_N * 0.25f * 32768.0f);
			float db = 20.0f * log10f(mag + 1e-7f) + b * 2.4f;            /* tilt: highs carry less energy */
			target = clampf((db + 62.0f) / 56.0f * LED_ROWS, 0.0f, (float)LED_ROWS);
		}
		led_level[b] = target > led_level[b] ? lerpf(led_level[b], target, 0.55f) : fmaxf(target, led_level[b] - 0.22f);
	}
}

/* A soft blurred bar, tinted per row, gives each lit LED the PSP's diffuse bloom. */
static vita2d_texture *led_glow_tex(void)
{
	static vita2d_texture *tex;
	static int tried;
	if (tex || tried) return tex;
	tried = 1;
	const int w = 64, h = 48;
	uint8_t *a = calloc((size_t)w * h, 1), *tmp = calloc((size_t)w * h, 1);
	if (a && tmp) {
		for (int y = 18; y < 30; y++)
			for (int x = 22; x < 42; x++) a[y * w + x] = 255;
		for (int pass = 0; pass < 10; pass++) box_blur(a, tmp, w, h);
		tex = vita2d_create_empty_texture(w, h);
		if (tex) {
			uint8_t *o = vita2d_texture_get_datap(tex);
			int stride = vita2d_texture_get_stride(tex);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					int v = a[y * w + x] * 4;
					uint8_t *px = o + y * stride + x * 4;
					px[0] = px[1] = px[2] = 255;
					px[3] = (uint8_t)(v > 255 ? 255 : v);
				}
			vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		}
	}
	free(a); free(tmp);
	return tex;
}

static void draw_led(float cx, float cy, int row, int lit_amount_255, int page_a)
{
	/* row 0 = bottom. bottom four cyan, next three amber, top red */
	int r = row < 4 ? 60 : 255;
	int g = row < 4 ? 232 : (row < 7 ? 178 : 40);
	int b = row < 4 ? 252 : (row < 7 ? 52 : 52);
	int a = lit_amount_255 * page_a / 255;
	vita2d_draw_rectangle(cx - 10, cy - 6, 20, 12, RGBA8(r / 12, g / 12, b / 12, page_a));  /* unlit pad */
	if (a <= 4) return;
	vita2d_texture *gl = led_glow_tex();
	if (gl) vita2d_draw_texture_tint_scale(gl, cx - 32, cy - 24, 1.0f, 1.0f, RGBA8(r, g, b, a * 85 / 100));
	vita2d_draw_rectangle(cx - 10, cy - 6, 20, 12, RGBA8(r, g, b, a));
	vita2d_draw_rectangle(cx - 8, cy - 4, 16, 8, RGBA8(255, 255, 255, a * 50 / 100));       /* hot core */
}

static void fmt_time(unsigned ms, char *out)
{
	unsigned s = ms / 1000;
	snprintf(out, 12, "%02u:%02u", s / 60, s % 60);
}

static void draw_mp3_pill(float x, float y, int a)
{
	const float w = 102, h = 22;
	unsigned int c = RGBA8(255, 255, 255, a);
	vita2d_draw_rectangle(x + 5, y, w - 10, h, c);
	vita2d_draw_rectangle(x, y + 5, w, h - 10, c);
	vita2d_draw_fill_circle(x + 5, y + 5, 5, c);
	vita2d_draw_fill_circle(x + w - 5, y + 5, 5, c);
	vita2d_draw_fill_circle(x + 5, y + h - 5, 5, c);
	vita2d_draw_fill_circle(x + w - 5, y + h - 5, 5, c);
	ptext_vc(x + w / 2 - ptext_width(20, "MP3") / 2.0f, y + h / 2, RGBA8(20, 20, 20, a), 20, "MP3");
	for (int i = 0; i < 2; i++)                              /* little speaker arcs */
		for (int k = -4 - i; k <= 4 + i; k++)
			vita2d_draw_rectangle(x + w + 4 + i * 4 + (k * k) / (6 + i * 4), y + h / 2 + k * 1.6f, 1.6f, 1.6f, WHITE(a));
}

/* One row of the track list (sub-list layout): number badge, title/rule/artist, MP3 pill. */
static void draw_track_row(const Item *it, int number, float y, float xoff, float sel, int a)
{
	float bx = 272.0f + xoff, tx = 343.0f + xoff;
	unsigned int line = WHITE(a);
	/* rounded-square number badge */
	vita2d_draw_rectangle(bx - 25, y - 25, 50, 3, line);
	vita2d_draw_rectangle(bx - 25, y + 22, 50, 3, line);
	vita2d_draw_rectangle(bx - 25, y - 25, 3, 50, line);
	vita2d_draw_rectangle(bx + 22, y - 25, 3, 50, line);
	if (sel > 0.5f) vita2d_draw_rectangle(bx - 22, y - 22, 44, 44, RGBA8(255, 255, 255, a * 18 / 100));
	char num[8];
	snprintf(num, sizeof(num), "%d", number);
	ptext_vc(bx - ptext_width(30, num) / 2.0f, y, WHITE(a), 30, num);

	float maxw = 820.0f - tx;
	ptext_vc_fit(tx - text_bearing(28, it->title), y - 20, WHITE(a), 28, it->title, maxw);
	if (it->sub[0]) ptext_vc_fit(tx - text_bearing(22, it->sub), y + 22, WHITE(a), 22, it->sub, maxw);
	if (sel > 0.3f) {
		draw_rule(tx - 1, 948, y, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		draw_mp3_pill(828, y + 11, (int)(a * sel));
	}
}

/* The full music player screen (black, LED spectrum, progress). */
static void draw_player(float alpha, int closing)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, a));
	if (closing) return;           /* leaving: just the backdrop dissolves, no ghost of the LED grid over the XMB */
	/* header bar */
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (v) {
		unsigned int c0 = RGBA8(83, 50, 0, a), c1 = RGBA8(92, 55, 0, a);
		v[0] = (vita2d_color_vertex){ 0, 0, 0.5f, c0 };       v[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, c0 };
		v[2] = (vita2d_color_vertex){ 0, 44, 0.5f, c1 };      v[3] = (vita2d_color_vertex){ SCREEN_W, 44, 0.5f, c1 };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	}
	draw_icon_wh(cat_tex[CAT_MUSIC], 30, 22, 34, 34, a);
	ptext_vc(56, 22, WHITE(a), 28, "-");
	char cnt[24];
	int total = menus[M_TRACKS].count;
	snprintf(cnt, sizeof(cnt), "(%d/%d)", music_index + 1, total);
	ptext_right_vc(932, 22, WHITE(a), 28, cnt);

	if (music_index >= 0 && music_index < total) {
		const Item *it = &menus[M_TRACKS].items[music_index];
		/* number badge */
		unsigned int line = WHITE(a);
		vita2d_draw_rectangle(42, 80, 64, 3, line);  vita2d_draw_rectangle(42, 141, 64, 3, line);
		vita2d_draw_rectangle(42, 80, 3, 64, line);  vita2d_draw_rectangle(103, 80, 3, 64, line);
		char num[8];
		snprintf(num, sizeof(num), "%d", music_index + 1);
		ptext_vc(74 - ptext_width(34, num) / 2.0f, 112, WHITE(a), 34, num);
		ptext_vc_fit(150 - text_bearing(30, it->title), 91, WHITE(a), 30, it->title, 790.0f);
		draw_rule(150, 948, 106, a);
		if (it->sub[0]) ptext_vc_fit(150 - text_bearing(24, it->sub), 133, WHITE(a), 24, it->sub, 640.0f);
		draw_mp3_pill(828, 119, a);
	}

	/* LED spectrum */
	for (int c = 0; c < LED_COLS; c++)
		for (int r = 0; r < LED_ROWS; r++) {
			float lit = clampf(led_level[c] - r, 0.0f, 1.0f);
			draw_led(73.5f + c * 74.0f, 454.5f - r * 42.0f, r, (int)(255 * lit), a);
		}

	/* time + progress */
	char e[12], tt[12], tail[24];
	fmt_time(music_elapsed_ms(), e);
	fmt_time((unsigned)mus_total_ms, tt);
	snprintf(tail, sizeof(tail), " / %s", tt);
	float tw = ptext_width(34, tail);
	ptext_right_vc(940 - tw, 494, RGBA8(40, 100, 255, a), 34, e);
	ptext_right_vc(940, 494, WHITE(a), 34, tail);
	float frac = mus_total_ms > 0 ? clampf((float)music_elapsed_ms() / mus_total_ms, 0.0f, 1.0f) : 0.0f;
	vita2d_draw_rectangle(482, 516, 466, 8, RGBA8(222, 222, 222, a));
	vita2d_draw_rectangle(482, 516, 466 * frac < 6 ? 6 : 466 * frac, 8, RGBA8(35, 85, 235, a));

	/* play / pause glyph */
	if (mus_playing) {
		for (int i = 0; i < 28; i++) {
			float half = (i < 14 ? i : 28 - i);
			vita2d_draw_rectangle(28, 506 + i, half * 3.0f + 1, 1, WHITE(a));
		}
	} else {
		vita2d_draw_rectangle(30, 506, 9, 28, WHITE(a));
		vita2d_draw_rectangle(47, 506, 9, 28, WHITE(a));
	}
}

/* Draws one vertical list. xoff slides it sideways, amul fades it, sub_p (0..1) shrinks it into the
 * icon-only "parent" column shown to the left of a Settings-style sub list. */
static void draw_column(int m, float xoff, float amul, float grow, float sub_p)
{
	if (amul <= 0.01f) return;
	if (is_game_folder(m)) { draw_folder_column(m, xoff, amul, grow); return; }
	Menu *mn = &menus[m];
	float sp = ease_out(sub_p);
	/* the Game list (Saved Data / Memory Stick) uses a slightly tighter text column than Settings */
	const float base_icon_x = m == CAT_SETTINGS ? 207.0f : 218.0f;      /* Settings icons carry a wrench badge that hangs left */
	const float text_x = m == CAT_SETTINGS ? 300.0f : 283.0f;
	const float icon_x = lerpf(base_icon_x, 100.0f, sp);
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -1.6f || d > 3.0f) continue;

		float spacing = lerpf(ITEM_SPACING, 115.0f, sp);
		float y = ITEM_Y + d * spacing;
		if (d < 0) y -= ITEM_ABOVE_GAP * (1.0f - sp) * clampf(-d, 0.0f, 1.0f);  /* hop over the category row */
		float sel = 1.0f - clampf(fabsf(d), 0.0f, 1.0f);              /* position-based: icon size only */
		const Item *it = &mn->items[j];
		float emph = it->glow;                                         /* selection-based: brightness, rule, glow */
		float fade = d < 0 ? clampf(1.0f + d * 0.6f, 0.0f, 1.0f) * (1.0f - sp)
		                    : clampf(1.0f - d * 0.1f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.42f, 1.0f, emph));
		if (a <= 4) continue;

		float ix = icon_x + xoff;
		if (it->icon && it->icon_rect) {
			/* LiveArea gate image: a landscape rectangle like the PSP's ICON0 */
			float w = 150.0f + 30.0f * grow * sel;
			float h = w * vita2d_texture_get_height(it->icon) / vita2d_texture_get_width(it->icon);
			draw_icon_wh(it->icon, ix, y, w, h, a);
		} else {
			float isz = (it->icon ? 72.0f : lerpf(112.0f, 120.0f, sel)) + 36.0f * grow * sel;
			isz = lerpf(isz, it->icon ? 72.0f : lerpf(84.0f, 92.0f, sel), sp);
			vita2d_texture *tex = it->icon ? it->icon : it->stock;
			if (tex && !it->icon && it->glow > 0.02f) draw_glow(tex, ix, y, isz, it->glow * 0.95f * (a / 255.0f));
			draw_icon(tex, ix, y, isz, a);
		}

		/* title above / subtitle below a thin rule; ink edges flush at the text column */
		int ta = (int)(a * (1.0f - sp));
		if (ta <= 4) continue;
		float tx = text_x + xoff;
		float maxw = SCREEN_W - 24.0f - text_x;
		if (it->sub[0]) {
			ptext_vc_fit(tx - text_bearing(28, it->title), y - 20, WHITE(ta), 28, it->title, maxw);
			ptext_vc_fit(tx - text_bearing(22, it->sub), y + 22, WHITE(ta), 22, it->sub, maxw);
			if (emph > 0.3f) draw_rule(tx - 1, 950, y, (int)(ta * clampf(emph * 1.4f - 0.2f, 0.0f, 1.0f)));
		} else {
			ptext_vc_fit(tx - text_bearing(28, it->title), y, WHITE(ta), 28, it->title, maxw);
		}
	}
}

/* Glyph test page for tools/check_font.py: 32 cells per row (30 px wide), 3 rows per size. */
static const unsigned int test_sizes[4] = { 20, 22, 24, 28 };

static void draw_text_grid(void)
{
	for (int si = 0; si < 4; si++)
		for (int c = 0; c < 94; c++) {
			int row = si * 3 + c / 32, col = c % 32;
			char buf[2] = { (char)(33 + c), 0 };
			ptext(6 + col * 30, 40 + row * 44, WHITE(255), test_sizes[si], buf);
		}
}

/* ---- debug remote: ux0:data/VitaXMB/remote.txt holds space-separated commands, e.g.
 *   "right right cross w30 shot:folder"   (w<N> waits N frames; shot:<name> writes
 *   ux0:data/VitaXMB/<name>.png). The file is deleted once read. Used with tools/remote.py. */

#define REMOTE_FILE CONFIG_DIR "/remote.txt"
static int test_page;
static char rc_cmd[64][100];
static int  rc_n, rc_i, rc_wait;
static char rc_shot[48];
static char rc_uri[128];

static void remote_poll(void)
{
	SceUID fd = sceIoOpen(REMOTE_FILE, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	char buf[2048];
	int n = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	sceIoRemove(REMOTE_FILE);
	if (n <= 0) return;
	buf[n] = 0;
	rc_n = rc_i = 0;
	for (char *tok = strtok(buf, " \r\n\t,"); tok && rc_n < 64; tok = strtok(NULL, " \r\n\t,"))
		snprintf(rc_cmd[rc_n++], sizeof(rc_cmd[0]), "%s", tok);
}

/* One command per frame; returns the synthetic "pressed" mask. */
static unsigned int remote_step(void)
{
#ifndef VITAXMB_DEBUG
	return 0;
#endif
	static const struct { const char *name; unsigned int btn; } map[] = {
		{ "left", SCE_CTRL_LEFT }, { "right", SCE_CTRL_RIGHT }, { "up", SCE_CTRL_UP }, { "down", SCE_CTRL_DOWN },
		{ "cross", SCE_CTRL_CROSS }, { "circle", SCE_CTRL_CIRCLE }, { "triangle", SCE_CTRL_TRIANGLE },
		{ "l", SCE_CTRL_LTRIGGER }, { "r", SCE_CTRL_RTRIGGER },
	};
	static int poll_div;
	if (rc_i >= rc_n && (++poll_div % 20) == 0) remote_poll();
	if (rc_wait > 0) { rc_wait--; return 0; }
	if (rc_i >= rc_n) return 0;
	const char *c = rc_cmd[rc_i++];
	if (c[0] == 'w' && c[1] >= '0' && c[1] <= '9') { rc_wait = atoi(c + 1); return 0; }
	if (strcmp(c, "page:text") == 0) { test_page = 1; return 0; }
	if (strcmp(c, "page:off") == 0) { test_page = 0; return 0; }
	if (strncmp(c, "uri:", 4) == 0) { snprintf(rc_uri, sizeof(rc_uri), "%s", c + 4); return 0; }
	if (strncmp(c, "hint:", 5) == 0) { text_hint_mode = atoi(c + 5); atlas_dirty = 1; return 0; }
	if (strncmp(c, "shot:", 5) == 0) { snprintf(rc_shot, sizeof(rc_shot), "%s", c + 5); return 0; }
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(c, map[i].name) == 0) return map[i].btn;
	return 0;
}

static void png_file_write(png_structp png, png_bytep data, png_size_t len)
{
	SceUID fd = *(SceUID *)png_get_io_ptr(png);
	sceIoWrite(fd, data, len);
}
static void png_file_flush(png_structp png) { (void)png; }

/* Saves the frame just rendered (call after vita2d_end_drawing, before the swap). */
static void save_screenshot(const char *name)
{
	char path[96];
	snprintf(path, sizeof(path), CONFIG_DIR "/%s.png", name);
	vita2d_wait_rendering_done();
	SceDisplayFrameBuf fbi;
	memset(&fbi, 0, sizeof(fbi));
	fbi.size = sizeof(fbi);
	if (sceDisplayGetFrameBuf(&fbi, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || !fbi.base) return;
	const uint8_t *fb = fbi.base;
	int pitch = fbi.pitch ? fbi.pitch : 1024;
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return;
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	uint8_t *row = malloc(SCREEN_W * 3);
	if (!png || !info || !row || setjmp(png_jmpbuf(png))) {
		if (png) png_destroy_write_struct(&png, info ? &info : NULL);
		free(row);
		sceIoClose(fd);
		return;
	}
	png_set_write_fn(png, &fd, png_file_write, png_file_flush);
	png_set_IHDR(png, info, SCREEN_W, SCREEN_H, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	             PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_compression_level(png, 1);
	png_write_info(png, info);
	for (int y = 0; y < SCREEN_H; y++) {
		const uint8_t *src = fb + (size_t)y * pitch * 4;
		for (int x = 0; x < SCREEN_W; x++) {
			row[x * 3 + 0] = src[x * 4 + 0];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 2];
		}
		png_write_row(png, row);
	}
	png_write_end(png, info);
	png_destroy_write_struct(&png, &info);
	free(row);
	sceIoClose(fd);
}

/* Actions queued by the UI and carried out once per frame, after input. */
enum { ACT_NONE, ACT_EXIT_ASK, ACT_EXIT_DO, ACT_ARTDEC_ASK, ACT_ARTDEC_DO, ACT_URI, ACT_START, ACT_INFO, ACT_REFRESH };

static void launch_uri(const char *uri)
{
	vs_shutdown();
	/* psgm: starts a title (and replaces us); the rest open system apps/pages over us */
	int flags = strncmp(uri, "psgm:", 5) == 0 ? 0xFFFFF : 0x20000;
	sceKernelDelayThread(10000);
	int r = sceAppMgrLaunchAppByUri(flags, uri);
	trace("launch_uri %s flags=%x -> %08x\n", uri, flags, r);
	if (r < 0 && flags != 0xFFFFF) {                     /* some pages only answer to the other flag */
		int r2 = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
		trace("  retry flags=ffff -> %08x\n", r2);
	}
	if (flags == 0xFFFFF) {
		sceKernelDelayThread(1000);
		audio_run = 0;
		sceKernelExitProcess(0);
	}
}

static int item_has_options(int m, const Item *it)
{
	return (m == M_MEMCARD && it->kind == KIND_APP) || (m == M_SAVES && it->id[0]);
}

int main(void)
{
	vita2d_init();
	vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
	text_init("app0:assets/font.otf");
	load_icons();
	sound_load_all();
	SceUID athread = sceKernelCreateThread("xmb_audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if (athread >= 0) sceKernelStartThread(athread, 0, NULL);

	ensure_dirs();
	config_load();
	build_menus();
	for (int mi = 0; mi < M_COUNT; mi++)
		for (int ji = 0; ji < menus[mi].count; ji++)
			if (menus[mi].items[ji].stock) glow_for(menus[mi].items[ji].stock);
	sound_play(SND_OPENING);

	int cat = CAT_GAME;
	int cur = cat;                      /* menu currently shown */
	int stack[MAX_DEPTH], depth = 0;    /* parents when inside a folder */
	float cat_pos = (float)cat;

	/* column transition: new list slides in from slide_dir, old one slides out */
	int prev_menu = -1;
	float in_t = 1.0f, out_t = 1.0f;
	int folder_trans = 0;               /* 1: in/out animation is a folder open/close */
	float slide_dir = 1.0f;

	for (int i = 0; i < M_COUNT; i++) menus[i].pos = (float)menus[i].sel;

	SceCtrlData pad, prev;
	memset(&pad, 0, sizeof(pad));
	memset(&prev, 0, sizeof(prev));
	int hold_frames = 0;
	unsigned int hold_btn = 0;

	float t = 0.0f;
	float folder_t = 0.0f;              /* 1 inside a game folder (wide icons) */
	float sub_t = 0.0f;                 /* 1 inside a Settings-style sub list */
	float startup_t = startup_anim ? 0.0f : 1.0f;
	const Item *launching = NULL;       /* set once Start Game has begun */
	float launch_fade = 0.0f;
	int launch_sent = 0;
	float launch_wait = 0.0f;

	/* overlays */
	int page_open = PAGE_NONE; float page_t = 0.0f;       /* System Information / game Information */
	int page_shown = PAGE_NONE;                           /* last page opened, kept while it fades out */
	int dlg_open = 0; float dlg_t = 0.0f;                 /* confirmation dialog */
	char dlg_l1[96] = "", dlg_l2[96] = "";
	int dlg_sel = 0, dlg_action = ACT_NONE;
	const Item *dlg_item = NULL;
	int opt_open = 0; float opt_t = 0.0f;                 /* Options panel */
	int opt_ids[3], opt_n = 0, opt_sel = 0;
	const Item *opt_item = NULL;
	int opt_menu = 0;

	/* dwell background (PSP's "hold on a game" effect, using the app's pic0.png) */
	vita2d_texture *pic_tex = NULL;
	char pic_loaded[112] = "";
	float pic_alpha = 0.0f;
	const Item *dwell_item = NULL;
	float dwell_t = 0.0f;
	SceUID bgthread = sceKernelCreateThread("xmb_bg", bg_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (bgthread >= 0) sceKernelStartThread(bgthread, 0, NULL);
	SceUID decthread = sceKernelCreateThread("xmb_dec", dec_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (decthread >= 0) sceKernelStartThread(decthread, 0, NULL);
	int seen_dec_gen = 0;
	SceUID musicthread = sceKernelCreateThread("xmb_music", music_thread, 0x10000110, 0x40000, 0, 0, NULL);
	{ int sr = musicthread >= 0 ? sceKernelStartThread(musicthread, 0, NULL) : -1; trace("music thread create=%08x start=%08x\n", musicthread, sr); }
	SceUID iconthread = sceKernelCreateThread("xmb_icons", icon_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (iconthread >= 0) sceKernelStartThread(iconthread, 0, NULL);

	for (;;) {
		uint64_t prof_t0 = sceKernelGetProcessTimeWide();
		/* ---------------- input ---------------- */
		prev = pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);

		unsigned int btn = pad.buttons;
		if (pad.lx < 60)  btn |= SCE_CTRL_LEFT;
		if (pad.lx > 195) btn |= SCE_CTRL_RIGHT;
		if (pad.ly < 60)  btn |= SCE_CTRL_UP;
		if (pad.ly > 195) btn |= SCE_CTRL_DOWN;

		unsigned int pb = prev.buttons;
		if (prev.lx < 60)  pb |= SCE_CTRL_LEFT;
		if (prev.lx > 195) pb |= SCE_CTRL_RIGHT;
		if (prev.ly < 60)  pb |= SCE_CTRL_UP;
		if (prev.ly > 195) pb |= SCE_CTRL_DOWN;
		unsigned int pressed = btn & ~pb;

		/* auto-repeat on directions */
		unsigned int dirs = btn & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN);
		if (dirs && dirs == hold_btn) {
			hold_frames++;
			if (hold_frames > 22 && (hold_frames % 5) == 0) pressed |= dirs;
		} else {
			hold_btn = dirs;
			hold_frames = 0;
		}
		pressed |= remote_step();
		if (launching || startup_t < 0.45f) pressed = 0;

		int act = ACT_NONE;
		const Item *act_item = NULL;
		Menu *mn = &menus[cur];
		int lay = menu_layout(cur);

		if (dlg_open) {
			/* ---- confirmation dialog ---- */
			if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { dlg_sel = !dlg_sel; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_CROSS) {
				if (dlg_sel == 0) { act = dlg_action; act_item = dlg_item; }
				else sound_play(SND_CANCEL);
				dlg_open = 0;
			} else if (pressed & SCE_CTRL_CIRCLE) {
				sound_play(SND_CANCEL);
				dlg_open = 0;
			}
		} else if (page_open == PAGE_PLAYER) {
			/* ---- music player ---- */
			int total = menus[M_TRACKS].count;
			if (pressed & SCE_CTRL_CROSS) { sound_play(SND_CURSOR); music_toggle(); }
			if (pressed & SCE_CTRL_CIRCLE) { sound_play(SND_CANCEL); page_open = PAGE_NONE; }
			if ((pressed & SCE_CTRL_RTRIGGER) && total) { sound_play(SND_CURSOR); music_start((music_index + 1) % total); }
			if ((pressed & SCE_CTRL_LTRIGGER) && total) { sound_play(SND_CURSOR); music_start((music_index + total - 1) % total); }
			if (pressed & SCE_CTRL_LEFT)  music_seek_rel(-10000);
			if (pressed & SCE_CTRL_RIGHT) music_seek_rel(10000);
		} else if (page_open != PAGE_NONE) {
			/* ---- information page ---- */
			if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) { sound_play(SND_CANCEL); page_open = PAGE_NONE; }
		} else if (opt_open) {
			/* ---- Options panel ---- */
			if (pressed & SCE_CTRL_UP)   { if (opt_sel > 0) { opt_sel--; sound_play(SND_CURSOR); } }
			if (pressed & SCE_CTRL_DOWN) { if (opt_sel < opt_n - 1) { opt_sel++; sound_play(SND_CURSOR); } }
			if (pressed & SCE_CTRL_CROSS) {
				int id = opt_ids[opt_sel];
				if (id != OPT_START) sound_play(SND_CURSOR);
				act = id == OPT_START ? ACT_START : (id == OPT_INFO ? ACT_INFO : ACT_REFRESH);
				act_item = opt_item;
				opt_open = 0;
			} else if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) {
				sound_play(SND_CANCEL);
				opt_open = 0;
			}
		} else {
			/* ---- the XMB itself ---- */
			int move_cat = 0, move_item = 0, go_back = 0, cycle = 0;
			if (lay == LAY_SUB) {
				if (pressed & SCE_CTRL_LEFT)  go_back = 1;     /* the arrow points back */
				if (pressed & SCE_CTRL_RIGHT) cycle = 1;
			} else {
				if (pressed & SCE_CTRL_LEFT)  move_cat = -1;
				if (pressed & SCE_CTRL_RIGHT) move_cat = 1;
			}
			if (pressed & SCE_CTRL_UP)    move_item = -1;
			if (pressed & SCE_CTRL_DOWN)  move_item = 1;
			if (pressed & SCE_CTRL_LTRIGGER) move_item = -5;
			if (pressed & SCE_CTRL_RTRIGGER) move_item = 5;
			if (pressed & SCE_CTRL_CIRCLE) go_back = 1;

			if (move_cat) {
				int n = cat + move_cat;
				if (n >= 0 && n < CAT_COUNT) {
					cat = n;
					prev_menu = cur;
					cur = cat;
					depth = 0;
					slide_dir = (float)move_cat;
					folder_trans = 0;       /* the column rides along with the category bar */
					in_t = 1.0f; out_t = 1.0f;
					sound_play(SND_CURSOR);
					mn = &menus[cur];
				}
			}
			if (move_item && mn->count) {
				int n = mn->sel + move_item;
				if (n < 0) n = 0;
				if (n >= mn->count) n = mn->count - 1;
				if (n != mn->sel) sound_play(SND_CURSOR);
				mn->sel = n;
			}

			Item *it = mn->count ? &mn->items[mn->sel] : NULL;
			if (it && ((pressed & SCE_CTRL_CROSS) || cycle)) {
				switch (it->kind) {
				case KIND_APP:
					if (pressed & SCE_CTRL_CROSS) { launching = it; sound_play(SND_STARTGAME); }
					break;
				case KIND_FOLDER:
					if ((pressed & SCE_CTRL_CROSS) && depth < MAX_DEPTH && it->submenu >= 0) {
						sound_play(SND_CURSOR);
						stack[depth++] = cur;
						prev_menu = cur;
						cur = it->submenu;
						slide_dir = 1.0f;
						folder_trans = 1;
						in_t = 0.0f; out_t = 0.0f;
					}
					break;
				case KIND_VALUE:
					sound_play(it->value_id == SET_ART && !art_decrypt ? SND_CONFIRM : SND_CURSOR);
					if (it->value_id == SET_ART && !art_decrypt) {
						act = ACT_ARTDEC_ASK;      /* kernel modules: ask first */
					} else {
						setting_change(it->value_id, 1);
					}
					break;
				case KIND_URI:
					if (pressed & SCE_CTRL_CROSS) { sound_play(SND_CURSOR); act = ACT_URI; act_item = it; }
					break;
				case KIND_PAGE:
					if (pressed & SCE_CTRL_CROSS) {
						sound_play(SND_CURSOR);
						if (it->value_id == PAGE_SYSINFO) { sysinfo_gather(); page_open = PAGE_SYSINFO; }
					}
					break;
				case KIND_EXIT:
					if (pressed & SCE_CTRL_CROSS) { sound_play(confirm_dialogs ? SND_CONFIRM : SND_CURSOR); act = ACT_EXIT_ASK; }
					break;
				case KIND_TRACK:
					if (pressed & SCE_CTRL_CROSS) {
						sound_play(SND_CURSOR);
						if (!(music_index == mn->sel && mus_ready)) music_start(mn->sel);
						page_open = PAGE_PLAYER;
					}
					break;
				default:
					break;
				}
			}
			if (go_back) {
				if (depth > 0) {
					sound_play(SND_CANCEL);
					prev_menu = cur;
					cur = stack[--depth];
					slide_dir = -1.0f;
					folder_trans = 1;
					in_t = 0.0f; out_t = 0.0f;
				}
			}
			if ((pressed & SCE_CTRL_TRIANGLE) && it && item_has_options(cur, it)) {
				sound_play(SND_CONFIRM);
				opt_item = it;
				opt_menu = cur;
				opt_n = 0;
				if (it->kind == KIND_APP) opt_ids[opt_n++] = OPT_START;
				opt_ids[opt_n++] = OPT_INFO;
				opt_ids[opt_n++] = OPT_REFRESH;
				opt_sel = 0;
				opt_open = 1;
			}
		}

		if (rc_uri[0]) {                                     /* debug: try a launch flag / URI pair and log the result */
			char *colon = strchr(rc_uri, ':');
			if (colon) {
				*colon = 0;
				unsigned fl = (unsigned)strtoul(rc_uri, NULL, 16);
				int r;
				if (!strcmp(rc_uri, "N")) {                      /* N:<titleid> -> sceAppMgrLaunchAppByName2 */
					r = sceAppMgrLaunchAppByName2(colon + 1, NULL, NULL);
					trace("TEST byname2 %s -> %08x\n", colon + 1, r);
				} else if (rc_uri[0] == 'B') {                   /* B<flags>:<titleid> -> sceAppMgrLaunchAppByName */
					fl = (unsigned)strtoul(rc_uri + 1, NULL, 16);
					r = sceAppMgrLaunchAppByName((int)fl, colon + 1, "");
					trace("TEST byname flags=%x %s -> %08x\n", fl, colon + 1, r);
				} else {
					r = sceAppMgrLaunchAppByUri((int)fl, colon + 1);
					trace("TEST flags=%x uri=%s -> %08x\n", fl, colon + 1, r);
				}
			}
			rc_uri[0] = 0;
		}

		/* ---- carry out whatever the UI asked for ---- */
		if (act == ACT_EXIT_ASK) {
			if (confirm_dialogs) {
				snprintf(dlg_l1, sizeof(dlg_l1), "Exit VitaXMB and return to the LiveArea?");
				dlg_l2[0] = 0;
				dlg_action = ACT_EXIT_DO; dlg_item = NULL; dlg_sel = 0; dlg_open = 1;
				act = ACT_NONE;
			} else {
				act = ACT_EXIT_DO;
			}
		} else if (act == ACT_ARTDEC_ASK) {
			snprintf(dlg_l1, sizeof(dlg_l1), "Decrypt game artwork?");
			snprintf(dlg_l2, sizeof(dlg_l2), "Loads VitaShell's kernel modules. Experimental.");
			dlg_action = ACT_ARTDEC_DO; dlg_item = NULL; dlg_sel = 1; dlg_open = 1;
			act = ACT_NONE;
		}

		switch (act) {
		case ACT_EXIT_DO:
			sceKernelDelayThread(300 * 1000);
			audio_run = 0;
			vs_shutdown();
			sceKernelExitProcess(0);
			break;
		case ACT_ARTDEC_DO:
			setting_change(SET_ART, 1);
			break;
		case ACT_URI:
			if (act_item) launch_uri(act_item->uri);
			break;
		case ACT_START:
			if (act_item) { launching = act_item; sound_play(SND_STARTGAME); }
			break;
		case ACT_INFO:
			if (act_item) { gameinfo_gather(act_item, opt_menu == M_SAVES); page_open = PAGE_GAMEINFO; }
			break;
		case ACT_REFRESH:
			if (opt_menu == M_MEMCARD) { scan_apps(); update_game_counts(); menus[M_MEMCARD].pos = (float)menus[M_MEMCARD].sel; }
			if (opt_menu == M_SAVES)   { scan_saves(); update_game_counts(); menus[M_SAVES].pos = (float)menus[M_SAVES].sel; }
			break;
		default: break;
		}

		lay = menu_layout(cur);
		mn = &menus[cur];
		uint64_t prof_t1 = sceKernelGetProcessTimeWide();
		update_icons(cur, out_t < 1.0f ? prev_menu : -1);
		uint64_t prof_t2 = sceKernelGetProcessTimeWide();

		/* freshly decrypted artwork: forget the "missing" result and look again */
		if (dec_gen != seen_dec_gen) {
			seen_dec_gen = dec_gen;
			Menu *am = &menus[M_MEMCARD];
			for (int i = 0; i < am->count; i++) {
				Item *ai = &am->items[i];
				if (!ai->dec_queued) continue;
				ai->meta_resolved = 0;
				ai->gate_path[0] = ai->bg_path[0] = 0;
				if (ai->icon_rect && ai->icon) { defer_free(ai->icon); ai->icon = NULL; }
				ai->icon_rect = 0;
				ai->icon_tried = 0;
				ai->load_state = 0;
			}
			pic_loaded[0] = 0;
		}

		/* ---------------- dwell background ---------------- */
		{
			const Item *sel_it = menus[cur].count ? &menus[cur].items[menus[cur].sel] : NULL;
			if (sel_it != dwell_item) { dwell_item = sel_it; dwell_t = 0.0f; }
			else dwell_t += 1.0f / 60.0f;

			const char *want = NULL;
			if (dwell_item && dwell_item->kind == KIND_APP && !launching && dwell_t > 0.3f && !opt_open && page_open == PAGE_NONE) {
				Item *wi = (Item *)dwell_item;
				if (wi->meta_resolved == 2)
					want = wi->bg_path[0] ? wi->bg_path : (wi->pic_path[0] ? wi->pic_path : NULL);
			}
			int showing = want && pic_tex && strcmp(want, pic_loaded) == 0;

			/* pick up a finished decode */
			if (bg_state == 3) {
				if (bg_pix) {
					vita2d_texture *nt = texture_from_rgba(bg_pix, bg_w, bg_h);
					free(bg_pix); bg_pix = NULL;
					if (nt) {
						if (pic_tex) defer_free(pic_tex);
						pic_tex = nt;
						snprintf(pic_loaded, sizeof(pic_loaded), "%s", bg_done_path);
					}
				} else {
					/* failed: drop the previous texture so another game's image can't linger */
					if (pic_tex) { defer_free(pic_tex); pic_tex = NULL; }
					snprintf(pic_loaded, sizeof(pic_loaded), "%s", bg_done_path);
				}
				bg_state = 0;
			}
			/* after holding on a title for a moment, fetch its image */
			if (want && dwell_t > 0.5f && bg_state == 0 && strcmp(want, pic_loaded) != 0 && pic_alpha < 0.05f) {
				snprintf(bg_req_path, sizeof(bg_req_path), "%s", want);
				bg_state = 1;
			}
			if (dwell_item && dwell_item->kind == KIND_APP && !showing && want && !pic_tex &&
			    strcmp(want, pic_loaded) == 0 && bg_state == 0 &&
			    dwell_item->pic_path[0] && strcmp(want, dwell_item->pic_path) != 0 && dwell_t > 0.5f) {
				/* LiveArea background failed to decode: fall back to pic0.png */
				snprintf(bg_req_path, sizeof(bg_req_path), "%s", dwell_item->pic_path);
				bg_state = 1;
			}
			float target = (showing && dwell_t > 0.5f) ? 1.0f : 0.0f;
			pic_alpha = lerpf(pic_alpha, target, target > pic_alpha ? 0.07f : 0.18f);
			if (pic_alpha < 0.002f) pic_alpha = 0.0f;
		}

		/* ---------------- music upkeep ---------------- */
		if (music_index >= 0 && mus_ready && mus_eof && mus_w == mus_r) {      /* track finished: next one */
			int total = menus[M_TRACKS].count;
			if (total > 0) music_start((music_index + 1) % total);
		}
		if (page_open != PAGE_NONE) page_shown = page_open;
		if (page_shown == PAGE_PLAYER && page_t > 0.0f) spectrum_update(mus_playing && mus_ready);

		/* ---------------- animation ---------------- */
		const float dt_s = 1.0f / 60.0f;
		cat_pos = lerpf(cat_pos, (float)cat, 0.22f);
		if (fabsf(cat_pos - cat) < 0.004f) cat_pos = (float)cat;
		for (int i = 0; i < M_COUNT; i++) {
			menus[i].pos = lerpf(menus[i].pos, (float)menus[i].sel, 0.25f);
			if (fabsf(menus[i].pos - menus[i].sel) < 0.004f) menus[i].pos = (float)menus[i].sel;
		}
		for (int mi = 0; mi < M_COUNT; mi++) {
			Menu *gm = &menus[mi];
			for (int ji = 0; ji < gm->count; ji++) {
				Item *gi = &gm->items[ji];
				float tg = ji == gm->sel ? 1.0f : 0.0f;
				gi->glow += (tg - gi->glow) * (tg > gi->glow ? 0.70f : 0.80f);
				if (fabsf(tg - gi->glow) < 0.01f) gi->glow = tg;
			}
		}
		in_t  = clampf(in_t  + dt_s / 0.30f, 0.0f, 1.0f);
		out_t = clampf(out_t + dt_s / 0.22f, 0.0f, 1.0f);
		t += dt_s;
		startup_t = clampf(startup_t + dt_s / 2.2f, 0.0f, 1.0f);
		{ static unsigned frame; if ((++frame % 300) == 0) trace("frame %u menu=%d sel=%d\n", frame, cur, menus[cur].sel); }
		folder_t = lerpf(folder_t, lay == LAY_GAME ? 1.0f : 0.0f, 0.2f);
		if (fabsf(folder_t - (lay == LAY_GAME)) < 0.002f) folder_t = (float)(lay == LAY_GAME);
		sub_t = lerpf(sub_t, lay == LAY_SUB ? 1.0f : 0.0f, 0.2f);
		if (fabsf(sub_t - (lay == LAY_SUB)) < 0.002f) sub_t = (float)(lay == LAY_SUB);
		page_t = lerpf(page_t, page_open != PAGE_NONE ? 1.0f : 0.0f, page_open != PAGE_NONE ? 0.25f : 0.45f);
		if (page_t < 0.01f) page_t = 0.0f; else if (page_t > 0.99f) page_t = 1.0f;
		dlg_t = lerpf(dlg_t, dlg_open ? 1.0f : 0.0f, 0.3f);
		if (dlg_t < 0.01f) dlg_t = 0.0f; else if (dlg_t > 0.99f) dlg_t = 1.0f;
		opt_t = lerpf(opt_t, opt_open ? 1.0f : 0.0f, 0.3f);
		if (opt_t < 0.01f) opt_t = 0.0f; else if (opt_t > 0.99f) opt_t = 1.0f;
		if (launching) {
			launch_fade = clampf(launch_fade + dt_s / 1.2f, 0.0f, 1.0f);
			if (!launch_sent && !sound_playing(SND_STARTGAME)) {
				audio_run = 0;
				launch_request(launching);
				launch_sent = 1;
			}
			if (launch_sent) {
				launch_wait += dt_s;
				if (launch_wait > 10.0f) sceKernelExitProcess(0);   /* launch failed: give up */
			}
		}

		SceDateTime dt;
		sceRtcGetCurrentClockLocalTime(&dt);
		int month = theme ? theme - 1 : (dt.month >= 1 && dt.month <= 12 ? dt.month - 1 : 0);
		Palette pal = get_palette(month);

		/* ---------------- draw ---------------- */
		flush_free();
		text_frame_begin();
		vita2d_start_drawing();
		vita2d_clear_screen();

		bg_alpha = 1.0f;
		draw_background(t, month);
		if (pic_tex && pic_alpha > 0.0f) {
			/* cover-fit: fill the screen, cropping the overflow evenly */
			float tw = vita2d_texture_get_width(pic_tex), th = vita2d_texture_get_height(pic_tex);
			float sc = fmaxf(SCREEN_W / tw, SCREEN_H / th);
			vita2d_draw_texture_tint_scale(pic_tex, (SCREEN_W - tw * sc) / 2, (SCREEN_H - th * sc) / 2,
			                               sc, sc, RGBA8(255, 255, 255, (int)(255 * pic_alpha)));
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(70 * pic_alpha)));
		}
		draw_status(&dt);

		/* startup: the bar slides in from the left, the lists rise after it */
		float su_bar = ease_out(clampf((startup_t - 0.10f) / 0.55f, 0.0f, 1.0f));

		/* Category row: the open category slides left for sub lists / game folders, the rest fade */
		float fe = ease_out(folder_t), se = ease_out(sub_t);
		float hide = fmaxf(fe, se);
		for (int i = 0; i < CAT_COUNT; i++) {
			float d = i - cat_pos;
			float x = CAT_X + d * CAT_SPACING + clampf(d, -1.0f, 1.0f) * 11.0f - 196.0f * fe - 118.0f * se;
			x -= (1.0f - su_bar) * 240.0f;
			float sel = 1.0f - clampf(fabsf(d), 0.0f, 1.0f);
			float size = lerpf(98.0f, 120.0f, sel);
			int a = (int)(lerpf(165, 255, sel) * su_bar);
			if (i != cat) a = (int)(a * (1.0f - hide));         /* only the open category stays */
			if (a > 2) draw_icon(cat_tex[i], x, CAT_Y, size, a);
			if (sel > 0.3f)
				ptext_vc(x - ptext_width(22, cat_names[i]) / 2.0f, CAT_Y + 58,
				         WHITE((int)(255 * sel * (1.0f - fe) * su_bar)), 22, cat_names[i]);
		}

		/* Lists. Each column is pinned to its category's x, so it travels with the bar.
		 * Opening/closing a folder adds a short slide on top. */
		{
			const float slide_px = 110.0f;
			int owner_cur = menu_owner(cur);
			float dc = owner_cur - cat_pos;
			float cur_x = dc * CAT_SPACING + clampf(dc, -1.0f, 1.0f) * 11.0f;
			float cur_a = (1.0f - clampf(fabsf(dc), 0.0f, 1.0f)) * su_bar;
			cur_x -= (1.0f - su_bar) * 240.0f;

			if (lay == LAY_SUB) {
				int par = depth > 0 ? stack[depth - 1] : menu_owner(cur);
				draw_column(par, cur_x, cur_a, 0.0f, sub_t);                 /* icon-only parent column */
				float cx = (folder_trans ? slide_dir * slide_px * (1.0f - ease_out(in_t)) : 0.0f) - (1.0f - su_bar) * 240.0f;
				draw_sub_list(cur, cx, folder_trans ? ease_out(in_t) : 1.0f);
				if (prev_menu >= 0 && menu_layout(prev_menu) == LAY_SUB && out_t < 1.0f)
					draw_sub_list(prev_menu, -slide_dir * slide_px * ease_out(out_t), 1.0f - ease_out(out_t));
				if (out_t >= 1.0f || prev_menu == par) prev_menu = -1;
			} else {
				if (prev_menu >= 0) {
					int owner_prev = menu_owner(prev_menu);
					float dp = owner_prev - cat_pos;
					float px = dp * CAT_SPACING + clampf(dp, -1.0f, 1.0f) * 11.0f;
					float pa = 1.0f - clampf(fabsf(dp), 0.0f, 1.0f);
					if (folder_trans && owner_prev == owner_cur) {
						px += -slide_dir * slide_px * ease_out(out_t);
						pa *= 1.0f - ease_out(out_t);
					}
					if (menu_layout(prev_menu) == LAY_SUB) draw_sub_list(prev_menu, px, pa);
					else draw_column(prev_menu, px, pa, 0.0f, 0.0f);
					if (pa < 0.01f || (folder_trans && out_t >= 1.0f)) prev_menu = -1;
				}
				if (folder_trans) {
					cur_x += slide_dir * slide_px * (1.0f - ease_out(in_t));
					cur_a *= ease_out(in_t);
				}
				draw_column(cur, cur_x, cur_a, pic_alpha, sub_t);
			}
		}

		if (folder_t > 0.05f) {                       /* "back" arrow at the left edge of a game folder */
			int ba = (int)(230 * folder_t);
			for (int i = 0; i < 18; i++)
				vita2d_draw_rectangle(10 + i * 0.7f, 272 - (18 - i) * 0.55f - 0.0f, 1.0f, (18 - i) * 1.1f, WHITE(ba));
		}
		if (sub_t > 0.05f) glyph_arrow_left(205, 270, 20, (int)(235 * sub_t));   /* ◀ beside the sub list */

		/* "Options" pill, only where the triangle does something */
		{
			const Item *sit = menus[cur].count ? &menus[cur].items[menus[cur].sel] : NULL;
			if (sit && item_has_options(cur, sit) && !dlg_open && page_open == PAGE_NONE && !launching)
				draw_options_pill((int)(255 * (1.0f - opt_t) * su_bar));
		}

		/* full-page information screens replace the XMB (the waves stay) */
		if (page_t > 0.0f) {
			if (page_shown == PAGE_PLAYER) {
				draw_player(page_t, page_open != PAGE_PLAYER);
			} else {
				bg_alpha = page_t;
				draw_background(t, month);
				bg_alpha = 1.0f;
				draw_status(&dt);
				draw_info_page(page_t);
			}
		}

		if (opt_t > 0.0f) draw_options_panel(opt_t, opt_ids, opt_n, opt_sel, &pal);
		if (dlg_t > 0.0f) draw_dialog(dlg_t, dlg_l1, dlg_l2, dlg_sel);

		if (test_page) {
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(40, 40, 48, 255));
			text_flat = 1;
			draw_text_grid();
			text_flat = 0;
		}

		if (launch_fade > 0.0f)
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(255 * launch_fade)));
		if (startup_t < 0.6f)                         /* fade in from black */
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(255 * (1.0f - ease_out(startup_t / 0.6f)))));

		vita2d_end_drawing();
		uint64_t prof_t3 = sceKernelGetProcessTimeWide();
		vita2d_swap_buffers();
		uint64_t prof_t4 = sceKernelGetProcessTimeWide();
		if (prof_t4 - prof_t0 > 24000)
			trace("slow frame %u us: input=%u icons=%u draw=%u swap=%u\n", (unsigned)(prof_t4 - prof_t0),
			      (unsigned)(prof_t1 - prof_t0), (unsigned)(prof_t2 - prof_t1), (unsigned)(prof_t3 - prof_t2), (unsigned)(prof_t4 - prof_t3));
		if (rc_shot[0]) { sceKernelDelayThread(50000); save_screenshot(rc_shot); rc_shot[0] = 0; }
	}

	return 0;
}
