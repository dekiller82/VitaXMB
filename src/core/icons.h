#pragma once

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
		for (int k = M_MEMCARD; k <= M_FOLDER; k++) {        /* the only menus whose rows carry art */
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
