#pragma once

/* ------------------------------------------------------------------ */
/* PSP games: the animated icon (ICON1.PMF) and the sound (SND0.AT3)   */
/* ------------------------------------------------------------------ */

/* On a PSP the game list shows more than the game's icon: after a moment on a game its ICON1.PMF, a little film, plays in place of the icon, and
 * its SND0.AT3 plays as music until the cursor moves away. A PSP game on the Vita is a bubble that Adrenaline's tools make: the bubble's
 * data/boot.bin names the game's file (an ISO / CSO image, or an EBOOT.PBP) and the two files are inside it. This reads them out of the image
 * (PBP header, or the ISO 9660 directory of PSP_GAME; CSO blocks are inflated), plays the film with the app's FFmpeg (H.264 in a PSMF's MPEG
 * program stream) and the sound through the mixer's own channel (sound.h, bgm_ring). SND0.AT3 is ATRAC3 / ATRAC3plus in a RIFF file: it needs
 * the FFmpeg libraries of tools/build_ffmpeg.sh with the atrac3 and atrac3p decoders; without them the film still plays, silently.
 *
 * A worker thread (pthread: the app's FFmpeg does not run on native threads) reads the files and decodes the sound; the main thread decodes
 * the film's pictures, a few per frame, since they are 144 x 80. */

#define PM_MAX_FILE (8 * 1024 * 1024)

/* ---- the film: a PSMF (MPEG-PS with H.264) ---- */

static uint8_t *pm_ps;                      /* the video elementary stream of the file (Annex B H.264) */
static size_t pm_ps_n, pm_ps_pos;
static AVCodecContext *pm_vctx;
static AVCodecParserContext *pm_vpar;
static AVFrame *pm_vframe;
static AVPacket *pm_vpkt;
static struct SwsContext *pm_sws;
static vita2d_texture *pm_tex[2];
static int pm_tex_w[2], pm_tex_h[2], pm_cur_tex = -1;
static int pm_shown;                        /* the number of pictures shown this round */
static int pm_flushing;
static double pm_fps = 30000.0 / 1001.0;
static uint64_t pm_t0;                      /* when this round of the film began */
static const Item *pm_icon_item;            /* the game the film belongs to */

static void pm_film_free(void)
{
	if (pm_sws) { sws_freeContext(pm_sws); pm_sws = NULL; }
	if (pm_vframe) av_frame_free(&pm_vframe);
	if (pm_vpkt) av_packet_free(&pm_vpkt);
	if (pm_vpar) { av_parser_close(pm_vpar); pm_vpar = NULL; }
	if (pm_vctx) avcodec_free_context(&pm_vctx);
	free(pm_ps); pm_ps = NULL;
	pm_ps_n = pm_ps_pos = 0; pm_shown = 0; pm_cur_tex = -1; pm_flushing = 0;
	for (int i = 0; i < 2; i++) if (pm_tex[i]) { defer_free(pm_tex[i]); pm_tex[i] = NULL; }                 /* the GPU may still be drawing with them */
}

