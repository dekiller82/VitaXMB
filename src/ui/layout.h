#pragma once

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

/* Layout measured from PSP screenshots (480x272) at 2x. */
/* Offset of a category d steps from the open one: spacing and the gap beside the open category come from the theme. */
#define CAT_OFFSET(d) ((d) * pt_pitch * 2.0f + clampf((d), -1.0f, 1.0f) * pt_gap * 2.0f)
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

/* Set by main every frame: something is loading (a game starting, an update downloading) and the busy spinner shows. */
static int status_busy;

/* The Vita's system volume is 0..30 in the registry; zero is the PSP's "mute" (read twice a second at most). */
static int status_muted(void)
{
	static uint64_t last;
	static int muted;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (last == 0 || now - last > 500000) {
		int vol = -1;
		last = now;
		muted = sceRegMgrGetKeyInt("/CONFIG/SOUND", "main_volume", &vol) >= 0 && vol == 0;
#ifdef VITAXMB_DEBUG
		SceIoStat st;                                                      /* debug builds: an empty ux0:data/VitaXMB/forcemute shows the icon */
		if (sceIoGetstat(CONFIG_DIR "/forcemute", &st) >= 0) muted = 1;
#endif
	}
	return muted;
}

/* A status picture at its own size (the PSP's pixels are two of ours), centred on cx, cy. */
static void draw_status_pic(vita2d_texture *t, float cx, float cy, int a)
{
	if (!t) return;
	draw_icon_wh(t, cx, cy, 2.0f * vita2d_texture_get_width(t), 2.0f * vita2d_texture_get_height(t), a);
}

/* The PSP's busy spinner (system_plugin_fg.rco, tex_busy): a sheet of 17 px cells, two identical columns by 30 rows, one frame
 * per row; the plane is 17 PSP pixels wide, so it is drawn at twice that. It sits at the bottom right with its shadow two PSP
 * pixels lower and to the right, and fades in and out. */
static void draw_busy(float dt_s)
{
	static float vis;
	vis = clampf(vis + (status_busy ? 6.0f : -6.0f) * dt_s, 0.0f, 1.0f);
	if (vis <= 0.01f) return;
	vita2d_texture *tt = res_theme("system_plugin_fg", "tex_busy"), *hr = tt ? NULL : res_hires("busy");
	if (hr) {                                               /* the PSP's spinner redrawn at 4x: 68 px cells drawn at half size */
		vita2d_texture *hs = res_hires("busy_shadow");
		int frames = vita2d_texture_get_height(hr) / 68;
		if (frames < 1) return;
		int frame = (int)((sceKernelGetProcessTimeWide() / 33000) % (uint64_t)frames), a = (int)(255 * vis);
		const float cx = 2.0f * (240.0f + 226.0f), cy = 2.0f * (136.0f + 122.0f);
		if (hs && vita2d_texture_get_height(hs) >= (frame + 1) * 68)
			vita2d_draw_texture_tint_part_scale(hs, cx + 4.0f - 17.0f, cy + 4.0f - 17.0f, 0, frame * 68, 68, 68, 0.5f, 0.5f, WHITE(a));
		vita2d_draw_texture_tint_part_scale(hr, cx - 17.0f, cy - 17.0f, 0, frame * 68, 68, 68, 0.5f, 0.5f, WHITE(a));
		return;
	}
	vita2d_texture *t = res_fg("tex_busy"), *sh = res_fg("tex_busy_shadow");
	if (!t) return;
	const int cell = 17;
	int frames = vita2d_texture_get_height(t) / cell;
	if (frames < 1 || vita2d_texture_get_width(t) < cell) return;
	int frame = (int)((sceKernelGetProcessTimeWide() / 33000) % (uint64_t)frames);
	const float cx = 2.0f * (240.0f + 226.0f), cy = 2.0f * (136.0f + 122.0f);       /* the plane "busy_icon" (226, -122), centred */
	int a = (int)(255 * vis);
	float half = (float)cell;                                                      /* half of the 34 px it is drawn at */
	if (sh && vita2d_texture_get_width(sh) >= cell && vita2d_texture_get_height(sh) >= (frame + 1) * cell)
		vita2d_draw_texture_tint_part_scale(sh, cx + 4.0f - half, cy + 4.0f - half, 0, frame * cell, cell, cell, 2.0f, 2.0f, WHITE(a));
	vita2d_draw_texture_tint_part_scale(t, cx - half, cy - half, 0, frame * cell, cell, cell, 2.0f, 2.0f, WHITE(a));
}

