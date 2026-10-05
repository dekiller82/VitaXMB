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

static void draw_status(const SceDateTime *dt)
{
	char buf[32];
	int pct = scePowerGetBatteryLifePercent();
	const float cy = 26.0f;                       /* shared centre line of clock and battery */

	float bx = 908;
	int clock_left = 0;                           /* a battery picture as wide as the screen carries the clock at its left */
	if (pt_bat) {
		/* a theme's own battery: four frames in one picture, full to empty, drawn at twice its size where the theme puts it */
		int fw = vita2d_texture_get_width(pt_bat), fh = vita2d_texture_get_height(pt_bat) / 4;
		int frame = pct > 66 ? 0 : (pct > 33 ? 1 : (pct > 8 ? 2 : 3));
		float w = fw * 2.0f, h = fh * 2.0f;
		float x0 = pt_bat_x * 2.0f - w / 2.0f, y0 = pt_bat_y * 2.0f - h / 2.0f;
		vita2d_draw_texture_part_scale(pt_bat, x0, y0, 0, frame * fh, fw, fh, 2.0f, 2.0f);
		bx = x0;
		clock_left = fw >= 300;
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
	for (int k = 0; k < segs; k++)
		vita2d_draw_rectangle(bx + bw - 6 - 8 * (k + 1) - 2 * k + 2, by + 5, 8, bh - 10, line);
	}

	if (clock24) {
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d", dt->month, dt->day, dt->hour, dt->minute);
	} else {
		int h12 = dt->hour % 12 ? dt->hour % 12 : 12;
		snprintf(buf, sizeof(buf), "%d/%d %d:%02d %s", dt->month, dt->day, h12, dt->minute, dt->hour < 12 ? "AM" : "PM");
	}
	if (clock_left && pt_clock_w > 0.0f) ptext_right_vc(942.0f, cy, WHITE(240), 24, buf);       /* a clock with a text box: at the right edge */
	else if (clock_left) ptext_vc(56.0f, cy, WHITE(240), 24, buf);
	else ptext_right_vc(pt_bat ? 2.0f * (240.0f + pt_clock_x) - 10.0f : bx - 22, cy, WHITE(240), 24, buf);
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
static void draw_rule(float x0, float x1, float y, int la);

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
		vita2d_texture *tex = it->icon ? it->icon : pt_swap(it->stock);
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
					ptext_vc_fit(tx - text_bearing(28, it->title), y - 21, WHITE(ta), 28, it->title, maxw);
					ptext_vc_fit(tx - text_bearing(22, it->sub), y + 23, WHITE(ta * 8 / 10), 22, it->sub, maxw);
					draw_rule(tx - 1, 948, y, ta);
				} else if (it->sub[0]) {
					ptext_vc_fit(tx - text_bearing(28, it->title), y - 15, WHITE(ta), 28, it->title, maxw);
					ptext_vc_fit(tx - text_bearing(20, it->sub), y + 17, WHITE(ta * 7 / 10), 20, it->sub, maxw);
					draw_rule(tx - 1, 948, y, ta);
				} else {
					ptext_vc_fit(tx - text_bearing(28, it->title), y, WHITE(ta), 28, it->title, maxw);
				}
			}
		}
	}
}
