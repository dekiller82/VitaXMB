#pragma once

static volatile unsigned icon_req_gen;               /* counts the icons asked for: the loader thread walks the rows only when it changed (thousands of retro games would make an idle walk cost) */

static void update_icons(int m, int keep_other)
{
	int uploads = 0;
	for (int k = 0; k < M_COUNT; k++) {
		Menu *mn = &menus[k];
		int visible = (k == m || k == keep_other) || (k < CAT_COUNT && k != CAT_SETTINGS && k != CAT_GAME);      /* the columns of placed apps load ahead, so they are ready when you get there */
		for (int i = 0; i < mn->count; i++) {
			Item *it = &mn->items[i];
			if (!it->icon_path[0]) continue;
			int keep = (k < CAT_COUNT && mn->count <= 48) ? 1 << 20 : (k < CAT_COUNT ? 20 : ICON_KEEP);      /* a column of placed apps keeps every icon loaded (up to 48 rows) */
			int near = visible && abs(i - mn->sel) <= keep;
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
				if (st == 0 && !it->icon && !it->icon_tried) { it->icon_tried = 1; it->load_state = 1; icon_req_gen++; }
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

/* The game lists show landscape tiles (the PSP's ICON0 shape, 16:9). An app that has only a square (or portrait) picture would sit in a dark box;
 * every such picture is instead turned into a landscape tile here, as it loads: its own colours blurred and dimmed to fill the tile, the
 * picture itself centred on top with the rounded corners the Vita's bubbles have. RGBA in, RGBA out (malloc), NULL when it cannot. */
#define TILE_W 320
#define TILE_H 178

static uint8_t *landscape_tile(const uint8_t *src, int w, int h)
{
	if (w < 8 || h < 8) return NULL;
	uint8_t *out = malloc((size_t)TILE_W * TILE_H * 4);
	if (!out) return NULL;
	/* background: the picture covering the whole tile, blurred (a box average of 16x16 pixel blocks, read back bilinearly), dimmed */
	enum { BS = 16, BW = TILE_W / BS, BH = (TILE_H + BS - 1) / BS };
	float blk[BH][BW][3];
	float cover = fmaxf((float)TILE_W / w, (float)TILE_H / h);
	for (int by = 0; by < BH; by++)
		for (int bx = 0; bx < BW; bx++) {
			float acc[3] = { 0, 0, 0 }, wsum = 0;
			for (int j = 0; j < BS; j += 4)
				for (int i = 0; i < BS; i += 4) {
					float u = ((bx * BS + i + 2) - TILE_W / 2.0f) / cover + w / 2.0f, v = ((by * BS + j + 2) - TILE_H / 2.0f) / cover + h / 2.0f;
					int x = (int)fminf(fmaxf(u, 0), w - 1), y = (int)fminf(fmaxf(v, 0), h - 1);
					const uint8_t *p = src + ((size_t)y * w + x) * 4;
					float a = p[3] / 255.0f;
					acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; wsum += a;
				}
			for (int c = 0; c < 3; c++) blk[by][bx][c] = wsum > 0 ? acc[c] / wsum : 24.0f;
		}
	/* the picture, as tall as 84% of the tile, centred */
	float fh = TILE_H * 0.84f, fw = fh * w / h;
	if (fw > TILE_W * 0.9f) { fw = TILE_W * 0.9f; fh = fw * h / w; }
	float fx0 = (TILE_W - fw) / 2.0f, fy0 = (TILE_H - fh) / 2.0f, rad = fminf(fw, fh) * 0.0937f;       /* the bubble's corner: 12 of 128 */
	for (int y = 0; y < TILE_H; y++)
		for (int x = 0; x < TILE_W; x++) {
			float gx = ((float)x + 0.5f) / BS - 0.5f, gy = ((float)y + 0.5f) / BS - 0.5f;
			int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
			float tx = gx - x0, ty = gy - y0;
			int x1 = x0 + 1 > BW - 1 ? BW - 1 : x0 + 1, y1 = y0 + 1 > BH - 1 ? BH - 1 : y0 + 1;
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			float col[3];
			for (int c = 0; c < 3; c++)
				col[c] = ((blk[y0][x0][c] * (1 - tx) + blk[y0][x1][c] * tx) * (1 - ty) + (blk[y1][x0][c] * (1 - tx) + blk[y1][x1][c] * tx) * ty) * 0.55f;
			/* inside the rounded rectangle of the picture? (signed distance, one pixel of antialiasing) */
			float px = fabsf(x + 0.5f - TILE_W / 2.0f) - (fw / 2.0f - rad), py = fabsf(y + 0.5f - TILE_H / 2.0f) - (fh / 2.0f - rad);
			float dx = fmaxf(px, 0), dy = fmaxf(py, 0);
			float sd = sqrtf(dx * dx + dy * dy) + fminf(fmaxf(px, py), 0) - rad;
			float cov = fminf(fmaxf(0.5f - sd, 0), 1);
			if (cov > 0) {
				float u = (x + 0.5f - fx0) * w / fw - 0.5f, v = (y + 0.5f - fy0) * h / fh - 0.5f;
				int sx0 = (int)floorf(u), sy0 = (int)floorf(v);
				float ux = u - sx0, vy = v - sy0;
				float pc[4] = { 0, 0, 0, 0 };
				for (int k = 0; k < 4; k++) {
					int sx = sx0 + (k & 1), sy = sy0 + (k >> 1);
					sx = sx < 0 ? 0 : (sx > w - 1 ? w - 1 : sx); sy = sy < 0 ? 0 : (sy > h - 1 ? h - 1 : sy);
					float wt = ((k & 1) ? ux : 1 - ux) * ((k >> 1) ? vy : 1 - vy);
					const uint8_t *p = src + ((size_t)sy * w + sx) * 4;
					float a = p[3] / 255.0f;
					pc[0] += p[0] * a * wt; pc[1] += p[1] * a * wt; pc[2] += p[2] * a * wt; pc[3] += a * wt;
				}
				float a = pc[3] * cov;
				for (int c = 0; c < 3; c++) col[c] = col[c] * (1 - a) + pc[c] * cov;
			}
			uint8_t *o = out + ((size_t)y * TILE_W + x) * 4;
			for (int c = 0; c < 3; c++) o[c] = (uint8_t)fminf(fmaxf(col[c] + 0.5f, 0), 255);
			o[3] = 255;
		}
	return out;
}

static void load_item_art(Item *it)
{
	int w = 0, h = 0, rect = 0;
	uint8_t *pix = NULL;
	if (it->rix) pix = retro_art(it, (it->tr & 8) ? 200 : 320, &w, &h);                  /* a retro game: RetroArch's thumbnail */
	else if (it->kind == KIND_APP) {
		resolve_meta(it);
		if (it->gate_path[0]) { pix = png_decode_scaled(it->gate_path, (it->tr & 8) ? 200 : 320, &w, &h); rect = pix != NULL; }
	}
	if (!pix && !it->rix) pix = png_decode_scaled(it->icon_path, (it->tr & 8) ? 200 : 320, &w, &h);
	if (!pix && !it->rix && it->icon_path2[0]) pix = png_decode_scaled(it->icon_path2, (it->tr & 8) ? 200 : 320, &w, &h);
	if (pix && w >= 8 && h >= 8 && (float)w / h < 1.5f) {              /* square or portrait art: a landscape tile like the rest of the list */
		uint8_t *tile = landscape_tile(pix, w, h);
		if (tile) { free(pix); pix = tile; w = TILE_W; h = TILE_H; rect = 1; }
	}
	it->pending_pix = pix;
	it->pending_w = w; it->pending_h = h;
	it->pending_rect = rect;
}

static volatile int loader_run = 1;

static int icon_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	unsigned seen = ~0u;
	while (loader_run) {
		if (loader_pause) { sceKernelDelayThread(2000); continue; }
		if (seen == icon_req_gen) { sceKernelDelayThread(3000); continue; }          /* nothing was asked for since the last scan found nothing */
		unsigned gen = icon_req_gen;
		Item *best = NULL;
		int bestd = 1 << 30;
		for (int k = 0; k < M_COUNT; k++) {                 /* the menus whose rows carry art (the game lists, and the apps placed in a column) */
			Menu *mn = &menus[k];
			for (int i = 0; i < mn->count; i++) {
				Item *it = &mn->items[i];
				if (it->load_state != 1) continue;
				int d = abs(i - mn->sel);                    /* nearest the cursor first */
				if (d < bestd) { bestd = d; best = it; }
			}
		}
		if (!best) { seen = gen; sceKernelDelayThread(3000); continue; }
		loader_busy = 1;
		best->load_state = 2;
		load_item_art(best);
		__sync_synchronize();
		best->load_state = 3;
		loader_busy = 0;
	}
	return 0;
}
