#pragma once

/* ------------------------------------------------------------------------------------------------------------------ */
/* Video player engine: FFmpeg demuxer + decoders on their own threads, audio through the music mixer's ring.           */
/*                                                                                                                      */
/*   control thread   opens the file, reads packets, decodes text subtitles, handles seeks / track changes             */
/*   video thread     decodes (software) and converts to RGBA into a small ring of frames                              */
/*   audio thread     decodes, resamples to 48 kHz stereo and fills mus_ring; the audio position is the clock         */
/*                                                                                                                      */
/* The main thread calls vid_update() once per frame and draws vid_tex_cur(). The drawing of the PSP-style control    */
/* panel is in ui/videoui.h.                                                                                           */
/* ------------------------------------------------------------------------------------------------------------------ */

enum { VS_IDLE, VS_OPENING, VS_PLAYING, VS_ENDED, VS_ERROR };
enum { VC_NONE, VC_OPEN, VC_CLOSE };
enum { VM_NORMAL, VM_ZOOM, VM_FULL, VM_ORIGINAL, VM_43, VM_COUNT };      /* screen modes in the order of the PSP manual: fit, zoom (crops), stretch, actual size, 4:3 */

#define VID_PATH_MAX 256
#define VQ_N 256
#define VFR_N 4
#define VID_TRACKS 16
#define VID_CUES 6000

typedef struct { AVPacket *pkt; int gen; } VItem;
typedef struct {
	VItem it[VQ_N];
	int r, w, n;
	size_t bytes;
	pthread_mutex_t mx;
} PktQ;

typedef struct { uint8_t *rgba; int gen; double pts; volatile int ready; } VFrame;
typedef struct { int index; char lang[8]; char name[24]; int channels; } VTrack;
typedef struct { int start_ms, end_ms; char text[200]; } VCue;
typedef struct { double start, end; char title[40]; } VChapter;

/* what the UI reads (all set by the engine) */
static volatile int vid_cmd;
static char vid_req_path[VID_PATH_MAX];
static volatile int vid_status;                 /* VS_* */
static volatile int vid_paused;
static volatile int vid_gen;                    /* bumped by every start and seek; frames and audio of an older one are dropped */
static char vid_path[VID_PATH_MAX];
static double vid_duration;                     /* seconds, 0 when unknown */
static int vid_w, vid_h;
static double vid_fps;
static char vid_vcodec[16], vid_container[24];
static int vid_bitrate;                         /* kbit/s, 0 if unknown */
static int vid_hw;                              /* decoded by the hardware (reserved) */
static VTrack vid_atracks[VID_TRACKS], vid_stracks[VID_TRACKS];
static int vid_n_a, vid_n_s;
static volatile int vid_a_sel, vid_s_sel;       /* selected audio track (index into vid_atracks), subtitle track (-1 = off) */
static VChapter vid_chapters[64];
static int vid_n_chapters;
static VCue *vid_cues;
static volatile int vid_n_cues;
static volatile int vid_run = 1;
static const char *volatile vid_stage = "idle";      /* where the opening is (shown while loading, and in the log) */
enum { VB_FF, VB_AVP };
static int vid_backend;                               /* VB_FF: the FFmpeg engine below, VB_AVP: the system player in avp.h */
static vita2d_texture avp_wrap;                       /* the system player's current frame (a YUV texture the GPU reads) */
static volatile int avp_have;
static int avp_open(const char *path);
static void avp_close(void);
static double avp_position(void);
static void avp_seek(double to);
static void avp_pause(int p);
static void avp_poll(void);
static void avp_select_audio(int sel);
static int avp_fetch_frame(void);
static volatile unsigned vid_dbg_reads, vid_dbg_seeks;
static volatile unsigned long long vid_dbg_bytes;
static volatile long long vid_dbg_seek_to;
static char vid_lastlog[120];
static int vid_loglines;
static uint64_t vid_t_open;                         /* when the open started, for the timing in the log */
static uint64_t vid_now_us(void);
#define VSTAGE(x) do { vid_stage = (x); trace("video: %s (+%d ms)\n", (x), (int)((vid_now_us() - vid_t_open) / 1000)); } while (0)

static AVFormatContext *vid_fmt;
static AVCodecContext *vid_vctx, *vid_actx, *vid_sctx;
static int vid_vidx = -1;
static SceUID vid_fd = -1;
static int64_t vid_fsize;
static unsigned char *vid_iobuf;
static AVIOContext *vid_pb;

static PktQ vq, aq;
static VFrame vfr[VFR_N];
static int vfr_r, vfr_w;
static volatile int vid_threads;                 /* decode threads that are running */
/* The worker threads are pthreads: the Vita's pthread library (and so FFmpeg's and this file's mutexes and waits) crashes in a thread made
 * with sceKernelCreateThread as soon as a lock is contended. */
static pthread_t vid_vth, vid_ath;
static int vid_vth_on, vid_ath_on;
static volatile int vid_has_audio;                 /* the file has an audio track the player decodes */
static volatile int vid_abort;                   /* set to stop the decode threads */
static volatile int vid_step_req;                  /* show the next picture while paused */
static int vid_stepped;                           /* the picture was stepped: the sound must be re-synchronised when playing resumes */
static volatile int vid_busy;                    /* the main thread is reading frames / cues: the control thread must not free them now */
static uint64_t vid_gen_t0;                       /* when the current generation (start or seek) began */
static volatile int vid_chan;                    /* audio output: 0 both channels, 1 left only, 2 right only (the PSP's Audio Options for stored videos) */
static volatile int vid_warn;                    /* 0 none; 1 bigger than 720p; 2 a big picture for the software decoder: the player waits for a choice */
static void vid_set_pause(int p);
static volatile int vid_sub_req = -2;            /* a subtitle track change waiting for the control thread */
static void vid_apply_sub(int sel);

