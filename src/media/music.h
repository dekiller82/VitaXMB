#pragma once

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
	ptext_right_vc(940 - tw, 494, pt_player_col_set ? RGBA8(pt_player_col[0], pt_player_col[1], pt_player_col[2], a) : RGBA8(40, 100, 255, a), 34, e);
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
	if (m < CAT_COUNT && pt_text_list()) { if (sub_p < 0.01f) draw_text_rows(m, xoff, amul); return; }   /* Euphoria-style text list */
	if (pt_strip_mode && m < CAT_COUNT && sub_p < 0.01f) {          /* (inside a sub list the parent is the small icon at the left, as in every layout) */
		/* strip themes show only the open item: one big icon centred on the screen, its name below (tile
		 * icons that carry their own caption need no extra text) */
		if (!mn->count) return;
		const Item *it = &mn->items[mn->sel];
		vita2d_texture *tex = it->icon ? it->icon : pt_swap(it->stock);
		if (!tex) return;
		int a = (int)(255 * amul);
		float tw = (float)vita2d_texture_get_width(tex), th = (float)vita2d_texture_get_height(tex);
		int themed = pt_owned(tex) || tex != it->stock;
		float size = themed ? 2.0f * (tw > th ? tw : th) : 112.0f;
		float w, h;
		icon_dims(tex, size, &w, &h);
		if (!themed) { w = h = size; }
		const float bottom = 352.0f, cx = 480.0f + xoff;
		draw_icon_wh(tex, cx, bottom - h / 2.0f, w, h, a);
		if (!(themed && th >= 70.0f)) {
			ptext_vc(cx - ptext_width(28, it->title) / 2.0f, bottom + 28.0f, WHITE(a), 28, it->title);
			if (it->sub[0]) ptext_vc(cx - ptext_width(22, it->sub) / 2.0f, bottom + 58.0f, WHITE(a * 8 / 10), 22, it->sub);
		}
		return;
	}
	float sp = ease_out(sub_p);
	/* the Game list (Saved Data / Memory Stick) uses a slightly tighter text column than Settings */
	const float base_icon_x = m == CAT_SETTINGS ? 207.0f : 218.0f;      /* Settings icons carry a wrench badge that hangs left */
	const float text_x = m == CAT_SETTINGS ? 300.0f : 283.0f;
	const float icon_x = lerpf(base_icon_x, 100.0f, sp);
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -1.6f || d > 3.0f) continue;

		/* the column is an XList of style 1: pitch and gap above in PSP pixels (65 / 60, which a theme can patch) */
		float spacing = lerpf(2.0f * xstyles[XS_COLUMN].pitch, 115.0f, sp);
		float y = ITEM_Y + d * spacing;
		if (d < 0) y -= 2.0f * xstyles[XS_COLUMN].gap_above * (1.0f - sp) * clampf(-d, 0.0f, 1.0f);  /* hop over the category row */
		float sel = 1.0f - clampf(fabsf(d), 0.0f, 1.0f);              /* position-based: icon size only */
		const Item *it = &mn->items[j];
		float emph = it->glow;                                         /* selection-based: brightness, rule, glow */
		float fade = d < 0 ? clampf(1.0f + d * 0.6f, 0.0f, 1.0f) * (1.0f - sp)
		                    : clampf(1.0f - d * 0.1f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.42f, 1.0f, emph));
		if (a <= 4) continue;

		float ix = icon_x + xoff;
		if (m == CAT_SETTINGS && sp < 0.5f && (pt_owned(pt_swap(it->stock)) || it->icon)) ix += (218.0f - 207.0f) * (1.0f - sp);   /* a theme's own icons have no hanging wrench badge: centre them under the category icon */
		if (it->icon && it->icon_rect) {
			/* LiveArea gate image: a landscape rectangle like the PSP's ICON0 */
			float w = 150.0f + 30.0f * grow * sel;
			float h = w * vita2d_texture_get_height(it->icon) / vita2d_texture_get_width(it->icon);
			draw_icon_wh(it->icon, ix, y, w, h, a);
		} else {
			float msc = it->icon ? 1.0f : pt_menu_scale;                 /* the theme's size for the menu's icons */
			float isz = (it->icon ? 72.0f : lerpf(112.0f, 120.0f, sel) * msc) + 36.0f * grow * sel;
			isz = lerpf(isz, it->icon ? 72.0f : lerpf(84.0f, 92.0f, sel) * msc, sp);
			vita2d_texture *tex = it->icon ? it->icon : pt_swap(it->stock);
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
