#pragma once

/* ------------------------------------------------------------------ */
/* Sound: pre-decoded 48 kHz s16 stereo PCM mixed on a worker thread   */
/* ------------------------------------------------------------------ */

enum { SND_OPENING, SND_CANCEL, SND_CURSOR, SND_CONFIRM, SND_STARTGAME, SND_CATEGORY, SND_OPTION, SND_ERROR, SND_COUNT };
static const char *snd_files[SND_COUNT] = { "opening", "cancel", "cursor", "confirm", "startgame", "category", "option", "error" };
/* a sound with no sample of its own (the firmware's, not a theme's) is played as this one */
static const signed char snd_fallback[SND_COUNT] = { -1, -1, -1, -1, -1, SND_CURSOR, -1, SND_CANCEL };

typedef struct { int16_t *data; int frames; volatile int pos; } Sound;
static Sound sounds[SND_COUNT];
static volatile int audio_run = 1;

#define AUDIO_GRAIN 1024

/* Music: the decoder thread fills this ring with 48 kHz stereo; the mixer drains it. */
#define MUS_RING 65536
static int16_t mus_ring[MUS_RING * 2];
static volatile unsigned mus_w, mus_r;
static volatile int mus_playing;
static volatile uint64_t mus_consumed;
#define VIS_N 1024
static int16_t vis_buf[VIS_N];
static volatile unsigned vis_w;

/* Reads the app's own sample for a slot into a buffer (NULL if there is none). */
static int16_t *sound_read_asset(int i, int *frames)
{
	char path[96];
	snprintf(path, sizeof(path), "app0:assets/sounds/%s.pcm", snd_files[i]);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return NULL;
	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	int16_t *buf = size > 0 ? malloc(size) : NULL;
	if (buf && sceIoRead(fd, buf, size) != size) { free(buf); buf = NULL; }
	sceIoClose(fd);
	if (buf) *frames = size / 4;
	return buf;
}

static void sound_load_all(void)
{
	for (int i = 0; i < SND_COUNT; i++) {
		if (sounds[i].data) continue;                              /* already there */
		char path[96];
		snprintf(path, sizeof(path), "app0:assets/sounds/%s.pcm", snd_files[i]);
		SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (fd < 0) continue;
		int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
		sceIoLseek32(fd, 0, SCE_SEEK_SET);
		int16_t *buf = malloc(size);
		if (buf && sceIoRead(fd, buf, size) == size) {
			sounds[i].data = buf;
			sounds[i].frames = size / 4;
			sounds[i].pos = sounds[i].frames; /* idle */
		} else {
			free(buf);
		}
		sceIoClose(fd);
	}
}

static void sound_play(int id)
{
	if (!sound_on) return;
	if (!sounds[id].data && snd_fallback[id] >= 0) id = snd_fallback[id];
	if (sounds[id].data) sounds[id].pos = 0;
}

/* ---- a theme's sounds: Sony ADPCM ("VAGp"), as stored in system_plugin.rco ---- */