static volatile int vid_seek_req;
static volatile double vid_seek_to;              /* seconds */
static volatile double vid_target;               /* frames / audio before this are dropped after a seek */
static volatile int vid_aclk_valid;
static volatile double vid_abase;                /* position of the first sample the audio thread wrote after the last (re)start */
static volatile int vid_eof_v, vid_eof_a;        /* the decoders drained everything */
static volatile int vid_have_frame_gen;          /* generation of the frame currently on screen */
static double vid_wall_pos;                      /* clock of a file without sound */
static uint64_t vid_wall_t0;
static volatile double vid_speed = 1.0;          /* 1.0, or 0.5 for slow motion (muted) */
static double vid_last_pts;                      /* pts of the frame on screen */
static vita2d_texture *vid_tex[3];
static int vid_tex_i, vid_tex_w, vid_tex_h;
static int vid_tex_have;
static struct SwsContext *vid_sws;
static int vid_sws_w, vid_sws_h, vid_sws_fmt;

/* ---- packet queues ---- */
static void q_init(PktQ *q) { memset(q, 0, sizeof(*q)); pthread_mutex_init(&q->mx, NULL); }

static int q_full(PktQ *q, size_t limit)
{
	pthread_mutex_lock(&q->mx);
	int f = q->n >= VQ_N - 2 || q->bytes > limit;
	pthread_mutex_unlock(&q->mx);
	return f;
}

static int q_put(PktQ *q, AVPacket *pkt, int gen)       /* pkt == NULL: end of stream marker; takes ownership */
{
	pthread_mutex_lock(&q->mx);
	if (q->n >= VQ_N) { pthread_mutex_unlock(&q->mx); av_packet_free(&pkt); return 0; }
	q->it[q->w].pkt = pkt; q->it[q->w].gen = gen;
	q->w = (q->w + 1) % VQ_N; q->n++;
	if (pkt) q->bytes += (size_t)pkt->size;
	pthread_mutex_unlock(&q->mx);
	return 1;
}

static int q_get(PktQ *q, VItem *out)
{
	pthread_mutex_lock(&q->mx);
	if (!q->n) { pthread_mutex_unlock(&q->mx); return 0; }
	*out = q->it[q->r];
	q->r = (q->r + 1) % VQ_N; q->n--;
	if (out->pkt) q->bytes -= (size_t)out->pkt->size;
	pthread_mutex_unlock(&q->mx);
	return 1;
}

static void q_flush(PktQ *q)
{
	VItem it;
	while (q_get(q, &it)) av_packet_free(&it.pkt);
}

