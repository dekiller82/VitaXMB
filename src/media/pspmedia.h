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
static int pm_film_parse(const uint8_t *d, size_t n, uint8_t **ps_out, size_t *n_out)
{
	size_t pos = 0;
	if (n > 16 && !memcmp(d, "PSMF", 4)) {
		pos = ((size_t)d[8] << 24) | (d[9] << 16) | (d[10] << 8) | d[11];
		if (pos < 0x100 || pos >= n) pos = 0x800;
	}
	uint8_t *ps = malloc(n + AV_INPUT_BUFFER_PADDING_SIZE);
	if (!ps) return 0;
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
			if (pl <= end) { memcpy(ps + vn, d + pl, end - pl); vn += end - pl; }
		}
		if (!len) break;                                                     /* an unbounded packet runs to the end */
		pos += 6 + len;
	}
	memset(ps + vn, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	if (vn <= 64) { free(ps); return 0; }
	*ps_out = ps;
	*n_out = vn;
	return 1;
}

/* A film made ready by the worker thread: parsed, with its decoder open (parsing the file and opening the decoder took 100-250 ms, far too long for
 * the frame loop). The main thread only takes it over (pm_film_install) and decodes its small pictures. */
typedef struct { uint8_t *ps; size_t ps_n; AVCodecContext *vctx; AVCodecParserContext *vpar; AVFrame *vframe; AVPacket *vpkt; struct SwsContext *sws; } PmFilmPrep;

static void pm_prep_free(PmFilmPrep *fp)
{
	if (!fp) return;
	if (fp->vframe) av_frame_free(&fp->vframe);
	if (fp->vpkt) av_packet_free(&fp->vpkt);
	if (fp->vpar) av_parser_close(fp->vpar);
	if (fp->vctx) avcodec_free_context(&fp->vctx);
	if (fp->sws) sws_freeContext(fp->sws);
	free(fp->ps);
	free(fp);
}

