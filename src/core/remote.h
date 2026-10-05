#pragma once

/* ---- debug remote: ux0:data/VitaXMB/remote.txt holds space-separated commands, e.g.
 *   "right right cross w30 shot:folder"   (w<N> waits N frames; shot:<name> writes
 *   ux0:data/VitaXMB/<name>.png). The file is deleted once read. Used with tools/remote.py. */

#define REMOTE_FILE CONFIG_DIR "/remote.txt"
static int test_page;
static char rc_cmd[64][100];
static int  rc_n, rc_i, rc_wait;
static char rc_shot[48];
static char rc_uri[128];
static char rc_name[40];                 /* debug builds: stands in for typing a folder name */

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
	if (strncmp(c, "name:", 5) == 0) { snprintf(rc_name, sizeof(rc_name), "%s", c + 5); return 0; }
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
enum { ACT_NONE, ACT_EXIT_ASK, ACT_EXIT_DO, ACT_ARTDEC_ASK, ACT_ARTDEC_DO, ACT_URI, ACT_START, ACT_INFO, ACT_REFRESH,
       ACT_NEWFOLDER, ACT_SELECT, ACT_RENAME, ACT_DELFOLDER_ASK, ACT_DELFOLDER_DO, ACT_REMOVE };

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
	if (m == M_MEMCARD) return it->kind == KIND_APP || it->kind == KIND_FOLDER;
	if (m == M_FOLDER) return 1;
	return m == M_SAVES && it->id[0];
}