/* takes the video of a PSMF out of its MPEG program stream: the payloads of the video PES packets, one after the other */
static int pm_film_parse(const uint8_t *d, size_t n)
{
	size_t pos = 0;
	if (n > 16 && !memcmp(d, "PSMF", 4)) {
		pos = ((size_t)d[8] << 24) | (d[9] << 16) | (d[10] << 8) | d[11];
		if (pos < 0x100 || pos >= n) pos = 0x800;
	}
	pm_ps = malloc(n + AV_INPUT_BUFFER_PADDING_SIZE);
	if (!pm_ps) return 0;
	size_t vn = 0;
	while (pos + 6 <= n) {
		if (d[pos] || d[pos + 1] || d[pos + 2] != 1) { pos++; continue; }
		uint8_t code = d[pos + 3];
		if (code == 0xBA) {                                                  /* pack header */
			pos += (d[pos + 4] >> 6) == 1 ? 14 + (d[pos + 13] & 7) : 12;
			continue;
		}
		if (code < 0xBA) { pos += 4; continue; }
		size_t len = ((size_t)d[pos + 4] << 8) | d[pos + 5];
		if (code >= 0xE0 && code <= 0xEF && pos + 9 <= n && len >= 3) {
			size_t pl = pos + 9 + d[pos + 8], end = len ? pos + 6 + len : n;
			if (end > n) end = n;
			if (pl <= end) { memcpy(pm_ps + vn, d + pl, end - pl); vn += end - pl; }
		}
		if (!len) break;                                                     /* an unbounded packet runs to the end */
		pos += 6 + len;
	}
	memset(pm_ps + vn, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	pm_ps_n = vn;
	return vn > 64;
}

static int pm_film_open(uint8_t *file, size_t n)
{
	pm_film_free();
	int ok = pm_film_parse(file, n);
	free(file);
	if (!ok) { pm_film_free(); return 0; }
	const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
	pm_vctx = c ? avcodec_alloc_context3(c) : NULL;
	pm_vpar = av_parser_init(AV_CODEC_ID_H264);
	pm_vframe = av_frame_alloc();
	pm_vpkt = av_packet_alloc();
	if (!pm_vctx || !pm_vpar || !pm_vframe || !pm_vpkt) { pm_film_free(); return 0; }
	pm_vctx->thread_count = 1;
	if (avcodec_open2(pm_vctx, c, NULL) < 0) { pm_film_free(); return 0; }
	pm_t0 = sceKernelGetProcessTimeWide();
	return 1;
}

/* decodes the next picture of the stream into pm_vframe: 1 when there is one, 0 at the end of the stream */
static int pm_film_decode(void)
{
	for (;;) {
		int r = avcodec_receive_frame(pm_vctx, pm_vframe);
		if (r >= 0) return 1;
		if (r != AVERROR(EAGAIN)) return 0;
		if (pm_ps_pos >= pm_ps_n) {
			if (pm_flushing) return 0;
			pm_flushing = 1;
			avcodec_send_packet(pm_vctx, NULL);
			continue;
		}
		uint8_t *out = NULL; int out_n = 0;
		int used = av_parser_parse2(pm_vpar, pm_vctx, &out, &out_n, pm_ps + pm_ps_pos, (int)(pm_ps_n - pm_ps_pos), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (used < 0) return 0;
		pm_ps_pos += (size_t)used;
		if (out_n > 0) {
			pm_vpkt->data = out; pm_vpkt->size = out_n;
			avcodec_send_packet(pm_vctx, pm_vpkt);
		}
	}
}

/* the picture that belongs to the time since the film began, a few decodes at most per call; at the end it starts again */
static void pm_film_update(void)
{
	if (!pm_vctx) return;
	if (pm_vctx->framerate.num > 0 && pm_vctx->framerate.den > 0) {
		double f = (double)pm_vctx->framerate.num / pm_vctx->framerate.den;
		if (f > 1.0 && f < 61.0) pm_fps = f;
	}
	int want = (int)((double)(sceKernelGetProcessTimeWide() - pm_t0) / 1000000.0 * pm_fps);
	for (int n = 0; n < 3 && pm_shown <= want; n++) {
		if (!pm_film_decode()) {                                        /* the end: round again */
			avcodec_flush_buffers(pm_vctx);
			av_parser_close(pm_vpar);
			pm_vpar = av_parser_init(AV_CODEC_ID_H264);
			pm_ps_pos = 0; pm_shown = 0; pm_flushing = 0;
			pm_t0 = sceKernelGetProcessTimeWide();
			return;
		}
		int w = pm_vframe->width, h = pm_vframe->height, k = pm_cur_tex == 0 ? 1 : 0;
		pm_shown++;
		if (w < 16 || h < 16 || w > 480 || h > 272) { av_frame_unref(pm_vframe); continue; }
		if (!pm_tex[k] || pm_tex_w[k] != w || pm_tex_h[k] != h) {
			xlog("psp film: %s %dx%d (coded %dx%d, sample aspect %d:%d, crop %d/%d/%d/%d, format %d)\n", pm_icon_item ? pm_icon_item->title : "?", w, h, pm_vctx->coded_width, pm_vctx->coded_height,
			     pm_vframe->sample_aspect_ratio.num, pm_vframe->sample_aspect_ratio.den, (int)pm_vframe->crop_left, (int)pm_vframe->crop_right, (int)pm_vframe->crop_top, (int)pm_vframe->crop_bottom, pm_vframe->format);
			if (pm_tex[k]) defer_free(pm_tex[k]);
			pm_tex[k] = vita2d_create_empty_texture(w, h);
			pm_tex_w[k] = w; pm_tex_h[k] = h;
			if (pm_tex[k]) vita2d_texture_set_filters(pm_tex[k], SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		}
		pm_sws = pm_tex[k] ? sws_getCachedContext(pm_sws, w, h, (enum AVPixelFormat)pm_vframe->format, w, h, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL) : NULL;
		if (pm_sws) {
			uint8_t *dst[4] = { vita2d_texture_get_datap(pm_tex[k]), NULL, NULL, NULL };
			int ds[4] = { vita2d_texture_get_stride(pm_tex[k]), 0, 0, 0 };
			sws_scale(pm_sws, (const uint8_t *const *)pm_vframe->data, pm_vframe->linesize, 0, h, dst, ds);
			pm_cur_tex = k;
		}
		av_frame_unref(pm_vframe);
	}
}

/* ---- the sound: SND0.AT3 through bgm_ring ---- */

typedef struct { const uint8_t *d; size_t n, pos; } PmMem;

static int pm_mem_read(void *op, uint8_t *buf, int sz)
{
	PmMem *m = op;
	size_t left = m->n - m->pos;
	if (!left) return AVERROR_EOF;
	if ((size_t)sz > left) sz = (int)left;
	memcpy(buf, m->d + m->pos, (size_t)sz);
	m->pos += (size_t)sz;
	return sz;
}

static int64_t pm_mem_seek(void *op, int64_t off, int whence)
{
	PmMem *m = op;
	if (whence == AVSEEK_SIZE) return (int64_t)m->n;
	int64_t np = whence == SEEK_SET ? off : (whence == SEEK_CUR ? (int64_t)m->pos + off : (int64_t)m->n + off);
	if (np < 0 || (size_t)np > m->n) return -1;
	m->pos = (size_t)np;
	return np;
}

static volatile int pm_gen;                 /* the game the worker is for: a new number asks it to stop and take the next request */
static char pm_req_id[24];
static char pm_req_path[256];                 /* a game that has no bubble: its file */

static int pm_game_path(const char *id, char *out, size_t n)
{
	if (pm_req_path[0]) { snprintf(out, n, "%s", pm_req_path); return 1; }
	return pm_bubble_game(id, out, n);
}
static volatile int pm_req_gen;

static int pm_stopped(int gen) { return pm_gen != gen; }

static uint8_t *volatile pm_film_in;          /* the worker's ICON1.PMF, for the main thread to take */
static size_t pm_film_in_n;
static volatile int pm_film_in_gen;

static void pm_bgm_push(const int16_t *pcm, int frames, int gen)
{
	while (frames > 0 && !pm_stopped(gen)) {
		unsigned used = bgm_w - bgm_r, room = BGM_RING - used;
		if (room < 256) { sceKernelDelayThread(8000); continue; }
		unsigned n = (unsigned)frames < room ? (unsigned)frames : room;
		for (unsigned i = 0; i < n; i++) {
			bgm_ring[((bgm_w + i) & (BGM_RING - 1)) * 2] = pcm[i * 2];
			bgm_ring[((bgm_w + i) & (BGM_RING - 1)) * 2 + 1] = pcm[i * 2 + 1];
		}
		__sync_synchronize();
		bgm_w += n;
		pcm += n * 2; frames -= (int)n;
	}
}

/* decodes and plays the file, round and round, until the game changes */
static void pm_sound_play(uint8_t *file, size_t n, int gen)
{
	PmMem mem = { file, n, 0 };
	while (!pm_stopped(gen)) {
		mem.pos = 0;
		AVFormatContext *fc = avformat_alloc_context();
		uint8_t *iob = av_malloc(32768);
		AVIOContext *io = fc && iob ? avio_alloc_context(iob, 32768, 0, &mem, pm_mem_read, NULL, pm_mem_seek) : NULL;
		if (!io) { av_free(iob); avformat_free_context(fc); return; }
		fc->pb = io;
		int ok = avformat_open_input(&fc, "snd0.at3", NULL, NULL) >= 0 && avformat_find_stream_info(fc, NULL) >= 0;
		int si = ok ? av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0) : -1;
		const AVCodec *c = si >= 0 ? avcodec_find_decoder(fc->streams[si]->codecpar->codec_id) : NULL;
		AVCodecContext *cc = c ? avcodec_alloc_context3(c) : NULL;
		if (!cc || avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) < 0 || avcodec_open2(cc, c, NULL) < 0) {
			xlog("psp sound: no decoder for SND0.AT3 (%s)\n", c ? c->name : "codec missing: rebuild FFmpeg with tools/build_ffmpeg.sh");
			avcodec_free_context(&cc);
			if (fc) avformat_close_input(&fc);
			av_free(io->buffer); avio_context_free(&io);
			return;
		}
		AVPacket *pk = av_packet_alloc();
		AVFrame *fr = av_frame_alloc();
		struct SwrContext *swr = NULL;
		static int16_t out[8192 * 2];
		int got_any = 0;
		while (!pm_stopped(gen) && av_read_frame(fc, pk) >= 0) {
			if (pk->stream_index == si && avcodec_send_packet(cc, pk) >= 0) {
				while (!pm_stopped(gen) && avcodec_receive_frame(cc, fr) >= 0) {
					if (!swr) {
						AVChannelLayout lay;
						av_channel_layout_default(&lay, 2);
						if (swr_alloc_set_opts2(&swr, &lay, AV_SAMPLE_FMT_S16, 48000, &fr->ch_layout, (enum AVSampleFormat)fr->format, fr->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) { swr_free(&swr); av_frame_unref(fr); break; }
					}
					uint8_t *op[1] = { (uint8_t *)out };
					int m = swr_convert(swr, op, 8192, (const uint8_t **)fr->extended_data, fr->nb_samples);
					if (m > 0) { pm_bgm_push(out, m, gen); got_any = 1; }
					av_frame_unref(fr);
				}
			}
			av_packet_unref(pk);
		}
		swr_free(&swr);
		av_frame_free(&fr); av_packet_free(&pk);
		avcodec_free_context(&cc);
		avformat_close_input(&fc);
		av_free(io->buffer); avio_context_free(&io);
		if (!got_any) return;
	}
}

static void *pm_worker(void *arg)
{
	(void)arg;
	int done = 0;
	for (;;) {
		while (pm_req_gen == done) sceKernelDelayThread(20000);
		int gen = pm_req_gen;
		done = gen;
		char id[24];
		snprintf(id, sizeof(id), "%s", pm_req_id);
		if (!id[0] || pm_stopped(gen)) continue;
		char game[256];
		PmSrc src;
		if (!pm_game_path(id, game, sizeof(game)) || !pm_src_open(&src, game)) continue;
		size_t ln = 0, ls = 0;
		uint8_t *film = pm_game_file(&src, PM_F_ICON1, &ln);
		uint8_t *snd = pm_game_file(&src, PM_F_SND0, &ls);
		pm_src_close(&src);
		if (film && !pm_stopped(gen)) { pm_film_in = film; pm_film_in_n = ln; pm_film_in_gen = gen; } else free(film);
		if (snd && !pm_stopped(gen) && sound_on && !mus_playing) {
			bgm_target = 0.0f;
			for (int w = 0; w < 100 && bgm_gain_now > 0.0f && !pm_stopped(gen); w++) sceKernelDelayThread(5000);      /* the last game's sound has to fade out first */
			bgm_r = bgm_w;                                                       /* and what it left in the ring is dropped (the mixer reads nothing at gain 0) */
			bgm_target = 1.0f;
			if (!pm_stopped(gen)) pm_sound_play(snd, ls, gen);
		}
		free(snd);
	}
	return NULL;
}

/* ---- the main thread's side ---- */

static pthread_t pm_thread;
static int pm_thread_on, pm_counter;
static char pm_active_id[24];
static const Item *pm_checked; static int pm_checked_ok;

static vita2d_texture *pm_icon_tex_for(const Item *it)
{
	return it && it == pm_icon_item && pm_cur_tex >= 0 ? pm_tex[pm_cur_tex] : NULL;
}

static void pm_stop(void)
{
	pm_gen = ++pm_counter;                                         /* the worker lets go of what it has */
	bgm_target = 0.0f;
	pm_icon_item = NULL;
	pm_active_id[0] = 0;
	pm_film_free();
	uint8_t *late = pm_film_in;
	if (late) { pm_film_in = NULL; free(late); }
}

/* called once a frame with the item the cursor rests on and for how long; plays the film and the sound of a PSP game bubble */
static void pm_tick(const Item *sel, float dwell, int allowed)
{
	int bubble = 0;
	if (allowed && sel && sel->kind == KIND_APP && dwell > 0.6f) {
		if (sel != pm_checked) {
			pm_checked = sel;
			char p[64];
			snprintf(p, sizeof(p), "ux0:app/%.20s/data/boot.bin", sel->id);
			SceIoStat st;
			pm_checked_ok = sel->path[0] || (!strncmp(sel->id, "PSPEMU", 6) && strcmp(sel->id, "PSPEMUCFW") && sceIoGetstat(p, &st) >= 0);
		}
		bubble = pm_checked_ok;
	} else if (!sel || sel != pm_checked) pm_checked = NULL;
	if (!bubble) {
		if (pm_active_id[0] || pm_icon_item) pm_stop();
		return;
	}
	if (strcmp(pm_active_id, sel->id)) {                           /* a new game: ask the worker for its files */
		if (pm_active_id[0] || pm_icon_item) pm_stop();
		if (!pm_thread_on) {
			pthread_attr_t at;
			pthread_attr_init(&at);
			pthread_attr_setstacksize(&at, 512 * 1024);
			pm_thread_on = pthread_create(&pm_thread, &at, pm_worker, NULL) == 0;
			pthread_attr_destroy(&at);
			if (!pm_thread_on) return;
		}
		snprintf(pm_active_id, sizeof(pm_active_id), "%s", sel->id);
		snprintf(pm_req_id, sizeof(pm_req_id), "%s", sel->id);
		snprintf(pm_req_path, sizeof(pm_req_path), "%s", sel->path);
		pm_gen = ++pm_counter;
		pm_req_gen = pm_gen;
	}
	uint8_t *film = pm_film_in;
	if (film && pm_film_in_gen == pm_gen && !pm_icon_item) {
		pm_film_in = NULL;
		if (pm_film_open(film, pm_film_in_n)) pm_icon_item = sel;
	}
	if (pm_icon_item == sel) pm_film_update();
}