/* worker thread: the ICON1.PMF bytes (freed here) -> a film ready to play, or NULL */
static PmFilmPrep *pm_film_prepare(uint8_t *file, size_t n)
{
	PmFilmPrep *fp = calloc(1, sizeof(*fp));
	int ok = fp && pm_film_parse(file, n, &fp->ps, &fp->ps_n);
	free(file);
	if (!ok) { free(fp); return NULL; }
	const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
	fp->vctx = c ? avcodec_alloc_context3(c) : NULL;
	fp->vpar = av_parser_init(AV_CODEC_ID_H264);
	fp->vframe = av_frame_alloc();
	fp->vpkt = av_packet_alloc();
	if (!fp->vctx || !fp->vpar || !fp->vframe || !fp->vpkt) { pm_prep_free(fp); return NULL; }
	fp->vctx->thread_count = 1;
	if (avcodec_open2(fp->vctx, c, NULL) < 0) { pm_prep_free(fp); return NULL; }
	/* Warm-up: the first picture decoded and converted here. The first decode of the process builds the codec's tables and the first conversion builds the
	 * colour converter (together 40 to 90 ms on the console); the frame loop gets a decoder that has done both. The film is then wound back to its start. */
	{
		size_t pos = 0;
		int flushing = 0, got = 0;
		for (int guard = 0; guard < 2000 && !got; guard++) {
			int r = avcodec_receive_frame(fp->vctx, fp->vframe);
			if (r >= 0) { got = 1; break; }
			if (r != AVERROR(EAGAIN)) break;
			if (pos >= fp->ps_n) {
				if (flushing) break;
				flushing = 1;
				avcodec_send_packet(fp->vctx, NULL);
				continue;
			}
			uint8_t *out = NULL; int out_n = 0;
			int used = av_parser_parse2(fp->vpar, fp->vctx, &out, &out_n, fp->ps + pos, (int)(fp->ps_n - pos), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if (used < 0) break;
			pos += (size_t)used;
			if (out_n > 0) { fp->vpkt->data = out; fp->vpkt->size = out_n; avcodec_send_packet(fp->vctx, fp->vpkt); }
		}
		if (got) {
			int w = fp->vframe->width, h = fp->vframe->height;
			if (w >= 16 && h >= 16 && w <= 480 && h <= 272) {
				fp->sws = sws_getCachedContext(NULL, w, h, (enum AVPixelFormat)fp->vframe->format, w, h, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
				uint8_t *tmp = fp->sws ? malloc((size_t)w * h * 4) : NULL;
				if (tmp) {
					uint8_t *dst[4] = { tmp, NULL, NULL, NULL };
					int ds[4] = { w * 4, 0, 0, 0 };
					sws_scale(fp->sws, (const uint8_t *const *)fp->vframe->data, fp->vframe->linesize, 0, h, dst, ds);
					free(tmp);
				}
			}
			av_frame_unref(fp->vframe);
		}
		avcodec_flush_buffers(fp->vctx);                               /* back to the start of the stream */
		av_parser_close(fp->vpar);
		fp->vpar = av_parser_init(AV_CODEC_ID_H264);
	}
	return fp;
}

/* main thread: makes a prepared film the current one (takes over its parts and frees the shell) */
static int pm_film_install(PmFilmPrep *fp)
{
	pm_film_free();
	pm_ps = fp->ps; pm_ps_n = fp->ps_n; pm_ps_pos = 0;
	pm_vctx = fp->vctx; pm_vpar = fp->vpar; pm_vframe = fp->vframe; pm_vpkt = fp->vpkt; pm_sws = fp->sws;
	free(fp);
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

/* The text block of a game: its PIC0.PNG (310 x 180), which the PSP draws over the background at the lower right, 5 PSP pixels from the right and bottom edges
 * (measured on a capture of Adrenaline's XMB: x 165, y 87 at the PSP's 480 x 272). The worker writes it out as a file, the main thread makes the texture. */
static char pm_pic0_file[112];
static volatile int pm_pic0_gen;                  /* the request the file belongs to */
static uint8_t *pm_pic0_pix;                      /* the worker's decoded picture, waiting for the main thread to make a texture of it (swapped atomically) */
static int pm_pic0_w, pm_pic0_h;
static vita2d_texture *pm_pic0_tex;
static const Item *pm_pic0_item;                  /* the game the texture is of */
static float pm_pic0_a;                           /* how far it has faded in; also grows the selected icon and fades the item text, as the background art does */

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
	sceKernelChangeThreadPriority(0, 0x10000110);                 /* a pthread starts at the main thread's priority; this one decodes audio and parses films: it must never compete with the frame loop */
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
		char p0[112];
		sceIoMkdir(PSP_CACHE_DIR, 0777);
		snprintf(p0, sizeof(p0), PSP_CACHE_DIR "/%s_p0.png", id);
		int have0 = psp_extract(&src, PM_F_PIC0, p0);
		pm_src_close(&src);
		if (have0 && !pm_stopped(gen)) {                     /* decoded here: the main thread only has to upload it */
			int w = 0, h = 0;
			uint8_t *pix = png_decode_scaled(p0, 1024, &w, &h);
			snprintf(pm_pic0_file, sizeof(pm_pic0_file), "%s", p0);
			if (pix) {
				pm_pic0_w = w; pm_pic0_h = h;
				uint8_t *old = __atomic_exchange_n(&pm_pic0_pix, pix, __ATOMIC_SEQ_CST);
				free(old);
			}
			pm_pic0_gen = gen;
		}
		PmFilmPrep *prep = film && !pm_stopped(gen) ? pm_film_prepare(film, ln) : (free(film), NULL);       /* parsed and opened here, not in the frame loop */
		if (prep && !pm_stopped(gen)) { pm_film_in = (uint8_t *)prep; pm_film_in_n = ln; pm_film_in_gen = gen; } else pm_prep_free(prep);
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
	uint8_t *late = __atomic_exchange_n(&pm_film_in, NULL, __ATOMIC_SEQ_CST);
	if (late) pm_prep_free((PmFilmPrep *)late);
}

/* fades the text block in for the game the cursor rests on and out for any other; the texture is made once the worker has written the file */
static void pm_pic0_step(const Item *sel, int bubble)
{
	int here = bubble && sel && pm_pic0_tex && pm_pic0_item == sel;
	float target = here ? 1.0f : 0.0f;
	pm_pic0_a += (target - pm_pic0_a) * (target > pm_pic0_a ? 0.07f : 0.18f);
	if (pm_pic0_a < 0.002f) pm_pic0_a = 0.0f;
	if (pm_pic0_tex && !here && pm_pic0_a == 0.0f) { defer_free(pm_pic0_tex); pm_pic0_tex = NULL; pm_pic0_item = NULL; }
	if (!pm_pic0_tex && bubble && sel && pm_pic0_gen == pm_gen) {
		uint8_t *pix = __atomic_exchange_n(&pm_pic0_pix, NULL, __ATOMIC_SEQ_CST);
		int w = pm_pic0_w, h = pm_pic0_h;
		vita2d_texture *t = pix ? texture_from_rgba(pix, w, h) : NULL;
		free(pix);
		pm_pic0_gen = 0;                                           /* once: a game without a text block is not asked again every frame */
		if (t) { pm_pic0_tex = t; pm_pic0_item = sel; }
	}
}

static void pm_pic0_draw(void)
{
	if (!pm_pic0_tex || pm_pic0_a <= 0.0f) return;
	float tw = (float)vita2d_texture_get_width(pm_pic0_tex), th = (float)vita2d_texture_get_height(pm_pic0_tex);
	vita2d_draw_texture_tint_scale(pm_pic0_tex, SCREEN_W - 10.0f - tw * 2.0f, SCREEN_H - 10.0f - th * 2.0f, 2.0f, 2.0f, RGBA8(255, 255, 255, (int)(255.0f * pm_pic0_a)));
}

/* called once a frame with the item the cursor rests on and for how long; plays the film and the sound of a PSP game bubble */
static void pm_tick(const Item *sel, float dwell, int allowed)
{
	int bubble = 0;
	if (allowed && sel && sel->kind == KIND_APP && dwell > 0.6f) {
		static int chk_pending;
		if (sel != pm_checked) {
			pm_checked = sel;
			chk_pending = 0;
			pm_checked_ok = 0;
			if (sel->path[0]) pm_checked_ok = 1;
			else if (!strncmp(sel->id, "PSPEMU", 6) && strcmp(sel->id, "PSPEMUCFW")) { sys_stat_request(sel->id); chk_pending = 1; }       /* asked of the system thread, answered a few frames later */
		}
		if (chk_pending && sys_stat_ready()) { pm_checked_ok = sys_stat_ok; chk_pending = 0; }
		bubble = pm_checked_ok;
	} else if (!sel || sel != pm_checked) pm_checked = NULL;
	if (!bubble) {
		if (pm_active_id[0] || pm_icon_item) pm_stop();
		pm_pic0_step(sel, 0);
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
		if (pm_film_install((PmFilmPrep *)film)) pm_icon_item = sel;
	}
	if (pm_icon_item == sel) pm_film_update();
	pm_pic0_step(sel, 1);
}
