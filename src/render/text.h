#pragma once

/* ------------------------------------------------------------------ */
/* Text: FreeType glyphs (light hinting) in a padded atlas, with a     */
/* pre-blurred shadow bitmap per glyph. Font: FOT-NewRodin Pro DB.     */
/* ------------------------------------------------------------------ */

#define ATLAS_W 2048
#define ATLAS_H 1024
#define GLYPH_SLOTS 4096        /* power of two */
#define GLYPH_PAD 2
#define SHADOW_PAD 8

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

/* A theme changed the font: drop every cached glyph at the next frame start. */
static void text_cache_flush(void) { atlas_dirty = 1; }

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

static void box_blur_r(uint8_t *img, uint8_t *tmp, int w, int h, int r)
{
	const int n = 2 * r + 1;
	for (int y = 0; y < h; y++) {                       /* running sums: the cost does not grow with the radius */
		const uint8_t *in = img + y * w;
		uint8_t *out = tmp + y * w;
		int sum = 0;
		for (int k = 0; k <= r && k < w; k++) sum += in[k];
		for (int x = 0; x < w; x++) {
			out[x] = (uint8_t)(sum / n);
			if (x + r + 1 < w) sum += in[x + r + 1];
			if (x - r >= 0) sum -= in[x - r];
		}
	}
	for (int x = 0; x < w; x++) {
		int sum = 0;
		for (int k = 0; k <= r && k < h; k++) sum += tmp[k * w + x];
		for (int y = 0; y < h; y++) {
			img[y * w + x] = (uint8_t)(sum / n);
			if (y + r + 1 < h) sum += tmp[(y + r + 1) * w + x];
			if (y - r >= 0) sum -= tmp[(y - r) * w + x];
		}
	}
}
static void box_blur(uint8_t *img, uint8_t *tmp, int w, int h) { box_blur_r(img, tmp, w, h, 1); }

/* The PSP's text shadow (measured on an Adrenaline capture): a wide soft glow, mostly below and right of the letters. Black: the dark blue seen in the capture was only measured on the blue sky. */
static int   sh_rad = 3, sh_n = 3;                      /* box blur radius and passes (about 3.5 px sigma) */
static float sh_dim = 0.6f, sh_alpha = 0.5f;            /* strength of the bitmap, opacity of the colour */
static int   sh_dx = 2, sh_dy = 2;                      /* offset, screen pixels */
static int   sh_col[3] = { 0, 0, 0 };
static float txt_soft = 0.5f;                           /* how much the letters themselves are softened (the PSP's picture is a 2x upscale) */

static void make_shadow(uint8_t *img, uint8_t *tmp, int pw, int ph)
{
	for (int i = 0; i < sh_n; i++) box_blur_r(img, tmp, pw, ph, sh_rad);
	for (int i = 0; i < pw * ph; i++) { int v = (int)(img[i] * sh_dim * 3.0f); img[i] = (uint8_t)(v > 255 ? 255 : v); }   /* the blur thins the glow: bring it back up */
}

