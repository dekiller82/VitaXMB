/*
 * gameboot.h - the PSP's game boot movie.
 *
 * The firmware plays flash0:/vsh/resource/gameboot.pmf when a game starts: a 480x272 H.264 clip of 75 frames (29.97 fps) whose sound is the
 * start-game jingle. Its picture stream is kept as assets/psp/gameboot.h264 (raw H.264, no container) and decoded here frame by frame while it is
 * shown, with the app's FFmpeg and on the calling thread, so nothing has to be opened or prepared in advance. The clip follows the jingle's own
 * position, so picture and sound stay together.
 */
#pragma once

#define GB_FPS (30000.0 / 1001.0)

static uint8_t *gb_data;
static int gb_size, gb_pos;
static AVCodecContext *gb_ctx;
static AVCodecParserContext *gb_parser;
static AVPacket *gb_pkt;
static AVFrame *gb_frame;
static struct SwsContext *gb_sws;
static vita2d_texture *gb_tex;
static int gb_shown = -1;                          /* the index of the frame in the texture, -1 for none yet */
static int gb_eof, gb_ok;
static Rco *gb_rco;                                /* opening_plugin.rco: the PSP logo that the plugin draws over the movie */
static RcoPlayer gb_anim;
static int gb_n_logo = -1;
static vita2d_texture *gb_psv;                     /* the Vita logo (assets/psp/psv_logo.png, 520x160), drawn where the PSP picture goes */
static uint64_t gb_us, gb_max_us, gb_calls;               /* time spent decoding, for the log */

static void gb_close(void)
{
	if (gb_sws) { sws_freeContext(gb_sws); gb_sws = NULL; }
	if (gb_frame) av_frame_free(&gb_frame);
	if (gb_pkt) av_packet_free(&gb_pkt);
	if (gb_parser) { av_parser_close(gb_parser); gb_parser = NULL; }
	if (gb_ctx) avcodec_free_context(&gb_ctx);
	free(gb_data); gb_data = NULL;
	if (gb_tex) { vita2d_wait_rendering_done(); vita2d_free_texture(gb_tex); gb_tex = NULL; }
	if (gb_psv) { vita2d_free_texture(gb_psv); gb_psv = NULL; }
	gb_ok = 0;
}