/* ---- file access: 64 bit seeks, a big buffer (the SD card likes large reads) ---- */
static void vid_ff_log(void *avcl, int level, const char *fmt, va_list vl)
{
	(void)avcl;
	if (level > AV_LOG_INFO) return;
	char line[200];
	vsnprintf(line, sizeof(line), fmt, vl);
	for (char *p = line; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
	if (!line[0] || (line[0] == ' ' && !line[1])) return;
	snprintf(vid_lastlog, sizeof(vid_lastlog), "%s", line);
	if (level <= AV_LOG_WARNING && vid_loglines < 20) { vid_loglines++; xlog("ffmpeg: %s\n", line); }      /* warnings and errors only; the rest is for debug builds */
	else trace("ffmpeg: %s\n", line);
}

static int vio_read(void *op, uint8_t *buf, int n)
{
	(void)op;
	vid_dbg_reads++;
	int r = sceIoRead(vid_fd, buf, (unsigned)n);
	if (r > 0) vid_dbg_bytes += (unsigned)r;
	return r > 0 ? r : (r == 0 ? AVERROR_EOF : AVERROR(EIO));
}

static int64_t vio_seek(void *op, int64_t off, int whence)
{
	(void)op;
	if (whence & AVSEEK_SIZE) return vid_fsize;
	whence &= ~AVSEEK_FORCE;
	vid_dbg_seeks++; vid_dbg_seek_to = off;
	int64_t r = sceIoLseek(vid_fd, off, whence);
	return r < 0 ? AVERROR(EIO) : r;
}

/* ---- the clock ---- */
static uint64_t vid_now_us(void) { return sceKernelGetProcessTimeWide(); }

static double vid_clock(void)
{
	if (vid_speed != 1.0 && vid_status == VS_PLAYING) {            /* slow motion: no sound, the wall clock at the chosen speed */
		if (vid_paused) return vid_wall_pos;
		return vid_wall_pos + (double)(vid_now_us() - vid_wall_t0) / 1.0e6 * vid_speed;
	}
	if (vid_aclk_valid) return vid_abase + (double)mus_consumed / 48000.0;
	if (vid_paused || vid_status != VS_PLAYING) return vid_wall_pos;
	if (vid_has_audio && vid_now_us() - vid_wall_t0 < 1500000ULL) return vid_wall_pos;      /* hold until the audio starts (but not forever) */
	return vid_wall_pos + (double)(vid_now_us() - vid_wall_t0) / 1.0e6 * vid_speed;
}

static void vid_wall_set(double pos) { vid_wall_pos = pos; vid_wall_t0 = vid_now_us(); }

/* ---- subtitles: ASS / SRT lines decoded by FFmpeg become cues ---- */
static void vid_add_cue(int start_ms, int end_ms, const char *ass_line)
{
	if (!vid_cues || vid_n_cues >= VID_CUES) return;
	/* "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text": the text is after the 8th comma */
	const char *p = ass_line;
	int commas = 0;
	while (*p && commas < 8) { if (*p == ',') commas++; p++; }
	if (commas < 8) p = ass_line;
	VCue *c = &vid_cues[vid_n_cues];
	size_t o = 0;
	for (; *p && o + 2 < sizeof(c->text); p++) {
		if (*p == '{') { while (*p && *p != '}') p++; if (!*p) break; continue; }       /* override tags */
		if (*p == '\\' && (p[1] == 'N' || p[1] == 'n')) { c->text[o++] = '\n'; p++; continue; }
		if (*p == '\\' && p[1] == 'h') { c->text[o++] = ' '; p++; continue; }
		if (*p == '\r') continue;
		c->text[o++] = *p;
	}
	while (o && (c->text[o - 1] == '\n' || c->text[o - 1] == ' ')) o--;
	c->text[o] = 0;
	if (!o) return;
	c->start_ms = start_ms; c->end_ms = end_ms;
	vid_n_cues++;
}

static void vid_sub_packet(AVCodecContext *sctx, AVStream *st, AVPacket *pkt)
{
	AVSubtitle sub;
	int got = 0;
	if (avcodec_decode_subtitle2(sctx, &sub, &got, pkt) < 0 || !got) return;
	int64_t base_ms = pkt->pts == AV_NOPTS_VALUE ? 0 : (int64_t)(pkt->pts * av_q2d(st->time_base) * 1000.0);
	int dur = (int)(pkt->duration > 0 ? pkt->duration * av_q2d(st->time_base) * 1000.0 : 0);
	for (unsigned i = 0; i < sub.num_rects; i++) {
		AVSubtitleRect *r = sub.rects[i];
		const char *txt = r->type == SUBTITLE_ASS ? r->ass : (r->type == SUBTITLE_TEXT ? r->text : NULL);
		if (!txt) continue;
		int s = (int)base_ms + (int)sub.start_display_time;
		int e = sub.end_display_time && sub.end_display_time != 0xFFFFFFFFu ? (int)base_ms + (int)sub.end_display_time : (int)base_ms + (dur ? dur : 3000);
		vid_add_cue(s, e, txt);
	}
	avsubtitle_free(&sub);
}

/* A subtitle file next to the video (.srt .ass .ssa .vtt): its cues join the embedded ones. */
static void vid_load_ext_subs(const char *vpath)
{
	static const char *ext[] = { ".srt", ".ass", ".ssa", ".vtt" };
	char sp[VID_PATH_MAX + 8];
	snprintf(sp, sizeof(sp), "%s", vpath);
	char *dot = strrchr(sp, '.');
	if (!dot) return;
	for (int k = 0; k < 4; k++) {
		strcpy(dot, ext[k]);
		AVFormatContext *f = NULL;
		if (avformat_open_input(&f, sp, NULL, NULL) < 0) continue;
		if (avformat_find_stream_info(f, NULL) >= 0 && f->nb_streams > 0) {
			AVStream *st = f->streams[0];
			const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
			AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
			if (ctx && avcodec_parameters_to_context(ctx, st->codecpar) >= 0 && avcodec_open2(ctx, c, NULL) >= 0) {
				AVPacket *p = av_packet_alloc();
				while (p && av_read_frame(f, p) >= 0) { vid_sub_packet(ctx, st, p); av_packet_unref(p); }
				av_packet_free(&p);
				if (vid_n_cues) {
					trace("subtitles from %s: %d cues\n", sp, vid_n_cues);
					if (vid_n_s < VID_TRACKS) {
						VTrack *t = &vid_stracks[vid_n_s];
						t->index = -2 - k; snprintf(t->lang, sizeof(t->lang), "ext"); snprintf(t->name, sizeof(t->name), "%s", ext[k] + 1);
						vid_n_s++;
					}
				}
			}
			avcodec_free_context(&ctx);
		}
		avformat_close_input(&f);
		if (vid_n_cues) break;
	}
}

/* ---- audio ---- */
static void vid_audio_reset_ring(void)
{
	mus_playing = 0;
	sceKernelDelayThread(40000);                      /* let the mixer finish its grain */
	mus_w = mus_r = 0;
	mus_consumed = 0;
}

/* the sound waits for the first picture of a start or a seek (a big video takes seconds to decode its first frame), but not for ever */
static int vid_pic_ready(void)
{
	return vid_have_frame_gen == vid_gen || sceKernelGetProcessTimeWide() - vid_gen_t0 > 5000000ULL;
}

static volatile int vid_a_have = -1;                 /* the track the audio thread has opened */

static void vid_pick_audio_ctx(int sel)
{
	if (sel < 0 || sel >= vid_n_a) return;
	AVStream *st = vid_fmt->streams[vid_atracks[sel].index];
	const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
	AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
	if (ctx && avcodec_parameters_to_context(ctx, st->codecpar) >= 0 && avcodec_open2(ctx, c, NULL) >= 0) vid_actx = ctx;
	else { avcodec_free_context(&ctx); xlog("video: no audio decoder for stream %d\n", vid_atracks[sel].index); }
}

static void *vid_athread(void *arg)
{
	(void)arg;
	sceKernelChangeThreadPriority(0, 0x10000108);
	AVFrame *fr = av_frame_alloc();
	SwrContext *swr = NULL;
	int seen = -1, swr_rate = 0, swr_fmt = -1, swr_ch = 0;
	static int16_t outbuf[48000 / 4 * 2 * 8];          /* up to ~2 s of 48 kHz stereo per decoded frame */
	int started = 0;
	vid_threads++;
	while (!vid_abort) {
		VItem it;
		if (vid_a_have != vid_a_sel) {                       /* the track was changed: this thread owns the audio decoder */
			avcodec_free_context(&vid_actx);
			vid_a_have = vid_a_sel;
			vid_pick_audio_ctx(vid_a_sel);
			seen = -1;
		}
		if (!vid_actx || !q_get(&aq, &it)) { sceKernelDelayThread(4000); continue; }
		if (it.gen != vid_gen) { av_packet_free(&it.pkt); continue; }
		if (it.gen != seen) {
			seen = it.gen;
			avcodec_flush_buffers(vid_actx);
			if (swr) swr_free(&swr);
			vid_audio_reset_ring();
			vid_aclk_valid = 0; started = 0; vid_eof_a = 0;
		}
		avcodec_send_packet(vid_actx, it.pkt);              /* a NULL packet (end of file) starts the drain */
		av_packet_free(&it.pkt);
		for (;;) {
			int r = avcodec_receive_frame(vid_actx, fr);
			if (r < 0) {
				if (r == AVERROR_EOF) vid_eof_a = 1;
				break;
			}
			if (it.gen != vid_gen) { av_frame_unref(fr); continue; }
			if (!swr || swr_rate != fr->sample_rate || swr_fmt != fr->format || swr_ch != fr->ch_layout.nb_channels) {
				if (swr) swr_free(&swr);
				AVChannelLayout out;
				av_channel_layout_default(&out, 2);
				if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, 48000, &fr->ch_layout, (enum AVSampleFormat)fr->format, fr->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) {
					swr_free(&swr); av_frame_unref(fr); continue;
				}
				swr_rate = fr->sample_rate; swr_fmt = fr->format; swr_ch = fr->ch_layout.nb_channels;
			}
			double pts = fr->pts == AV_NOPTS_VALUE ? -1.0 : fr->pts * av_q2d(vid_fmt->streams[vid_atracks[vid_a_sel].index]->time_base);
			int max_out = (int)(sizeof(outbuf) / sizeof(outbuf[0]) / 2);
			uint8_t *outp[1] = { (uint8_t *)outbuf };
			int n = swr_convert(swr, outp, max_out, (const uint8_t **)fr->extended_data, fr->nb_samples);
			av_frame_unref(fr);
			if (n <= 0) continue;
			int skip = 0;
			if (pts >= 0 && pts + (double)n / 48000.0 < vid_target) continue;                 /* wholly before the seek target */
			if (pts >= 0 && pts < vid_target) skip = (int)((vid_target - pts) * 48000.0);     /* partly: trim the front */
			if (skip >= n) continue;
			if (!started) {
				vid_abase = (pts >= 0 ? pts : vid_target) + (double)skip / 48000.0;
				mus_consumed = 0;
				vid_aclk_valid = 1;
				started = 1;
			}
			for (int i = skip; i < n && it.gen == vid_gen && !vid_abort; ) {
				unsigned space = MUS_RING - (mus_w - mus_r);
				if (space < 256) {
					if (!mus_playing && !vid_paused && vid_speed == 1.0 && vid_pic_ready()) mus_playing = 1;      /* ring full: start (or restart after a gap) */
					sceKernelDelayThread(5000);
					continue;
				}
				int chunk = n - i < (int)space ? n - i : (int)space;
				for (int k = 0; k < chunk; k++) {
					unsigned w = (mus_w + k) & (MUS_RING - 1);
					int16_t cl = outbuf[(i + k) * 2], cr = outbuf[(i + k) * 2 + 1];
					if (vid_chan == 1) cr = cl; else if (vid_chan == 2) cl = cr;
					mus_ring[w * 2] = cl;
					mus_ring[w * 2 + 1] = cr;
				}
				mus_w += chunk;
				i += chunk;
				if (!mus_playing && !vid_paused && vid_speed == 1.0 && (int)(mus_w - mus_r) > 9600 && vid_pic_ready()) mus_playing = 1;      /* 0.2 s buffered, and the picture is up */
			}
		}
	}
	if (swr) swr_free(&swr);
	av_frame_free(&fr);
	vid_threads--;
	return NULL;
}

