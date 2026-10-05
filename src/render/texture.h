#pragma once

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