/* a 3x3 smoothing of a glyph bitmap (copy), mixed with the original by txt_soft */
static void soften_glyph(uint8_t *dst, const uint8_t *src, int w, int h, int pitch)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int s = 0, wsum = 0;
			for (int j = -1; j <= 1; j++)
				for (int i = -1; i <= 1; i++) {
					int xx = x + i, yy = y + j, wt = (i == 0 ? 2 : 1) * (j == 0 ? 2 : 1);
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					s += src[yy * pitch + xx] * wt; wsum += wt;
				}
			float v = src[y * pitch + x] * (1.0f - txt_soft) + (float)s / (float)wsum * txt_soft;
			dst[y * w + x] = (uint8_t)(v > 255.0f ? 255 : v);
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

			if (cp & 0x100000) {                 /* a glyph of the theme's PGF font */
				PtGlyph pg;
				g->key = key;
				g->aw = g->ah = g->sw = g->sh = 0;
				g->adv = 0;
				if (!pt_font || !pt_glyph(pt_font, cp & 0xFFFFF, &pg)) return g;
				float sc = pt_font_scale(size);
				g->adv = pg.adv / 64.0f * sc;
				uint8_t *src = pt_glyph_bitmap(pt_font, &pg);
				if (!src) return g;
				/* place the scaled bitmap with its top-left at (left, -top) from the pen, shifted by sub/4 px */
				float x0 = pg.left * sc + sub * 0.25f, y0 = -pg.top * sc;
				int ox0 = (int)floorf(x0), oy0 = (int)floorf(y0);
				int w = (int)ceilf(pg.w * sc + (x0 - ox0)) + 1, hh = (int)ceilf(pg.h * sc + (y0 - oy0)) + 1;
				uint8_t *dst = calloc((size_t)w * hh, 1);
				if (!dst) { free(src); return g; }
				for (int j = 0; j < hh; j++)
					for (int i = 0; i < w; i++) {
						float u = (ox0 + i + 0.5f - x0) / sc - 0.5f, v = (oy0 + j + 0.5f - y0) / sc - 0.5f;
						int iu = (int)floorf(u), iv = (int)floorf(v);
						float fu = u - iu, fv = v - iv, acc = 0;
						for (int dy = 0; dy < 2; dy++)
							for (int dx = 0; dx < 2; dx++) {
								int xx = iu + dx, yy = iv + dy;
								float wgt = (dx ? fu : 1 - fu) * (dy ? fv : 1 - fv);
								if (xx >= 0 && yy >= 0 && xx < pg.w && yy < pg.h) acc += wgt * src[yy * pg.w + xx];
							}
						dst[j * w + i] = (uint8_t)(acc > 255 ? 255 : acc + 0.5f);
					}
				free(src);
				int pw = w + SHADOW_PAD * 2, ph = hh + SHADOW_PAD * 2;
				int ax, ay, sx, sy;
				if (!atlas_alloc(w + GLYPH_PAD * 2, hh + GLYPH_PAD * 2, &ax, &ay) ||
				    !atlas_alloc(pw + GLYPH_PAD * 2, ph + GLYPH_PAD * 2, &sx, &sy)) {
					free(dst);
					g->key = 0;
					atlas_dirty = 1;
					return NULL;
				}
				ax += GLYPH_PAD; ay += GLYPH_PAD; sx += GLYPH_PAD; sy += GLYPH_PAD;
				atlas_put(ax, ay, w, hh, dst, w);
				uint8_t *img = calloc(pw * ph, 1), *tmp = calloc(pw * ph, 1);
				if (img && tmp) {
					for (int j = 0; j < hh; j++) memcpy(img + (j + SHADOW_PAD) * pw + SHADOW_PAD, dst + j * w, w);
					make_shadow(img, tmp, pw, ph);
					atlas_put(sx, sy, pw, ph, img, pw);
					g->sx = sx; g->sy = sy; g->sw = pw; g->sh = ph;
					g->sleft = ox0 - SHADOW_PAD;
					g->stop = -oy0 + SHADOW_PAD;
				}
				free(img); free(tmp); free(dst);
				g->ax = ax; g->ay = ay; g->aw = w; g->ah = hh;
				g->left = ox0; g->top = -oy0;
				return g;
			}
			/* The shadow is soft, so a quarter-pixel shift of the letter does not change it: only the glyph at phase 0 builds it, the other phases share it. */
			Glyph *g0 = NULL;
			if (sub) {
				g->key = key;                                   /* hold this slot while the phase-0 glyph is made */
				g->aw = g->ah = g->sw = g->sh = 0;
				g->adv = 0;
				g0 = glyph_get(cp, size, 0);
				if (!g0) { g->key = 0; return NULL; }
			}
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
				if (FT_Load_Glyph(ft_face, idx, flags[text_hint_mode & 3] | FT_LOAD_NO_BITMAP)) {
					g->key = 0;
					return NULL;
				}
			}
			/* shift the outline by sub/4 px so glyphs sit at fractional pen positions */
			if (sub && ft_face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
				FT_Outline_Translate(&ft_face->glyph->outline, sub * 16, 0);
			if (FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL)) {
				g->key = 0;
				return NULL;
			}
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
				    (!g0 && !atlas_alloc(pw + GLYPH_PAD * 2, ph + GLYPH_PAD * 2, &sx, &sy))) {
					g->key = 0;
					atlas_dirty = 1;
					return NULL;
				}
				ox += GLYPH_PAD; oy += GLYPH_PAD; sx += GLYPH_PAD; sy += GLYPH_PAD;
				if (txt_soft > 0.001f) {
					uint8_t *soft = malloc((size_t)w * hh);
					if (soft) { soften_glyph(soft, bm->buffer, w, hh, bm->pitch); atlas_put(ox, oy, w, hh, soft, w); free(soft); }
					else atlas_put(ox, oy, w, hh, bm->buffer, bm->pitch);
				} else atlas_put(ox, oy, w, hh, bm->buffer, bm->pitch);

				/* shadow: padded copy, blurred (shared with the other phases of the same letter) */
				uint8_t *img = g0 ? NULL : calloc(pw * ph, 1), *tmp = g0 ? NULL : calloc(pw * ph, 1);
				if (g0) {
					g->sx = g0->sx; g->sy = g0->sy; g->sw = g0->sw; g->sh = g0->sh; g->sleft = g0->sleft; g->stop = g0->stop;
				} else if (img && tmp) {
					for (int j = 0; j < hh; j++)
						memcpy(img + (j + SHADOW_PAD) * pw + SHADOW_PAD, bm->buffer + j * bm->pitch, w);
					make_shadow(img, tmp, pw, ph);
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
	if (pt_font && *s) {                 /* the theme's bitmap font, if it has every character */
		int n = 0, ok = 1;
		const unsigned char *u = (const unsigned char *)s;
		while (*u && n < MAX_SHAPED) {
			uint32_t cp;
			if (u[0] < 0x80) { cp = u[0]; u += 1; }
			else if ((u[0] & 0xE0) == 0xC0 && u[1]) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
			else if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
			else { ok = 0; break; }
			float adv = pt_font_adv(cp, size);
			if (adv < 0) { ok = 0; break; }
			out[n].gid = 0x100000u | cp; out[n].adv = adv; out[n].xoff = 0; n++;
		}
		if (ok && n) return n;
	}
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

/* Builds the common letters (ASCII, the sizes the menus use, the four sub-pixel phases) a few at a time, e.g. during the boot animation. */
static void text_prewarm(int steps)
{
	static int idx;
	static const unsigned int sizes[3] = { 28, 22, 24 };
	if (!atlas || pt_font) return;
	while (steps-- > 0 && idx < 3 * 95) {                  /* phase 0 only: it owns the shadow; the other phases are cheap and the atlas has to stay roomy */
		int si = idx / 95, ci = idx % 95, ph = 0;
		idx++;
		char s[2] = { (char)(32 + ci), 0 };
		Shaped sh[MAX_SHAPED];
		if (text_shape(sizes[si], s, sh) > 0) glyph_get(sh[0].gid, sizes[si], ph);
	}
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
	unsigned int shadow = RGBA8(sh_col[0], sh_col[1], sh_col[2], (unsigned int)(a * sh_alpha));
	const float by = y;                 /* fractional on purpose: text glides with the icons */
	const int sdx = sh_dx, sdy = sh_dy;

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
	ptext(x, cy + floorf(size * (pt_font ? pt_font->cap / 40.0f : 0.391f) + 0.5f), col, size, s);
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

/* Size to draw an icon at: stock icons are square boxes; a theme's keep their shape and are not
 * magnified past 2x (the Vita screen is twice the PSP's). */
static void icon_dims(vita2d_texture *t, float size, float *w, float *h)
{
	float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
	if (!pt_owned(t)) { *w = *h = size; return; }
	/* A theme's picture is drawn at its own size, doubled for the Vita screen; 120 is the full (selected) size, smaller boxes scale it down. */
	float s = 2.0f * size / 120.0f;
	if (s > 2.6f) s = 2.6f;
	*w = tw * s; *h = th * s;
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
	if (!t) return;
	float w, h;
	icon_dims(t, size, &w, &h);
	draw_icon_wh(t, cx, cy, w, h, a);
}