/* ---- video ---- */
static void *vid_vthread(void *arg)
{
	(void)arg;
	sceKernelChangeThreadPriority(0, 0x10000108);
	AVFrame *fr = av_frame_alloc();
	int seen = -1;
	vid_threads++;
	while (!vid_abort) {
		/* a ready slot is needed before decoding further */
		VItem it;
		if (vfr[vfr_w].ready) { sceKernelDelayThread(3000); continue; }
		if (!q_get(&vq, &it)) { sceKernelDelayThread(3000); continue; }
		if (it.gen != vid_gen) { av_packet_free(&it.pkt); continue; }
		if (it.gen != seen) {
			seen = it.gen;
			avcodec_flush_buffers(vid_vctx);
			vid_eof_v = 0;
		}
		avcodec_send_packet(vid_vctx, it.pkt);
		av_packet_free(&it.pkt);
		for (;;) {
			int r = avcodec_receive_frame(vid_vctx, fr);
			if (r < 0) { if (r == AVERROR_EOF) vid_eof_v = 1; break; }
			if (it.gen != vid_gen) { av_frame_unref(fr); continue; }
			double pts = fr->best_effort_timestamp == AV_NOPTS_VALUE ? vid_target : fr->best_effort_timestamp * av_q2d(vid_fmt->streams[vid_vidx]->time_base);
			if (pts < vid_target - 0.001) { av_frame_unref(fr); continue; }         /* decoded only to reach the seek target */
			while (vfr[vfr_w].ready && !vid_abort && it.gen == vid_gen) sceKernelDelayThread(3000);
			if (vid_abort || it.gen != vid_gen) { av_frame_unref(fr); continue; }
			if (!vid_sws || vid_sws_w != fr->width || vid_sws_h != fr->height || vid_sws_fmt != fr->format) {
				sws_freeContext(vid_sws);
				vid_sws = sws_getContext(fr->width, fr->height, (enum AVPixelFormat)fr->format, vid_w, vid_h, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
				vid_sws_w = fr->width; vid_sws_h = fr->height; vid_sws_fmt = fr->format;
			}
			if (vid_sws) {
				uint8_t *dst[4] = { vfr[vfr_w].rgba, NULL, NULL, NULL };
				int dstride[4] = { vid_w * 4, 0, 0, 0 };
				sws_scale(vid_sws, (const uint8_t *const *)fr->data, fr->linesize, 0, fr->height, dst, dstride);
				vfr[vfr_w].pts = pts;
				vfr[vfr_w].gen = it.gen;
				__sync_synchronize();
				vfr[vfr_w].ready = 1;
				vfr_w = (vfr_w + 1) % VFR_N;
			}
			av_frame_unref(fr);
		}
	}
	av_frame_free(&fr);
	vid_threads--;
	return NULL;
}

/* ---- open / close ---- */
static int vid_spawn(pthread_t *t, void *(*fn)(void *))
{
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, 1024 * 1024);
	int r = pthread_create(t, &at, fn, NULL);
	pthread_attr_destroy(&at);
	return r;
}
static void vid_free_all(void)
{
	if (vid_backend == VB_AVP) { avp_close(); vid_backend = VB_FF; }
	vid_abort = 1;
	if (vid_vth_on) { pthread_join(vid_vth, NULL); vid_vth_on = 0; }
	if (vid_ath_on) { pthread_join(vid_ath, NULL); vid_ath_on = 0; }
	vid_abort = 0;
	q_flush(&vq); q_flush(&aq);
	avcodec_free_context(&vid_vctx);
	avcodec_free_context(&vid_actx);
	avcodec_free_context(&vid_sctx);
	if (vid_fmt) avformat_close_input(&vid_fmt);
	if (vid_pb) { av_freep(&vid_pb->buffer); avio_context_free(&vid_pb); vid_pb = NULL; }
	if (vid_fd >= 0) { sceIoClose(vid_fd); vid_fd = -1; }
	if (vid_sws) { sws_freeContext(vid_sws); vid_sws = NULL; }
	for (int i = 0; i < VFR_N; i++) { free(vfr[i].rgba); memset(&vfr[i], 0, sizeof(vfr[i])); }
	vfr_r = vfr_w = 0;
	free(vid_cues); vid_cues = NULL; vid_n_cues = 0;
	mus_playing = 0;
	vid_aclk_valid = 0;
	vid_vidx = -1;
}

