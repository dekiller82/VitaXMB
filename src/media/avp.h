#pragma once

/* ------------------------------------------------------------------------------------------------------------------ */
/* The video player's first choice: the Vita's own player library (SceAvPlayer, as the Videos app and Vita-Media-Player */
/* use it). It decodes H.264 / AAC with the hardware, starts at once, and hands over frames as YUV that the GPU turns   */
/* into RGB itself. MP4 / MOV / M4V / 3GP only; anything it cannot open falls back to the FFmpeg engine in video.h.      */
/* Audio is pulled from the library and put into the music mixer's ring, paced so the library's clock stays right.      */
/* ------------------------------------------------------------------------------------------------------------------ */

/* in the SDK's library stub, but missing from its header */
#include <malloc.h>
extern int sceAvPlayerStreamCount(SceAvPlayerHandle handle);
extern int sceAvPlayerEnableStream(SceAvPlayerHandle handle, uint32_t id);
extern int sceAvPlayerDisableStream(SceAvPlayerHandle handle, uint32_t id);

#define AVP_EV_STOP    0x01
#define AVP_EV_READY   0x02
#define AVP_EV_PLAY    0x03
#define AVP_EV_PAUSE   0x04
#define AVP_EV_BUFFER  0x05
#define AVP_EV_TEXT    0x10

static SceAvPlayerHandle avp_h;                  /* the handle is a pointer: 0 is "none", and a valid one can look negative */
static volatile int avp_state;                    /* 0 nothing, 1 opening, 2 ready, 3 failed */
static volatile int avp_ended;
static volatile int avp_run;
static volatile int avp_flush;                    /* the audio ring must be emptied (after a jump) */
static pthread_t avp_pump_th;
static int avp_pump_on;
static int avp_audio_stream = -1, avp_video_stream = -1;

/* ---- memory the library asks for: frames live in GPU-mapped CDRAM ---- */
static void *avp_alloc(void *a, uint32_t align, uint32_t size) { (void)a; return memalign(align, size); }
static void avp_free(void *a, void *p) { (void)a; free(p); }