/* The firmware's own Latin font (flash0:/font/ltn0.pgf, 6.61): the PSP draws all its Latin text in it; its letters are narrower and lighter than
 * the UI font's (FOT-NewRodin), which stays as the fallback for characters the PGF font lacks. Loaded once at start. */
static PtFont *stock_clock_font(void)
{
	static PtFont *f; static int tried;
	if (!tried) {
		tried = 1;
		size_t n = 0;
		uint8_t *b = boot_read_file("app0:assets/psp/ltn0.pgf", &n);
		if (b) { f = pt_font_parse(b, n); if (!f) free(b); }
	}
	return f;
}

static void draw_status(const SceDateTime *dt)
{
	char buf[32];
	int pct = scePowerGetBatteryLifePercent();
	const float cy = 26.0f;                       /* shared centre line of clock and battery */
	/* charging: anim_battery_charging fires OnChargeBattery every 400 ms; its handler (vshmain 0x31158) shows frame `counter` of the
	 * four-frame sheet and counts down 3, 2, 1, 0, 3, ... (empty, one bar, two, full), whatever the real level is */
	int charging = scePowerIsBatteryCharging();
	unsigned step = (unsigned)(sceKernelGetProcessTimeWide() / 400000);

	float bx = 908;
	int clock_left = 0;                           /* a battery picture as wide as the screen carries the clock at its left */
	if (pt_bat) {
		/* a theme's own battery: four frames in one picture, full to empty, drawn at twice its size where the theme puts it */
		int fw = vita2d_texture_get_width(pt_bat), fh = vita2d_texture_get_height(pt_bat) / 4;
		int frame = pct > 66 ? 0 : (pct > 33 ? 1 : (pct > 8 ? 2 : 3));
		if (charging) frame = 3 - (int)(step % 4u);                       /* vshmain's OnChargeBattery: 3, 2, 1, 0 (empty to full), then 3 again, whatever the level */
		float w = fw * 2.0f, h = fh * 2.0f;
		float x0 = pt_bat_x * 2.0f - w / 2.0f, y0 = pt_bat_y * 2.0f - h / 2.0f;
		vita2d_draw_texture_part_scale(pt_bat, x0, y0, 0, frame * fh, fw, fh, 2.0f, 2.0f);
		bx = x0;
		clock_left = fw >= 300;
	} else if (res_hires("battery")) {
		/* the firmware's battery redrawn at 4x (vector shapes fitted to its frames): frames of 176 x 64, drawn at half size = twice the PSP's
		 * 44 x 16 where the PSP puts it (plane 223, 124); a capture of the real XMB matches it in size, shape and place. Its shadow is two PSP
		 * pixels down and right. The clock keeps the old anchor (bx). */
		vita2d_texture *hb = res_hires("battery"), *hs = res_hires("battery_shadow");
		int frame = pct > 66 ? 0 : (pct > 33 ? 1 : (pct > 8 ? 2 : 3));
		if (charging) frame = 3 - (int)(step % 4u);                       /* vshmain's OnChargeBattery: 3, 2, 1, 0 (empty to full), then 3 again, whatever the level */
		/* the redrawn picture has no soft baked-in glow below its body, so it looks higher than the PSP's: measured against a capture of the
		 * real XMB its centre sat 3 px above the clock text's, the PSP's 0.6 px; two pixels down puts it back */
		float bcx = pt_bat_x * 2.0f, bcy = pt_bat_y * 2.0f + 2.0f;
		if (hs && vita2d_texture_get_height(hs) >= 4 * 68)
			vita2d_draw_texture_tint_part_scale(hs, bcx + 4.0f - 45.0f, bcy + 4.0f - 17.0f, 0, frame * 68, 180, 68, 0.5f, 0.5f, WHITE(255));
		vita2d_draw_texture_part_scale(hb, bcx - 44.0f, bcy - 16.0f, 0, frame * 64, 176, 64, 0.5f, 0.5f);
	} else if (res_fg("tex_battery") && vita2d_texture_get_height(res_fg("tex_battery")) >= 64) {
		/* the firmware's own battery (system_plugin_fg.rco, 44 x 16 frames: three segments, two, one, empty) at twice its size where the
		 * PSP puts it (plane 223, 124); a capture of the real XMB matches it in size, shape and place. Its shadow sheet is 4 frames of
		 * 17 rows, two PSP pixels down and right. The clock keeps the old anchor (bx). */
		vita2d_texture *bt = res_fg("tex_battery"), *bs = res_fg("tex_battery_shadow");
		int fw = vita2d_texture_get_width(bt), fh = vita2d_texture_get_height(bt) / 4;
		int frame = pct > 66 ? 0 : (pct > 33 ? 1 : (pct > 8 ? 2 : 3));
		if (charging) frame = 3 - (int)(step % 4u);                       /* vshmain's OnChargeBattery: 3, 2, 1, 0 (empty to full), then 3 again, whatever the level */
		float bcx = pt_bat_x * 2.0f, bcy = pt_bat_y * 2.0f;
		if (bs && vita2d_texture_get_height(bs) >= 4 * 17) {
			int sw = vita2d_texture_get_width(bs);
			vita2d_draw_texture_tint_part_scale(bs, bcx + 4.0f - sw, bcy + 4.0f - 17.0f, 0, frame * 17, sw, 17, 2.0f, 2.0f, WHITE(255));
		}
		vita2d_draw_texture_part_scale(bt, bcx - fw, bcy - fh, 0, frame * fh, fw, fh, 2.0f, 2.0f);
	} else {
	/* PSP battery: outlined body, nub on the LEFT, up to three segments filling from the right */
	const float bw = 42, bh = 24, by = cy - bh / 2;
	unsigned int line = WHITE(235);
	vita2d_draw_rectangle(bx + 1, by + 2, bw, bh, RGBA8(0, 0, 0, 38));            /* soft shadow */
	vita2d_draw_rectangle(bx, by, bw, 2, line);
	vita2d_draw_rectangle(bx, by + bh - 2, bw, 2, line);
	vita2d_draw_rectangle(bx, by, 2, bh, line);
	vita2d_draw_rectangle(bx + bw - 2, by, 2, bh, line);
	vita2d_draw_rectangle(bx - 4, cy - 5, 4, 10, line);
	int segs = pct > 66 ? 3 : (pct > 33 ? 2 : (pct > 8 ? 1 : 0));
	if (charging) segs = (int)(step % 4u);                           /* the same sequence: no bar, one, two, three */
	for (int k = 0; k < segs; k++)
		vita2d_draw_rectangle(bx + bw - 6 - 8 * (k + 1) - 2 * k + 2, by + 5, 8, bh - 10, line);
	}

	if (clock24) {
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d", dt->month, dt->day, dt->hour, dt->minute);
	} else {
		int h12 = dt->hour % 12 ? dt->hour % 12 : 12;
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d %s", dt->month, dt->day, h12, dt->minute, dt->hour < 12 ? "AM" : "PM");
	}
	const int csz = (int)((pt_font && pt_font == pt_stock_font ? 28.0f : 24.0f) * pt_clock_scale + 0.5f);   /* the PSP's font: digits 20 px tall, as in a photo of the real thing */
	const unsigned int ccol = RGBA8(pt_clock_rgb[0], pt_clock_rgb[1], pt_clock_rgb[2], (int)(240.0f * pt_clock_alpha));
	float tw = ptext_width(csz, buf), clock_l;
	if (pt_clock_code_set) {                                                       /* the firmware's code puts the clock (vshmain 0x31038) */
		float r = 2.0f * (240.0f + pt_clock_x) - 10.0f;
		ptext_right_vc(r, pt_clock_cy, ccol, csz, buf);
		clock_l = r - tw;
	} else if (clock_left && pt_clock_w > 0.0f) { ptext_right_vc(942.0f, cy, ccol, csz, buf); clock_l = 942.0f - tw; }       /* a clock with a text box: at the right edge */
	else if (clock_left) { ptext_vc(56.0f, cy, ccol, csz, buf); clock_l = 56.0f; }
	else {
		float r = pt_bat ? 2.0f * (240.0f + pt_clock_x) - 10.0f : bx - 22;
		ptext_right_vc(r, cy, ccol, csz, buf);
		clock_l = r - tw;
	}

	/* mute: the speaker with a slash, five PSP pixels left of the clock (the hold switch has no counterpart on a Vita) */
	if (status_muted()) {
		vita2d_texture *tm = res_theme("system_plugin_fg", "tex_mute"), *hm = tm ? NULL : res_hires("mute");
		if (hm) {                                                          /* the PSP's icon redrawn at 4x, drawn at half size */
			vita2d_texture *hms = res_hires("mute_shadow");
			float mw = 0.5f * vita2d_texture_get_width(hm), mh = 0.5f * vita2d_texture_get_height(hm);
			float mx = clock_l - pt_mute_gap - mw / 2.0f, my = pt_mute_cy + 2.0f;                   /* two pixels down, like the battery */
			int ma = (int)(255 * pt_mute_alpha);
			if (hms) vita2d_draw_texture_tint_scale(hms, mx + 4.0f - 0.25f * vita2d_texture_get_width(hms), my + pt_mute_sdy - 0.25f * vita2d_texture_get_height(hms), 0.5f, 0.5f, WHITE(ma));
			vita2d_draw_texture_tint_scale(hm, mx - mw / 2.0f, my - mh / 2.0f, 0.5f, 0.5f, WHITE(ma));
		}
		vita2d_texture *m = hm ? NULL : res_fg("tex_mute"), *ms = res_fg("tex_mute_shadow");
		if (m) {
			float mw = 2.0f * vita2d_texture_get_width(m), mx = clock_l - pt_mute_gap - mw / 2.0f;
			float my = pt_mute_cy;                                           /* the firmware's own line for it: 124 -> 24 */
			draw_status_pic(ms, mx + 4.0f, my + pt_mute_sdy, (int)(255 * pt_mute_alpha));
			draw_status_pic(m, mx, my, (int)(255 * pt_mute_alpha));
		}
	}
}