static void vid_tag_lang(AVStream *st, char *lang, size_t n)
{
	AVDictionaryEntry *e = av_dict_get(st->metadata, "language", NULL, 0);
	snprintf(lang, n, "%s", e ? e->value : "");
}

static int vid_open_file(const char *path)
{
	vid_status = VS_OPENING;
	VSTAGE("opening the file");
	vid_n_a = vid_n_s = vid_n_chapters = 0;
	vid_a_sel = 0; vid_s_sel = -1;
	vid_eof_v = vid_eof_a = 0;
	vid_paused = 0; vid_speed = 1.0; vid_warn = 0;
	vid_duration = 0; vid_bitrate = 0; vid_fps = 0;
	if (path != vid_path) snprintf(vid_path, sizeof(vid_path), "%s", path);

	vid_fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (vid_fd < 0) { xlog("video: cannot open %s (%08x)\n", path, vid_fd); return -1; }
	vid_fsize = sceIoLseek(vid_fd, 0, SCE_SEEK_END);
	sceIoLseek(vid_fd, 0, SCE_SEEK_SET);

	VSTAGE("reading the container");
	vid_fmt = avformat_alloc_context();
	vid_iobuf = av_malloc(256 * 1024);
	vid_pb = avio_alloc_context(vid_iobuf, 256 * 1024, 0, NULL, vio_read, NULL, vio_seek);
	if (!vid_fmt || !vid_iobuf || !vid_pb) return -1;
	vid_fmt->pb = vid_pb;
	vid_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
	vid_fmt->probesize = 2 * 1024 * 1024;
	vid_fmt->max_analyze_duration = AV_TIME_BASE;          /* the container already says what is inside: do not decode seconds of picture to find out */
	vid_fmt->fps_probe_size = 2;                           /* (the default decodes ~20 frames to guess the frame rate: 50 s for a big video on the Vita) */
	if (avformat_open_input(&vid_fmt, path, NULL, NULL) < 0) { xlog("video: unknown container %s\n", path); vid_fmt = NULL; return -1; }
	/* Containers with a header index (MKV, MP4 / MOV, AVI, ASF, FLV) already say what is inside; probing would decode frames, which is
	 * minutes for a big picture on the Vita. Streams without such a header (TS, PS, raw) are probed, but only briefly. */
	{
		const char *fn = vid_fmt->iformat ? vid_fmt->iformat->name : "";
		int indexed = strstr(fn, "matroska") || strstr(fn, "mov") || strstr(fn, "avi") || strstr(fn, "asf") || strstr(fn, "flv");
		if (indexed) {
			VSTAGE("reading the stream headers");
		} else {
			VSTAGE("probing the streams");
			vid_fmt->probesize = 1024 * 1024;
			vid_fmt->max_analyze_duration = AV_TIME_BASE / 2;
			if (avformat_find_stream_info(vid_fmt, NULL) < 0) { xlog("video: no stream info %s\n", path); return -1; }
		}
	}

	snprintf(vid_container, sizeof(vid_container), "%.23s", vid_fmt->iformat ? vid_fmt->iformat->name : "?");
	if (vid_fmt->duration > 0) vid_duration = (double)vid_fmt->duration / AV_TIME_BASE;
	if (vid_fmt->bit_rate > 0) vid_bitrate = (int)(vid_fmt->bit_rate / 1000);
	for (unsigned i = 0; i < vid_fmt->nb_chapters && vid_n_chapters < 64; i++) {
		AVChapter *c = vid_fmt->chapters[i];
		VChapter *o = &vid_chapters[vid_n_chapters++];
		o->start = c->start * av_q2d(c->time_base);
		o->end = c->end * av_q2d(c->time_base);
		AVDictionaryEntry *e = av_dict_get(c->metadata, "title", NULL, 0);
		snprintf(o->title, sizeof(o->title), "%s", e ? e->value : "");
	}

	vid_vidx = av_find_best_stream(vid_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (vid_vidx < 0) { xlog("video: no video stream in %s\n", path); return -1; }
	for (unsigned i = 0; i < vid_fmt->nb_streams; i++) {
		AVStream *st = vid_fmt->streams[i];
		if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && vid_n_a < VID_TRACKS) {
			VTrack *t = &vid_atracks[vid_n_a++];
			t->index = (int)i; t->channels = st->codecpar->ch_layout.nb_channels;
			vid_tag_lang(st, t->lang, sizeof(t->lang));
			snprintf(t->name, sizeof(t->name), "%s", avcodec_get_name(st->codecpar->codec_id));
		} else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && vid_n_s < VID_TRACKS && st->codecpar->codec_id != AV_CODEC_ID_NONE) {
			VTrack *t = &vid_stracks[vid_n_s++];
			t->index = (int)i;
			vid_tag_lang(st, t->lang, sizeof(t->lang));
			snprintf(t->name, sizeof(t->name), "%s", avcodec_get_name(st->codecpar->codec_id));
		}
	}

	AVStream *vs = vid_fmt->streams[vid_vidx];
	const AVCodec *vc = avcodec_find_decoder(vs->codecpar->codec_id);
	if (!vc) { xlog("video: no decoder for %s\n", avcodec_get_name(vs->codecpar->codec_id)); return -1; }
	VSTAGE("opening the video decoder");
	vid_vctx = avcodec_alloc_context3(vc);
	if (!vid_vctx || avcodec_parameters_to_context(vid_vctx, vs->codecpar) < 0) return -1;
	vid_vctx->thread_count = 3;
	vid_vctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	if (vs->codecpar->width * vs->codecpar->height > 1280 * 720) vid_vctx->skip_loop_filter = AVDISCARD_NONREF;     /* big pictures: the loop filter costs too much */
	if (avcodec_open2(vid_vctx, vc, NULL) < 0) { xlog("video: cannot open decoder %s\n", vc->name); return -1; }
	vid_w = vs->codecpar->width; vid_h = vs->codecpar->height;
	if (vid_w <= 0 || vid_h <= 0 || vid_w > 4096 || vid_h > 2304) return -1;
	vid_fps = av_q2d(av_guess_frame_rate(vid_fmt, vs, NULL));
	snprintf(vid_vcodec, sizeof(vid_vcodec), "%s", avcodec_get_name(vs->codecpar->codec_id));

	vid_a_sel = 0; vid_a_have = -1;                    /* the audio thread opens the decoder */
	vid_has_audio = vid_n_a > 0;
	for (int i = 0; i < VFR_N; i++) {
		vfr[i].rgba = malloc((size_t)vid_w * vid_h * 4);
		if (!vfr[i].rgba) { xlog("video: out of memory for %dx%d frames\n", vid_w, vid_h); return -1; }
		vfr[i].ready = 0;
	}
	vfr_r = vfr_w = 0;
	vid_cues = calloc(VID_CUES, sizeof(VCue));
	vid_n_cues = 0;
	VSTAGE("looking for subtitles");
	vid_load_ext_subs(path);
	/* the first embedded text subtitle track is read as the file plays */
	for (int i = 0; i < vid_n_s; i++) if (vid_stracks[i].index >= 0) {
		AVStream *st = vid_fmt->streams[vid_stracks[i].index];
		const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
		AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
		if (ctx && avcodec_parameters_to_context(ctx, st->codecpar) >= 0 && avcodec_open2(ctx, c, NULL) >= 0) { vid_sctx = ctx; vid_s_sel = i; break; }
		avcodec_free_context(&ctx);
	}
	if (vid_s_sel < 0 && vid_n_cues) vid_s_sel = vid_n_s - 1;      /* an external file */

	vid_gen++;
	vid_gen_t0 = vid_now_us();
	vid_target = 0; vid_have_frame_gen = -1; vid_aclk_valid = 0; vid_last_pts = 0;
	vid_wall_set(0);
	vid_audio_reset_ring();
	vid_abort = 0;
	VSTAGE("starting the decoders");
	if (vid_spawn(&vid_vth, vid_vthread) || (vid_vth_on = 1, vid_spawn(&vid_ath, vid_athread))) { xlog("video: cannot start the decoder threads\n"); return -1; }
	vid_ath_on = 1;
	VSTAGE("playing");
	trace("video opened %s: %dx%d %s %.2f fps, %.1f s, %d audio, %d subtitle tracks\n", path, vid_w, vid_h, vid_vcodec, vid_fps, vid_duration, vid_n_a, vid_n_s);
	return 0;
}