/* Decodes a VAG file to 48 kHz stereo s16 (linear resampling); NULL if it is not one. */
static int16_t *vag_to_pcm(const uint8_t *v, size_t len, int *frames)
{
	if (len < 0x30 + 16 || memcmp(v, "VAGp", 4) != 0) return NULL;
	uint32_t size = (uint32_t)v[12] << 24 | v[13] << 16 | v[14] << 8 | v[15];
	uint32_t rate = (uint32_t)v[16] << 24 | v[17] << 16 | v[18] << 8 | v[19];
	if (rate < 4000 || rate > 96000) return NULL;
	size_t avail = len - 0x30;
	if (size == 0 || size > avail) size = (uint32_t)avail;
	size_t blocks = size / 16;
	if (blocks == 0 || blocks > 400000) return NULL;
	int16_t *mono = malloc(blocks * 28 * sizeof(int16_t));
	if (!mono) return NULL;
	static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
	int s1 = 0, s2 = 0;
	size_t n = 0;
	for (size_t b = 0; b < blocks; b++) {
		const uint8_t *p = v + 0x30 + b * 16;
		int shift = p[0] & 15, filt = p[0] >> 4;
		if (filt > 4) filt = 4;
		if (p[1] == 7) break;                                   /* end of sample */
		for (int i = 0; i < 28; i++) {
			int nib = (i & 1) ? (p[2 + i / 2] >> 4) : (p[2 + i / 2] & 15);
			int s = (int)(int16_t)(nib << 12) >> shift;
			s += (s1 * f0[filt] + s2 * f1[filt]) / 64;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			s2 = s1; s1 = s;
			mono[n++] = (int16_t)s;
		}
	}
	if (n < 2) { free(mono); return NULL; }
	int out_frames = (int)((uint64_t)n * 48000 / rate);
	int16_t *pcm = malloc((size_t)out_frames * 4);
	if (!pcm) { free(mono); return NULL; }
	for (int i = 0; i < out_frames; i++) {
		double pos = (double)i * rate / 48000.0;
		size_t i0 = (size_t)pos;
		double fr = pos - (double)i0;
		int a = mono[i0 < n ? i0 : n - 1], c = mono[i0 + 1 < n ? i0 + 1 : n - 1];
		int16_t v16 = (int16_t)(a + (c - a) * fr);
		pcm[i * 2] = v16; pcm[i * 2 + 1] = v16;
	}
	free(mono);
	*frames = out_frames;
	return pcm;
}

/* Puts a decoded sample into a slot while the mixer may be reading it: the old buffer is freed later, not now. */
static int16_t *snd_graveyard[16];
static int snd_grave_n;
static void sound_set(int id, int16_t *pcm, int frames)
{
	Sound *s = &sounds[id];
	trace("sound slot %d <- %d frames\n", id, frames);
	int16_t *old = s->data;
	s->frames = 0;
	s->data = pcm;
	s->frames = frames;
	s->pos = frames;                                            /* idle */
	if (old) {
		if (snd_grave_n == 16) { free(snd_graveyard[0]); memmove(snd_graveyard, snd_graveyard + 1, sizeof(snd_graveyard) - sizeof(snd_graveyard[0])); snd_grave_n--; }
		snd_graveyard[snd_grave_n++] = old;
	}
}

static int sound_playing(int id)
{
	return sounds[id].data && sounds[id].pos < sounds[id].frames;
}

static int audio_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) return 0;
	static int16_t mix[AUDIO_GRAIN * 2];
	int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

	while (audio_run) {
		int32_t acc[AUDIO_GRAIN * 2];
		memset(acc, 0, sizeof(acc));
		for (int i = 0; i < SND_COUNT; i++) {
			Sound *s = &sounds[i];
			int pos = s->pos;
			if (!s->data || pos >= s->frames) continue;
			int n = s->frames - pos;
			if (n > AUDIO_GRAIN) n = AUDIO_GRAIN;
			for (int k = 0; k < n * 2; k++) acc[k] += s->data[pos * 2 + k];
			s->pos = pos + n;
		}
		if (mus_playing) {
			unsigned avail = mus_w - mus_r;
			unsigned n = avail < AUDIO_GRAIN ? avail : AUDIO_GRAIN;
			for (unsigned i = 0; i < n; i++) {
				unsigned idx = (mus_r + i) & (MUS_RING - 1);
				int l = mus_ring[idx * 2], r = mus_ring[idx * 2 + 1];
				acc[i * 2] += l * 85 / 100;
				acc[i * 2 + 1] += r * 85 / 100;
				vis_buf[(vis_w + i) % VIS_N] = (int16_t)((l + r) / 2);
			}
			vis_w += n; mus_r += n; mus_consumed += n;
		}
		for (int k = 0; k < AUDIO_GRAIN * 2; k++)
			mix[k] = acc[k] > 32767 ? 32767 : (acc[k] < -32768 ? -32768 : acc[k]);
		sceAudioOutOutput(port, mix);
	}
	sceAudioOutReleasePort(port);
	return 0;
}