static int gb_open(void)
{
	if (gb_ok) return 1;
	gb_close();
	gb_pos = 0; gb_shown = -1; gb_eof = 0;
	SceUID fd = sceIoOpen("app0:assets/psp/gameboot.h264", SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	gb_size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	gb_data = gb_size > 0 ? malloc((size_t)gb_size + AV_INPUT_BUFFER_PADDING_SIZE) : NULL;
	int ok = gb_data && sceIoRead(fd, gb_data, gb_size) == gb_size;
	sceIoClose(fd);
	if (!ok) { gb_close(); return 0; }
	memset(gb_data + gb_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
	gb_ctx = c ? avcodec_alloc_context3(c) : NULL;
	gb_parser = av_parser_init(AV_CODEC_ID_H264);
	gb_pkt = av_packet_alloc();
	gb_frame = av_frame_alloc();
	if (!gb_ctx || !gb_parser || !gb_pkt || !gb_frame) { gb_close(); return 0; }
	gb_ctx->thread_count = 1;
	if (avcodec_open2(gb_ctx, c, NULL) < 0) { gb_close(); return 0; }
	gb_tex = vita2d_create_empty_texture(480, 272);
	if (!gb_tex) { gb_close(); return 0; }
	vita2d_texture_set_filters(gb_tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	/* the plugin's anim_gameboot_movie brings the PSP logo (plane psp_logo, picture tex_psp_logo: the letters and "PlayStation Portable") in over the movie:
	   a recolour to full at 0.3 s that takes 0.2 s, and it stays to the end */
	size_t rn = 0;
	uint8_t *rd = boot_read_file("app0:assets/psp/opening_plugin.rco", &rn);
	gb_rco = rd ? rco_open(rd, rn) : NULL;
	if (gb_rco) {
		gb_n_logo = rco_find(gb_rco, "psp_logo");
		if (gb_n_logo >= 0) { RcoNode *lg = &gb_rco->nodes[gb_n_logo]; lg->ca = lg->a; lg->a = 1.0f; }      /* the plane starts invisible (alpha 0) and the recolour's alpha brings it in: the player keeps that alpha in ca */
		if (!rco_play_start(&gb_anim, gb_rco, "anim_gameboot_movie", NULL, NULL)) gb_n_logo = -1;
	}
	if (boot_logo) gb_psv = vita2d_load_PNG_file("app0:assets/psp/psv_logo.png");
	gb_ok = 1;
	return 1;
}

/* decodes the next picture of the stream into gb_frame; 1 when there is one */
static int gb_decode_next(void)
{
	for (;;) {
		int r = avcodec_receive_frame(gb_ctx, gb_frame);
		if (r >= 0) return 1;
		if (r != AVERROR(EAGAIN)) return 0;                        /* the end of the stream, or an error */
		if (gb_pos >= gb_size) {
			if (gb_eof) return 0;
			gb_eof = 1;
			avcodec_send_packet(gb_ctx, NULL);                     /* flush */
			continue;
		}
		uint8_t *out = NULL; int out_n = 0;
		int used = av_parser_parse2(gb_parser, gb_ctx, &out, &out_n, gb_data + gb_pos, gb_size - gb_pos, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (used < 0) return 0;
		gb_pos += used;
		if (out_n > 0) {
			gb_pkt->data = out; gb_pkt->size = out_n;
			avcodec_send_packet(gb_ctx, gb_pkt);
		}
	}
}

/* brings the picture to the one that belongs to t seconds into the clip; at most two decodes per call, so a slow frame cannot stall the menu */
static void gb_seek_time(double t)
{
	if (!gb_ok) return;
	uint64_t t0 = sceKernelGetProcessTimeWide();
	if (gb_n_logo >= 0) rco_play_step(&gb_anim, (float)(t * 1000.0));
	int want = (int)(t * GB_FPS);
	if (want < 0) want = 0;
	for (int n = 0; n < 2 && gb_shown < want && !(gb_eof && gb_pos >= gb_size); n++) {
		if (!gb_decode_next()) { gb_eof = 1; gb_pos = gb_size; break; }
		if (!gb_sws) gb_sws = sws_getContext(gb_frame->width, gb_frame->height, (enum AVPixelFormat)gb_frame->format, 480, 272, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		if (gb_sws) {
			uint8_t *dst[4] = { vita2d_texture_get_datap(gb_tex), NULL, NULL, NULL };
			int dstride[4] = { vita2d_texture_get_stride(gb_tex), 0, 0, 0 };
			sws_scale(gb_sws, (const uint8_t *const *)gb_frame->data, gb_frame->linesize, 0, gb_frame->height, dst, dstride);
		}
		gb_shown++;
	}
	uint64_t d = sceKernelGetProcessTimeWide() - t0;
	gb_us += d; gb_calls++;
	if (d > gb_max_us) gb_max_us = d;
}

static void gb_report(void)
{
	if (gb_calls) xlog("gameboot: %d pictures, %u calls, %u us decoding in all, worst call %u us\n", gb_shown + 1, (unsigned)gb_calls, (unsigned)gb_us, (unsigned)gb_max_us);
	gb_us = gb_max_us = gb_calls = 0;
}

/* true once the last picture has been reached */
static int gb_finished(void)
{
	return !gb_ok || (gb_eof && gb_pos >= gb_size);
}

static void gb_draw(float alpha)
{
	if (!gb_ok || gb_shown < 0 || alpha <= 0.0f) return;
	if (alpha > 1.0f) alpha = 1.0f;
	vita2d_draw_texture_tint_scale(gb_tex, 0, 0, SCREEN_W / 480.0f, SCREEN_H / 272.0f, RGBA8(255, 255, 255, (int)(255 * alpha)));
	if (gb_n_logo < 0) return;
	if (!gb_psv) { rco_draw_plane(gb_rco, gb_n_logo, alpha, 0); return; }
	RcoNode *n = &gb_rco->nodes[gb_n_logo];                                       /* same place, size and colour as the PSP picture */
	float a = fminf(n->a * n->ca * alpha, 1.0f);
	if (a <= 0.003f) return;
	float dw = (n->w > 0.0f ? n->w : 260.0f) * n->sx * 2.0f, dh = (n->h > 0.0f ? n->h : 80.0f) * n->sy * 2.0f;
	float px = (240.0f + n->x) * 2.0f - dw / 2.0f, py = (136.0f - n->y) * 2.0f - dh / 2.0f;
	int cr = (int)(255.0f * fminf(fmaxf(n->r, 0.0f), 1.0f)), cg = (int)(255.0f * fminf(fmaxf(n->g, 0.0f), 1.0f)), cb = (int)(255.0f * fminf(fmaxf(n->b, 0.0f), 1.0f));
	vita2d_draw_texture_tint_scale(gb_psv, px, py, dw / vita2d_texture_get_width(gb_psv), dh / vita2d_texture_get_height(gb_psv), RGBA8(cr, cg, cb, (int)(255.0f * a)));
}
