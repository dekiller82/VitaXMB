#pragma once

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