static void *avp_tex_alloc(void *a, uint32_t align, uint32_t size)
{
	(void)a;
	if (align < 0x40000) align = 0x40000;                      /* CDRAM blocks are 256 KB aligned */
	size = (size + align - 1) & ~(align - 1);
	SceKernelAllocMemBlockOpt opt;
	memset(&opt, 0, sizeof(opt));
	opt.size = sizeof(opt);
	opt.attr = 0x4;                                            /* has an alignment */
	opt.alignment = align;
	SceUID uid = sceKernelAllocMemBlock("xmb_avp", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, &opt);
	if (uid < 0) { xlog("avplayer: no CDRAM for a frame (%08x, %u bytes)\n", uid, size); return NULL; }
	void *mem = NULL;
	sceKernelGetMemBlockBase(uid, &mem);
	sceGxmMapMemory(mem, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
	return mem;
}

/* The library frees its frames from its own thread, but waiting for the GPU and unmapping memory next to the main thread's drawing crashed the
 * GPU library. The blocks are queued here and released by the main thread (avp_flush_frees, once per frame, outside a scene). */
#define AVP_PEND 32
static void *avp_pend[AVP_PEND];
static volatile int avp_pend_n;
static volatile int avp_lk;
static void avp_lock(void) { while (__sync_lock_test_and_set(&avp_lk, 1)) sceKernelDelayThread(500); }
static void avp_unlock(void) { __sync_lock_release(&avp_lk); }

static void avp_tex_free(void *a, void *mem)
{
	(void)a;
	if (!mem) return;
	for (;;) {
		avp_lock();
		if (avp_pend_n < AVP_PEND) { avp_pend[avp_pend_n++] = mem; avp_unlock(); return; }
		avp_unlock();
		sceKernelDelayThread(2000);
	}
}

static void avp_flush_frees(void)
{
	if (!avp_pend_n) return;
	void *list[AVP_PEND];
	avp_lock();
	int n = avp_pend_n;
	memcpy(list, avp_pend, (size_t)n * sizeof(void *));
	avp_pend_n = 0;
	avp_unlock();
	vita2d_wait_rendering_done();
	for (int i = 0; i < n; i++) {
		SceUID uid = sceKernelFindMemBlockByAddr(list[i], 0);
		sceGxmUnmapMemory(list[i]);
		if (uid >= 0) sceKernelFreeMemBlock(uid);
	}
}

/* ---- events (called on the library's own thread) ---- */
static void avp_on_ready(void)
{
	int n = sceAvPlayerStreamCount(avp_h);
	vid_n_a = vid_n_s = 0;
	avp_audio_stream = avp_video_stream = -1;
	for (int i = 0; i < n; i++) {
		SceAvPlayerStreamInfo si;
		memset(&si, 0, sizeof(si));
		if (sceAvPlayerGetStreamInfo(avp_h, (uint32_t)i, &si) < 0) continue;
		if (si.type == SCE_AVPLAYER_VIDEO && avp_video_stream < 0) {
			avp_video_stream = i;
			sceAvPlayerEnableStream(avp_h, (uint32_t)i);
			vid_w = (int)si.details.video.width; vid_h = (int)si.details.video.height;
			if (si.duration) vid_duration = (double)si.duration / 1000.0;
		} else if (si.type == SCE_AVPLAYER_AUDIO && vid_n_a < VID_TRACKS) {
			VTrack *t = &vid_atracks[vid_n_a];
			t->index = i; t->channels = si.details.audio.channelCount;
			uint32_t lc = si.details.audio.languageCode;
			t->lang[0] = (char)(lc & 0xFF); t->lang[1] = (char)((lc >> 8) & 0xFF); t->lang[2] = (char)((lc >> 16) & 0xFF); t->lang[3] = 0;
			if ((unsigned char)t->lang[0] < 'a' || (unsigned char)t->lang[0] > 'z') t->lang[0] = 0;
			snprintf(t->name, sizeof(t->name), "audio");
			if (avp_audio_stream < 0) { avp_audio_stream = i; sceAvPlayerEnableStream(avp_h, (uint32_t)i); vid_a_sel = vid_n_a; }
			if (!vid_duration && si.duration) vid_duration = (double)si.duration / 1000.0;
			vid_n_a++;
		} else if (si.type == SCE_AVPLAYER_TIMEDTEXT && vid_n_s < VID_TRACKS) {
			VTrack *t = &vid_stracks[vid_n_s];
			t->index = i; snprintf(t->lang, sizeof(t->lang), "%s", ""); snprintf(t->name, sizeof(t->name), "text");
			if (vid_n_s == 0) { sceAvPlayerEnableStream(avp_h, (uint32_t)i); vid_s_sel = 0; }
			vid_n_s++;
		}
	}
	vid_has_audio = avp_audio_stream >= 0;
	if (avp_video_stream < 0) { avp_state = 3; return; }
	sceAvPlayerStart(avp_h);
	avp_state = 2;
}

static void avp_event(void *p, int32_t id, int32_t src, void *data)
{
	(void)p; (void)src;
	switch (id) {
	case AVP_EV_READY: avp_on_ready(); break;
	case AVP_EV_STOP: if (avp_state == 2) avp_ended = 1; break;
	case AVP_EV_TEXT: {
		SceAvPlayerFrameInfo *f = (SceAvPlayerFrameInfo *)data;
		if (!f || !f->pData || !vid_cues || vid_n_cues >= VID_CUES) break;
		VCue *c = &vid_cues[vid_n_cues];
		int sz = f->details.subs.textSize;
		if (sz <= 0 || sz >= (int)sizeof(c->text)) break;
		memcpy(c->text, f->pData, (size_t)sz);
		c->text[sz] = 0;
		c->start_ms = (int)f->timeStamp; c->end_ms = (int)f->timeStamp + 4000;
		vid_n_cues++;
		break; }
	default: break;
	}
}

/* ---- video: the newest frame becomes the texture the GPU samples (YUV -> RGB in hardware) ---- */
static int avp_fetch_frame(void)
{
	SceAvPlayerFrameInfo f;
	memset(&f, 0, sizeof(f));
	if (!sceAvPlayerGetVideoData(avp_h, &f) || !f.pData) return 0;
	memset(&avp_wrap.gxm_tex, 0, sizeof(avp_wrap.gxm_tex));
	sceGxmTextureInitLinear(&avp_wrap.gxm_tex, f.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1, f.details.video.width, f.details.video.height, 0);
	sceGxmTextureSetMinFilter(&avp_wrap.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetMagFilter(&avp_wrap.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
	vid_w = (int)f.details.video.width; vid_h = (int)f.details.video.height;
	avp_have = 1;
	return 1;
}

/* ---- audio: pull PCM from the library and put it into the mixer's ring as 48 kHz stereo ---- */
static void *avp_pump(void *arg)
{
	(void)arg;
	sceKernelChangeThreadPriority(0, 0x10000108);
	SceAvPlayerFrameInfo fr;
	double rpos = 0;
	int16_t pl = 0, pr = 0;
	while (avp_run) {
		if (avp_flush) {
			avp_flush = 0;
			mus_playing = 0;
			sceKernelDelayThread(40000);
			mus_w = mus_r = 0; mus_consumed = 0;
			rpos = 0; pl = pr = 0;
		}
		unsigned fill = mus_w - mus_r;
		if (vid_paused || fill > 4800) { sceKernelDelayThread(3000); continue; }     /* keep about 100 ms buffered: the library paces itself on what we take */
		memset(&fr, 0, sizeof(fr));
		if (!sceAvPlayerGetAudioData(avp_h, &fr) || !fr.pData) { sceKernelDelayThread(2000); continue; }
		int ch = fr.details.audio.channelCount ? fr.details.audio.channelCount : 2;
		int hz = fr.details.audio.sampleRate ? (int)fr.details.audio.sampleRate : 48000;
		int nf = (int)(fr.details.audio.size / (unsigned)(ch * 2));
		const int16_t *in = (const int16_t *)fr.pData;
		if (nf <= 0) continue;
		double step = (double)hz / 48000.0;
		int guard = 0;
		for (;;) {
			int i0 = (int)floor(rpos);
			if (i0 + 1 >= nf || guard++ > 4096) break;
			double f = rpos - i0;
			int l0, r0, l1, r1;
			if (i0 < 0) { l0 = pl; r0 = pr; } else { l0 = in[i0 * ch]; r0 = in[i0 * ch + (ch > 1 ? 1 : 0)]; }
			l1 = in[(i0 + 1) * ch]; r1 = in[(i0 + 1) * ch + (ch > 1 ? 1 : 0)];
			while (MUS_RING - (mus_w - mus_r) < 2 && avp_run) sceKernelDelayThread(3000);
			unsigned w = mus_w & (MUS_RING - 1);
			int16_t cl = (int16_t)(l0 + (l1 - l0) * f), cr = (int16_t)(r0 + (r1 - r0) * f);
			if (vid_chan == 1) cr = cl; else if (vid_chan == 2) cl = cr;
			mus_ring[w * 2] = cl;
			mus_ring[w * 2 + 1] = cr;
			mus_w++;
			rpos += step;
		}
		rpos -= nf;
		pl = in[(nf - 1) * ch]; pr = in[(nf - 1) * ch + (ch > 1 ? 1 : 0)];
		if (!mus_playing && !vid_paused && !avp_flush && (int)(mus_w - mus_r) > 1500) mus_playing = 1;
	}
	return NULL;
}

/* ---- open / close ---- */
static void avp_close(void)
{
	avp_run = 0;
	avp_have = 0;
	if (avp_pump_on) { pthread_join(avp_pump_th, NULL); avp_pump_on = 0; }
	if (avp_h) {
		sceAvPlayerStop(avp_h);
		sceAvPlayerClose(avp_h);
		avp_h = 0;
	}
	mus_playing = 0;
	avp_state = 0; avp_ended = 0;
}

static int avp_open(const char *path)
{
	static int loaded;
	VSTAGE("starting the system player");
	if (!loaded) {
		int r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
		trace("avplayer: module load %08x\n", r);
		loaded = r >= 0 ? 1 : -1;
	}
	if (loaded < 0) return -1;

	SceAvPlayerInitData init;
	memset(&init, 0, sizeof(init));
	init.memoryReplacement.allocate = avp_alloc;
	init.memoryReplacement.deallocate = avp_free;
	init.memoryReplacement.allocateTexture = avp_tex_alloc;
	init.memoryReplacement.deallocateTexture = avp_tex_free;
	init.eventReplacement.eventCallback = avp_event;
	init.basePriority = 125;
	init.numOutputVideoFrameBuffers = 6;
	init.autoStart = SCE_FALSE;
	init.debugLevel = 0;

	vid_n_a = vid_n_s = vid_n_chapters = 0;
	vid_a_sel = 0; vid_s_sel = -1;
	vid_duration = 0; vid_bitrate = 0; vid_fps = 0; vid_w = vid_h = 0;
	vid_paused = 0; vid_speed = 1.0; vid_has_audio = 0; vid_warn = 0;
	free(vid_cues);
	vid_cues = calloc(VID_CUES, sizeof(VCue));
	vid_n_cues = 0;
	avp_state = 1; avp_ended = 0; avp_have = 0; avp_flush = 1;

	avp_h = sceAvPlayerInit(&init);
	if (!avp_h) { xlog("avplayer: init failed\n"); return -1; }
	VSTAGE("opening with the system player");
	int r = sceAvPlayerAddSource(avp_h, path);
	if (r < 0) { xlog("avplayer: add source failed %08x\n", r); return -1; }
	uint64_t t0 = vid_now_us();
	while (avp_state == 1 && vid_now_us() - t0 < 6000000ULL) sceKernelDelayThread(10000);
	if (avp_state != 2) { xlog("avplayer: no ready event (state %d)\n", avp_state); return -1; }

	if ((vid_w > vid_h ? vid_w : vid_h) > 1280 || (vid_w > vid_h ? vid_h : vid_w) > 720) {          /* above 720p it shows no picture: do not wait for one */
		trace("avplayer: %dx%d is above 720p\n", vid_w, vid_h);
		return -1;
	}
	avp_run = 1;                                           /* the library paces its pictures on the audio being taken: start taking it now */
	if (pthread_create(&avp_pump_th, NULL, avp_pump, NULL)) { xlog("avplayer: audio thread failed\n"); return -1; }
	avp_pump_on = 1;
	VSTAGE("waiting for the first picture");
	t0 = vid_now_us();
	while (!avp_have && vid_now_us() - t0 < 4000000ULL && !avp_ended) { avp_fetch_frame(); sceKernelDelayThread(8000); }
	if (!avp_have) { xlog("avplayer: no picture (the video may be too large or in a codec it cannot decode)\n"); return -1; }

	snprintf(vid_container, sizeof(vid_container), "mp4");
	snprintf(vid_vcodec, sizeof(vid_vcodec), "H.264");
	vid_hw = 1;
	vid_load_ext_subs(path);
	if (vid_s_sel < 0 && vid_n_cues && vid_n_s) vid_s_sel = vid_n_s - 1;      /* an external subtitle file */
	VSTAGE("playing (system player)");
	return 0;
}

static double avp_position(void) { return avp_h ? (double)sceAvPlayerCurrentTime(avp_h) / 1000.0 : 0.0; }

static void avp_seek(double to)
{
	if (!avp_h) return;
	if (to < 0) to = 0;
	if (vid_duration > 0 && to > vid_duration - 0.3) to = vid_duration > 0.6 ? vid_duration - 0.3 : 0;
	avp_flush = 1;
	sceAvPlayerJumpToTime(avp_h, (uint64_t)(to * 1000.0));
}

static void avp_pause(int p)
{
	if (!avp_h) return;
	if (p) { sceAvPlayerPause(avp_h); mus_playing = 0; }
	else { sceAvPlayerResume(avp_h); if (mus_w != mus_r) mus_playing = 1; }
}

static void avp_poll(void)
{
	if (!avp_h || vid_status != VS_PLAYING) return;
	if (avp_ended || !sceAvPlayerIsActive(avp_h)) vid_status = VS_ENDED;
}

static void avp_select_audio(int sel)
{
	if (!avp_h || sel < 0 || sel >= vid_n_a || sel == vid_a_sel) return;
	if (avp_audio_stream >= 0) sceAvPlayerDisableStream(avp_h, (uint32_t)avp_audio_stream);
	avp_audio_stream = vid_atracks[sel].index;
	sceAvPlayerEnableStream(avp_h, (uint32_t)avp_audio_stream);
	vid_a_sel = sel;
	avp_flush = 1;
}