static float ease_out(float t) { t = clampf(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t); }

typedef struct { vita2d_texture *src, *glow; } GlowEntry;
static GlowEntry glows[120];
static int glow_n;
#define GLOW_PAD 16

static void glow_release(vita2d_texture *src)
{
	for (int i = 0; i < glow_n; i++)
		if (glows[i].src == src) {
			if (glows[i].glow) defer_free(glows[i].glow);
			glows[i] = glows[--glow_n];
			return;
		}
}

/* A soft white bloom shaped like the icon, as on the PSP's selected item. */
static vita2d_texture *glow_for(vita2d_texture *src)
{
	for (int i = 0; i < glow_n; i++) if (glows[i].src == src) return glows[i].glow;
	if (glow_n >= 120) return NULL;
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

static int is_game_folder(int m) { return m == M_MEMCARD || m == M_SAVES || m == M_FOLDER; }
static int menu_layout(int m) { return is_game_folder(m) ? LAY_GAME : (m >= M_VITAXMB && m != M_VIDEOS ? LAY_SUB : (m == M_VIDEOS ? LAY_SUB : LAY_COLUMN)); }
static int menu_owner(int m)
{
	if (m < CAT_COUNT) return m;
	if (m == M_VIDEOS) return CAT_VIDEO;
	if (m == M_TRACKS) return CAT_MUSIC;
	if (m == M_MEMCARD || m == M_SAVES || m == M_FOLDER) return CAT_GAME;
	return CAT_SETTINGS;
}

#define FOLDER_X 184.0f

/* y of the row at offset d from the selection, in the folder (game list) layout. */
static float folder_y(float d)
{
	return 272.0f + xl_offset(XS_GAME, d);                      /* the game list's style in the engine (paf.h) */
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
static void draw_rule(float x0, float x1, float y, int la);

/* The picture a row shows: its own icon once it is loaded; before that (or when it has none) a theme's loading / broken
 * picture for a game or a save; else the app's own icon. */
static vita2d_texture *item_pic(const Item *it, int menu)
{
	if (it->icon) return it->icon;
	if (it->kind == KIND_APP || menu == M_SAVES) {
		int loading = !it->icon_tried || (it->load_state >= 1 && it->load_state <= 3);
		vita2d_texture *p = res_placeholder(menu == M_SAVES, loading);
		if (p) return p;
	}
	return pt_swap(it->stock);
}

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
		float ix = FOLDER_X + pt_list_dx + xoff;
		vita2d_texture *tex = item_pic(it, m);
		if (!tex) continue;
		float w, h;
		if (it->icon) fit_box(tex, bw, bh, &w, &h);
		else icon_dims(tex, fminf(bh, it->stock == tex_folder ? 92.0f : 84.0f), &w, &h);   /* the 64px stock icons stay small */
		if (it->icon && !it->icon_rect) {            /* no landscape art: frame the square icon like one */
			vita2d_draw_rectangle(ix - bw / 2 + 3, y - bh / 2 + 4, bw, bh, RGBA8(0, 0, 0, a * 25 / 100));
			vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, bh, RGBA8(18, 28, 38, a * 80 / 100));
			vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, 2, WHITE(a * 35 / 100));
		}
		draw_icon_wh(tex, ix, y, w, h, a);

		if (t < 0.5f) {                               /* selected row: label unless art covers it */
			int ta = (int)(a * (1.0f - clampf(pic_a * 1.6f, 0.0f, 1.0f)) * (1.0f - t * 2.0f));
			if (ta > 4) {
				float tx = ix + (it->icon ? bw / 2 + 24 : w / 2 + 29);      /* the small stock icons keep the label close */
				float maxw = SCREEN_W - 24.0f - tx;
				if (it->sub[0] && !it->icon) {                    /* like the other lists: title above the rule, subtitle below */
					ptext_vc_row(tx - text_bearing(PT_TITLE(28), it->title), y - 21, WHITE(ta), PT_TITLE(28), it->title, maxw, 1);
					ptext_vc_row(tx - text_bearing(PT_SUB(22), it->sub), y + 23, WHITE(ta * 8 / 10), PT_SUB(22), it->sub, maxw, 1);
					draw_rule(tx - 1, 948, y, ta);
				} else if (it->sub[0]) {
					ptext_vc_row(tx - text_bearing(PT_TITLE(28), it->title), y - 15, WHITE(ta), PT_TITLE(28), it->title, maxw, 1);
					ptext_vc_row(tx - text_bearing(PT_SUB(20), it->sub), y + 17, WHITE(ta * 7 / 10), PT_SUB(20), it->sub, maxw, 1);
					draw_rule(tx - 1, 948, y, ta);
				} else {
					ptext_vc_row(tx - text_bearing(PT_TITLE(28), it->title), y, WHITE(ta), PT_TITLE(28), it->title, maxw, 1);
				}
			}
		}
	}
}