/* the control thread: opens, reads packets, answers seeks */
static void vid_do_seek(double to)
{
	if (to < 0) to = 0;
	if (vid_duration > 0 && to > vid_duration - 0.2) to = vid_duration > 0.4 ? vid_duration - 0.2 : 0;
	vid_gen++;
	vid_gen_t0 = vid_now_us();
	vid_target = to;
	vid_eof_v = vid_eof_a = 0;
	mus_playing = 0;
	vid_aclk_valid = 0;
	q_flush(&vq); q_flush(&aq);
	int64_t ts = (int64_t)(to * AV_TIME_BASE);
	if (av_seek_frame(vid_fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) av_seek_frame(vid_fmt, -1, ts, AVSEEK_FLAG_ANY);
	vid_wall_set(to);
	vid_last_pts = to;
}

/* the containers the system player reads (and so the ones tried with it first); ux0:data/VitaXMB/noavp turns it off for testing */
static int vid_avp_candidate(const char *path)
{
	static const char *e[] = { ".mp4", ".m4v", ".mov", ".3gp", ".3g2" };
	const char *dot = strrchr(path, '.');
	if (!dot) return 0;
	int ok = 0;
	for (unsigned i = 0; i < sizeof(e) / sizeof(e[0]); i++) if (!strcasecmp(dot, e[i])) ok = 1;
	if (!ok) return 0;
	FILE *f = fopen(CONFIG_DIR "/noavp", "rb");
	if (f) { fclose(f); return 0; }
	return 1;
}

static void *vid_thread(void *arg)
{
	(void)arg;
	sceKernelChangeThreadPriority(0, 0x10000110);
	AVPacket *pkt = av_packet_alloc();
	int at_eof = 0;
	while (vid_run) {
		int cmd = vid_cmd;
		if (cmd != VC_NONE) {
			vid_cmd = VC_NONE;
			if (cmd == VC_CLOSE || cmd == VC_OPEN) {
				if (vid_status != VS_IDLE) {
					vid_status = VS_OPENING;                         /* the main thread stops reading frames ... */
					__sync_synchronize();
					while (vid_busy) sceKernelDelayThread(1000);    /* ... and is out of them before they are freed */
					vid_free_all();
					vid_status = VS_IDLE;
				}
			}
			if (cmd == VC_OPEN) {
				trace("video: open request %s\n", vid_req_path);
				vid_t_open = vid_now_us();
				snprintf(vid_path, sizeof(vid_path), "%s", vid_req_path);
				int ok = -1;
				vid_backend = VB_FF;
				if (vid_avp_candidate(vid_path)) {
					vid_status = VS_OPENING;
					if (avp_open(vid_path) == 0) { ok = 0; vid_backend = VB_AVP; }
					else { xlog("video: the system player cannot play this file, trying FFmpeg\n"); avp_close(); vid_free_all(); }
				}
				if (ok < 0) ok = vid_open_file(vid_path);
				if (ok < 0) { vid_free_all(); vid_status = VS_ERROR; }
				else {
					vid_status = VS_PLAYING; at_eof = 0; vid_seek_req = 0;
					/* the Vita cannot play much above 720p, and the software decoder not even that: ask first */
					int big = (vid_w > vid_h ? vid_w : vid_h) > 1280 || (vid_w > vid_h ? vid_h : vid_w) > 720;
					int heavy = vid_backend == VB_FF && (long)vid_w * vid_h > 520000;
					vid_warn = big ? 1 : (heavy ? 2 : 0);
					if (vid_warn) { trace("video: %dx%d, warning %d\n", vid_w, vid_h, vid_warn); vid_set_pause(1); }
				}
			}
		}
		if (vid_status != VS_PLAYING) { sceKernelDelayThread(8000); continue; }
		if (vid_backend == VB_AVP) { avp_poll(); sceKernelDelayThread(30000); continue; }

		if (vid_sub_req != -2) { int sr = vid_sub_req; vid_sub_req = -2; vid_apply_sub(sr); }
		if (vid_seek_req) {
			vid_seek_req = 0;
			vid_do_seek(vid_seek_to);
			at_eof = 0;
		}
		if (at_eof) {
			if (vid_eof_v && (vid_eof_a || !vid_has_audio) && vid_status == VS_PLAYING && vfr[vfr_r].ready == 0 && (!vid_aclk_valid || mus_w == mus_r)) vid_status = VS_ENDED;
			sceKernelDelayThread(8000);
			continue;
		}
		if (q_full(&vq, 24u << 20) || q_full(&aq, 6u << 20)) { sceKernelDelayThread(3000); continue; }
		int r = av_read_frame(vid_fmt, pkt);
		if (r < 0) {
			if (r == AVERROR_EOF || avio_feof(vid_fmt->pb) || r == AVERROR(EIO)) {
				q_put(&vq, NULL, vid_gen);
				q_put(&aq, NULL, vid_gen);
				at_eof = 1;
			}
			continue;
		}
		if (pkt->stream_index == vid_vidx) {
			q_put(&vq, av_packet_clone(pkt), vid_gen);
		} else if (vid_has_audio && vid_a_sel >= 0 && pkt->stream_index == vid_atracks[vid_a_sel].index) {
			q_put(&aq, av_packet_clone(pkt), vid_gen);
		} else if (vid_sctx && vid_s_sel >= 0 && vid_s_sel < vid_n_s && pkt->stream_index == vid_stracks[vid_s_sel].index) {
			vid_sub_packet(vid_sctx, vid_fmt->streams[pkt->stream_index], pkt);
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	return NULL;
}

/* ---- calls from the UI (main thread) ---- */
static void vid_open(const char *path)
{
	snprintf(vid_req_path, sizeof(vid_req_path), "%s", path);
	vid_cmd = VC_OPEN;
}
static void vid_close(void) { vid_cmd = VC_CLOSE; }

static void vid_seek_abs(double to)
{
	if (vid_backend == VB_AVP) { avp_seek(to); if (vid_status == VS_ENDED) vid_status = VS_PLAYING; return; }
	vid_seek_to = to; vid_seek_req = 1; if (vid_status == VS_ENDED) vid_status = VS_PLAYING;
}
static double vid_position(void)
{
	if (vid_backend == VB_AVP) return avp_position();
	return vid_clock() < 0 ? 0 : vid_clock();
}
static void vid_seek_rel(double d) { vid_seek_abs(vid_position() + d); }

static void vid_set_pause(int p)
{
	if (p == vid_paused) return;
	if (vid_backend == VB_AVP) { vid_paused = p; avp_pause(p); return; }
	if (!p && vid_stepped) { vid_stepped = 0; vid_paused = 0; vid_seek_abs(vid_last_pts); return; }
	if (!p) vid_wall_set(vid_wall_pos);
	else vid_wall_pos = vid_clock();
	vid_paused = p;
	if (p) mus_playing = 0;
	else if (vid_aclk_valid && vid_speed == 1.0) mus_playing = 1;
}
static void vid_toggle_pause(void) { vid_set_pause(!vid_paused); }

static void vid_set_speed(double s)                      /* 0.5: slow motion, silent (FFmpeg engine only) */
{
	if (vid_backend == VB_AVP || s == vid_speed) return;
	double pos = vid_position();
	if (s != 1.0) {
		vid_wall_pos = pos; vid_wall_t0 = vid_now_us();
		vid_speed = s;
		mus_playing = 0;
	} else {
		vid_speed = 1.0;
		vid_seek_abs(pos);                                   /* the sound starts again from here */
	}
}

/* one picture forward while paused (FFmpeg engine only) */
static void vid_step_frame(void)
{
	if (vid_backend == VB_AVP || vid_status != VS_PLAYING) return;
	if (!vid_paused) vid_set_pause(1);
	vid_step_req = 1;
}

static void vid_select_audio(int sel)
{
	if (sel < 0 || sel >= vid_n_a || sel == vid_a_sel || vid_status != VS_PLAYING) return;
	if (vid_backend == VB_AVP) { avp_select_audio(sel); return; }
	double pos = vid_position();
	vid_a_sel = sel;                                     /* the audio thread notices and reopens its decoder */
	vid_seek_abs(pos);                                   /* and the stream restarts from here with the new track */
}

/* Switch the subtitle track (-1: off). The control thread does it (it owns the subtitle decoder); embedded tracks fill in as the file plays. */
static void vid_select_sub(int sel)
{
	if (sel < -1 || sel >= vid_n_s) return;
	if (vid_backend == VB_AVP) vid_s_sel = sel; else vid_sub_req = sel;
}

static void vid_apply_sub(int sel)
{
	if (sel >= 0 && vid_stracks[sel].index >= 0 && sel != vid_s_sel) {
		avcodec_free_context(&vid_sctx);
		vid_n_cues = 0;
		AVStream *st = vid_fmt->streams[vid_stracks[sel].index];
		const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
		AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
		if (ctx && avcodec_parameters_to_context(ctx, st->codecpar) >= 0 && avcodec_open2(ctx, c, NULL) >= 0) vid_sctx = ctx;
		else avcodec_free_context(&ctx);
		vid_seek_abs(vid_position());                   /* read the packets of the new track from here on */
	}
	vid_s_sel = sel;
}

/* the cue to show at time t (seconds), or NULL */
static const VCue *vid_cue_at(double t)
{
	int ms = (int)(t * 1000.0);
	for (int i = vid_n_cues - 1; i >= 0 && i > vid_n_cues - 400; i--)
		if (vid_cues[i].start_ms <= ms && ms < vid_cues[i].end_ms) return &vid_cues[i];
	for (int i = 0; i < vid_n_cues && i <= vid_n_cues - 400; i++)
		if (vid_cues[i].start_ms <= ms && ms < vid_cues[i].end_ms) return &vid_cues[i];
	return NULL;
}

/* Called once per displayed frame: moves the next decoded picture into a texture when its time has come. */
static void vid_update(void)
{
	if (vid_status != VS_PLAYING && vid_status != VS_ENDED) return;
	if (vid_backend == VB_AVP) { if (vid_status == VS_PLAYING) avp_fetch_frame(); return; }
	if (!vid_tex_have || vid_tex_w != vid_w || vid_tex_h != vid_h) {
		for (int i = 0; i < 3; i++) { if (vid_tex[i]) defer_free(vid_tex[i]); vid_tex[i] = vita2d_create_empty_texture(vid_w, vid_h); if (vid_tex[i]) vita2d_texture_set_filters(vid_tex[i], SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR); }
		vid_tex_w = vid_w; vid_tex_h = vid_h; vid_tex_have = 1;
		vid_tex_i = -1;
	}
	double clk = vid_clock();
	int shown = 0;
	for (int guard = 0; guard < VFR_N; guard++) {
		VFrame *f = &vfr[vfr_r];
		if (!f->ready) break;
		__sync_synchronize();
		if (f->gen != vid_gen) { f->ready = 0; vfr_r = (vfr_r + 1) % VFR_N; continue; }
		int first = vid_have_frame_gen != f->gen;                       /* the first picture after a start or a seek is shown at once */
		int force = vid_step_req;
		if (!first && !force && f->pts > clk + 0.004) break;
		/* if the next one is due as well, this one is late: drop it */
		VFrame *nx = &vfr[(vfr_r + 1) % VFR_N];
		if (!first && !force && nx->ready && nx->gen == vid_gen && nx->pts <= clk + 0.004) { f->ready = 0; vfr_r = (vfr_r + 1) % VFR_N; continue; }
		int ti = (vid_tex_i + 1) % 3;
		vita2d_texture *t = vid_tex[ti];
		if (t) {
			uint8_t *dst = vita2d_texture_get_datap(t);
			int stride = vita2d_texture_get_stride(t);
			for (int y = 0; y < vid_h; y++) memcpy(dst + (size_t)y * stride, f->rgba + (size_t)y * vid_w * 4, (size_t)vid_w * 4);
			vid_tex_i = ti;
			vid_have_frame_gen = f->gen;
			if (vid_aclk_valid && !vid_paused && vid_speed == 1.0 && mus_w != mus_r) mus_playing = 1;      /* the picture is up: let the waiting sound go */
			vid_last_pts = f->pts;
			if (force) {                                                  /* stepped: the clock moves to this picture */
				vid_step_req = 0; vid_stepped = 1;
				vid_wall_pos = f->pts;
				if (vid_aclk_valid) vid_abase = f->pts - (double)mus_consumed / 48000.0;
			}
			if (!vid_aclk_valid && vid_status == VS_PLAYING && first) vid_wall_set(f->pts);     /* a file without sound: the first picture sets the clock */
		}
		f->ready = 0;
		vfr_r = (vfr_r + 1) % VFR_N;
		shown = 1;
		if (!first) break;
	}
	(void)shown;
}

static vita2d_texture *vid_tex_cur(void)
{
	if (vid_backend == VB_AVP) return avp_have ? &avp_wrap : NULL;
	return vid_tex_i >= 0 ? vid_tex[vid_tex_i] : NULL;
}

static void vid_init(void)
{
	q_init(&vq); q_init(&aq);
	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(vid_ff_log);
	pthread_t th;
	int r = vid_spawn(&th, vid_thread);
	if (!r) pthread_detach(th);
	trace("video thread %d\n", r);
	trace("video: control thread %d\n", r);
}
