#pragma once

/* ------------------------------------------------------------------ */
/* Music: MP3 decoder thread -> PCM ring -> audio mixer, plus the UI   */
/* ------------------------------------------------------------------ */

enum { MC_NONE, MC_LOAD, MC_SEEK, MC_STOP };
static volatile int mus_cmd;
static char mus_req_path[256];
static volatile int mus_req_seek_ms;
static volatile int mus_total_ms;
static volatile int mus_ready;          /* a file is open and being played/paused */
static volatile int mus_eof;            /* decoder reached the end of the file */
static volatile unsigned mus_base_ms;   /* where playback started (after a seek) */
static volatile int mus_run = 1;
static int music_index = -1;            /* index of the playing row in menus[M_TRACKS] */

/* ---- streamed file reader: a 64 KB window over the file, so decoders never need the whole file in RAM ---- */
#define BF_WIN (64 * 1024)
typedef struct { SceUID fd; int size, pos, wstart, wlen, err; uint8_t *win; } BFile;

static int bf_open(BFile *b, const char *path)
{
	memset(b, 0, sizeof(*b));
	b->fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (b->fd < 0) return -1;
	b->size = sceIoLseek32(b->fd, 0, SCE_SEEK_END);
	b->win = malloc(BF_WIN);
	if (!b->win || b->size <= 0) { sceIoClose(b->fd); free(b->win); b->win = NULL; b->fd = -1; return -1; }
	return 0;
}
static void bf_close(BFile *b)
{
	if (b->fd >= 0 && b->win) sceIoClose(b->fd);
	free(b->win); b->win = NULL; b->fd = -1;
}
static void bf_seek(BFile *b, int64_t pos) { b->pos = pos < 0 ? 0 : pos > b->size ? b->size : (int)pos; }
static int bf_read(BFile *b, void *dst, int n)
{
	uint8_t *o = dst;
	int got = 0;
	while (got < n && b->pos < b->size) {
		if (b->pos < b->wstart || b->pos >= b->wstart + b->wlen) {
			int r = 0;
			for (int tries = 0; tries < 4; tries++) {          /* a failed SD read is retried rather than ending the song */
				sceIoLseek32(b->fd, b->pos, SCE_SEEK_SET);
				r = sceIoRead(b->fd, b->win, BF_WIN);
				if (r > 0) break;
				b->err = r;
				sceKernelDelayThread(20000);
			}
			if (r <= 0) break;
			b->wstart = b->pos; b->wlen = r;
		}
		int off = b->pos - b->wstart, k = b->wlen - off;
		if (k > n - got) k = n - got;
		memcpy(o + got, b->win + off, k);
		got += k; b->pos += k;
	}
	return got;
}
static size_t bf_mp3_read(void *buf, size_t size, void *u) { return (size_t)bf_read(u, buf, (int)size); }
static int bf_mp3_seek(uint64_t pos, void *u) { bf_seek(u, (int64_t)pos); return 0; }
static size_t bf_flac_read(void *u, void *buf, size_t n) { return (size_t)bf_read(u, buf, (int)n); }
static drflac_bool32 bf_flac_seek(void *u, int off, drflac_seek_origin o)
{
	BFile *b = u;
	bf_seek(b, o == DRFLAC_SEEK_SET ? (int64_t)off : o == DRFLAC_SEEK_END ? (int64_t)b->size + off : (int64_t)b->pos + off);
	return 1;
}
static drflac_bool32 bf_flac_tell(void *u, drflac_int64 *cur) { *cur = ((BFile *)u)->pos; return 1; }

/* ---- decoders: MP3 (minimp3), FLAC (dr_flac), Ogg Vorbis (stb_vorbis), WAV / AIFF (raw PCM, parsed here) ---- */
enum { DF_NONE, DF_MP3, DF_FLAC, DF_OGG, DF_PCM };
#define PCM_MAX_CH 8
typedef struct {
	int fmt, hz, ch;
	uint64_t total_frames;
	BFile bf;
	uint64_t pos_frames;                                 /* frames decoded so far, for the early-end log */
	mp3dec_io_t io;
	mp3dec_ex_t mp3;
	drflac *flac; int flac_retry;
	stb_vorbis *ogg; uint8_t *ogg_mem;                  /* Ogg is small, so it is decoded from memory */
	int64_t pcm_off; uint64_t pcm_frames, pcm_pos;       /* DF_PCM: where the sample data sits in the file */
	int bytes, big, is_float, unsigned8, src_ch;
} MusDec;

static uint32_t rd_le(const uint8_t *p, int n) { uint32_t v = 0; for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static uint32_t rd_be(const uint8_t *p, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v = (v << 8) | p[i]; return v; }

static int pcm_open(MusDec *d)
{
	BFile *bf = &d->bf;
	uint8_t h[12], c[8], b[64];
	if (bf_read(bf, h, 12) != 12) return -1;
	int aiff = !memcmp(h, "FORM", 4);
	if (!aiff && (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))) return -1;
	int have_fmt = 0, tag = 0, bits = 0;
	int64_t data_off = 0, dlen = -1;
	d->big = aiff;
	for (int64_t pos = 12; pos + 8 <= bf->size;) {
		bf_seek(bf, pos);
		if (bf_read(bf, c, 8) != 8) break;
		int64_t cl = aiff ? rd_be(c + 4, 4) : rd_le(c + 4, 4);
		if (pos + 8 + cl > bf->size) cl = bf->size - pos - 8;      /* truncated file: take what is there */
		int rn = cl < (int64_t)sizeof(b) ? (int)cl : (int)sizeof(b);
		if (!aiff && !memcmp(c, "fmt ", 4) && cl >= 16) {
			bf_read(bf, b, rn);
			tag = rd_le(b, 2); d->src_ch = rd_le(b + 2, 2); d->hz = rd_le(b + 4, 4); bits = rd_le(b + 14, 2);
			if (tag == 0xFFFE && rn >= 26) tag = rd_le(b + 24, 2);
			have_fmt = 1;
		} else if (aiff && !memcmp(c, "COMM", 4) && cl >= 18) {
			bf_read(bf, b, rn);
			d->src_ch = rd_be(b, 2); bits = rd_be(b + 6, 2);
			uint32_t ex = rd_be(b + 8, 2) & 0x7FFF;
			uint64_t mant = ((uint64_t)rd_be(b + 10, 4) << 32) | rd_be(b + 14, 4);
			d->hz = (int)ldexp((double)mant, (int)ex - 16383 - 63);
			tag = 1;
			if (!memcmp(h + 8, "AIFC", 4) && rn >= 22) {
				if (!memcmp(b + 18, "sowt", 4)) d->big = 0;
				else if (!memcmp(b + 18, "fl32", 4) || !memcmp(b + 18, "FL32", 4)) tag = 3;
				else if (memcmp(b + 18, "NONE", 4) && memcmp(b + 18, "none", 4)) return -1;   /* compressed AIFC */
			}
			have_fmt = 1;
		} else if (!memcmp(c, aiff ? "SSND" : "data", 4)) {
			data_off = pos + 8; dlen = cl;
			if (aiff && cl >= 8) {
				bf_read(bf, b, 8);
				uint32_t off = rd_be(b, 4);
				data_off += 8 + off; dlen = cl - 8 - (int64_t)off;
			}
			if (have_fmt) break;
		}
		pos += 8 + cl + (cl & 1);
	}
	if (!have_fmt || dlen <= 0 || d->src_ch < 1 || d->src_ch > PCM_MAX_CH || d->hz < 1000 || bits < 8 || bits > 32 || (bits & 7)) return -1;
	if (tag != 1 && !(tag == 3 && bits == 32)) return -1;
	d->is_float = tag == 3;
	d->bytes = bits / 8;
	d->unsigned8 = !aiff && bits == 8;
	d->ch = d->src_ch > 1 ? 2 : 1;
	d->pcm_off = data_off;
	d->pcm_frames = (uint64_t)dlen / ((uint64_t)d->bytes * d->src_ch);
	d->pcm_pos = 0;
	d->total_frames = d->pcm_frames;
	return d->pcm_frames ? 0 : -1;
}

static int16_t pcm_sample(const MusDec *d, const uint8_t *p)
{
	int n = d->bytes;
	if (d->is_float) {
		union { uint32_t u; float f; } v;
		v.u = d->big ? rd_be(p, 4) : rd_le(p, 4);
		float x = v.f * 32767.0f;
		return (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : x);
	}
	if (n == 1) return d->unsigned8 ? (int16_t)((p[0] - 128) << 8) : (int16_t)((int8_t)p[0] << 8);
	return (int16_t)(d->big ? rd_be(p, 2) : rd_le(p + n - 2, 2));   /* keep the top 16 bits */
}

static void dec_close(MusDec *d)
{
	if (d->fmt == DF_MP3) mp3dec_ex_close(&d->mp3);
	else if (d->fmt == DF_FLAC && d->flac) drflac_close(d->flac);
	else if (d->fmt == DF_OGG && d->ogg) stb_vorbis_close(d->ogg);
	free(d->ogg_mem);
	bf_close(&d->bf);
	memset(d, 0, sizeof(*d));
	d->bf.fd = -1;
}

static int dec_open(MusDec *d, const char *path)
{
	memset(d, 0, sizeof(*d));
	const char *dot = strrchr(path, '.');
	if (!dot || bf_open(&d->bf, path)) { d->bf.fd = -1; return -1; }
	int ok = 0;
	if (!strcasecmp(dot, ".mp3")) {
		d->io.read = bf_mp3_read; d->io.read_data = &d->bf;
		d->io.seek = bf_mp3_seek; d->io.seek_data = &d->bf;
		if (mp3dec_ex_open_cb(&d->mp3, &d->io, MP3D_SEEK_TO_SAMPLE) == 0) {
			d->fmt = DF_MP3;
			d->hz = d->mp3.info.hz ? d->mp3.info.hz : 44100;
			d->ch = d->mp3.info.channels ? d->mp3.info.channels : 2;
			d->total_frames = d->mp3.samples / d->ch;
			ok = 1;
		}
	} else if (!strcasecmp(dot, ".flac")) {
		d->flac = drflac_open(bf_flac_read, bf_flac_seek, bf_flac_tell, &d->bf, NULL);
		if (d->flac && d->flac->channels <= 2) {
			d->fmt = DF_FLAC;
			d->hz = (int)d->flac->sampleRate; d->ch = d->flac->channels;
			d->total_frames = d->flac->totalPCMFrameCount;
			ok = 1;
		} else if (d->flac) { drflac_close(d->flac); d->flac = NULL; }
	} else if (!strcasecmp(dot, ".ogg")) {
		d->ogg_mem = malloc(d->bf.size);
		if (d->ogg_mem && bf_read(&d->bf, d->ogg_mem, d->bf.size) == d->bf.size) {
			int err = 0;
			d->ogg = stb_vorbis_open_memory(d->ogg_mem, d->bf.size, &err, NULL);
			if (d->ogg) {
				stb_vorbis_info vi = stb_vorbis_get_info(d->ogg);
				d->fmt = DF_OGG;
				d->hz = (int)vi.sample_rate; d->ch = vi.channels > 1 ? 2 : 1;   /* more channels are downmixed by stb_vorbis */
				d->total_frames = stb_vorbis_stream_length_in_samples(d->ogg);
				ok = d->hz > 0;
			}
		}
	} else if (pcm_open(d) == 0) {
		d->fmt = DF_PCM;
		ok = 1;
	}
	if (!ok) { dec_close(d); return -1; }
	return 0;
}

/* Reads up to `frames` frames of interleaved s16 (d->ch channels). Returns frames read, 0 at the end. */
static int dec_read_raw(MusDec *d, int16_t *out, int frames)
{
	if (d->fmt == DF_MP3) return (int)(mp3dec_ex_read(&d->mp3, out, (size_t)frames * d->ch) / d->ch);
	if (d->fmt == DF_FLAC) {
		int n = (int)drflac_read_pcm_frames_s16(d->flac, frames, out);
		/* A good file sometimes made the decoder return nothing in the middle of a song (no read error behind it; the same file decodes
		 * fine elsewhere). Open it again at the same frame and go on, a few times at most. */
		if (n <= 0 && d->total_frames && d->pos_frames + 44100 < d->total_frames && d->flac_retry < 4) {
			uint64_t at = d->pos_frames;
			d->flac_retry++;
			drflac_close(d->flac);
			d->flac = drflac_open(bf_flac_read, bf_flac_seek, bf_flac_tell, &d->bf, NULL);
			if (d->flac && drflac_seek_to_pcm_frame(d->flac, at)) {
				n = (int)drflac_read_pcm_frames_s16(d->flac, frames, out);
				xlog("flac: stopped at frame %llu of %llu, reopened (try %d): %s\n", (unsigned long long)at, (unsigned long long)d->total_frames, d->flac_retry, n > 0 ? "continuing" : "no better");
			} else xlog("flac: stopped at frame %llu and could not be reopened\n", (unsigned long long)at);
		}
		return n;
	}
	if (d->fmt == DF_OGG) return stb_vorbis_get_samples_short_interleaved(d->ogg, d->ch, out, frames * d->ch);
	if (d->fmt == DF_PCM) {
		static uint8_t raw[1152 * PCM_MAX_CH * 4];
		int fs = d->bytes * d->src_ch;
		if (d->pcm_pos + frames > d->pcm_frames) frames = (int)(d->pcm_frames - d->pcm_pos);
		bf_seek(&d->bf, d->pcm_off + (int64_t)d->pcm_pos * fs);
		frames = bf_read(&d->bf, raw, frames * fs) / fs;
		const uint8_t *p = raw;
		for (int i = 0; i < frames; i++, p += fs) {
			out[i * d->ch] = pcm_sample(d, p);
			if (d->ch > 1) out[i * d->ch + 1] = pcm_sample(d, p + d->bytes);
		}
		d->pcm_pos += frames;
		return frames;
	}
	return 0;
}

static int dec_read(MusDec *d, int16_t *out, int frames)
{
	int n = dec_read_raw(d, out, frames);
	if (n > 0) d->pos_frames += n;
	return n;
}

static void dec_seek(MusDec *d, uint64_t frame)
{
	d->pos_frames = frame;
	if (d->fmt == DF_MP3) mp3dec_ex_seek(&d->mp3, frame * d->ch);
	else if (d->fmt == DF_FLAC) drflac_seek_to_pcm_frame(d->flac, frame);
	else if (d->fmt == DF_OGG) stb_vorbis_seek(d->ogg, (unsigned)frame);
	else if (d->fmt == DF_PCM) d->pcm_pos = frame < d->pcm_frames ? frame : d->pcm_frames;
}

static int music_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	trace("music thread running\n");
	static MusDec dec;
	static int16_t pcm[2304 * 2];
	int open = 0, hz = 44100, ch = 2;
	double rpos = 0;
	int16_t prev_l = 0, prev_r = 0;

	while (mus_run) {
		int cmd = mus_cmd;
		if (cmd != MC_NONE) {
			mus_cmd = MC_NONE;
			if (cmd == MC_LOAD || cmd == MC_STOP) {
				mus_playing = 0;
				sceKernelDelayThread(50000);                  /* let the mixer finish its current grain */
				if (open) { dec_close(&dec); open = 0; }
				mus_w = mus_r = 0; mus_consumed = 0; mus_ready = 0; mus_eof = 0; mus_total_ms = 0; mus_base_ms = 0;
			}
			if (cmd == MC_LOAD) {
				trace("music load %s\n", mus_req_path);
				if (dec_open(&dec, mus_req_path) == 0) {
					open = 1;
					hz = dec.hz;
					ch = dec.ch;
					mus_total_ms = (int)(dec.total_frames * 1000ULL / hz);
					trace("audio opened: %d ms, %d Hz, %d ch\n", (int)mus_total_ms, hz, ch);
					rpos = 0; prev_l = prev_r = 0;
					mus_ready = 1;
					mus_playing = 1;
				} else {
					trace("audio open FAILED\n");
					xlog("audio open failed %s\n", mus_req_path);
					mus_eof = 1;
				}
			} else if (cmd == MC_SEEK && open) {
				int was = mus_playing;
				mus_playing = 0;
				sceKernelDelayThread(50000);
				mus_w = mus_r = 0; mus_consumed = 0; mus_eof = 0;
				int ms = mus_req_seek_ms;
				if (ms < 0) ms = 0;
				if (ms > mus_total_ms - 200) ms = mus_total_ms > 200 ? mus_total_ms - 200 : 0;
				dec_seek(&dec, (uint64_t)ms * hz / 1000);
				mus_base_ms = (unsigned)ms;
				rpos = 0;
				mus_playing = was;
			}
		}

		if (open && !mus_eof && (MUS_RING - (mus_w - mus_r)) > 4096) {
			int nf = dec_read(&dec, pcm, 1152);
			if (nf <= 0) {
				if (dec.total_frames && dec.pos_frames + hz < dec.total_frames)      /* more than a second short: not a normal end */
					xlog("audio ended early %s: frame %llu of %llu, fmt %d, io err %08x\n", mus_req_path,
					     (unsigned long long)dec.pos_frames, (unsigned long long)dec.total_frames, dec.fmt, (unsigned)dec.bf.err);
				mus_eof = 1; continue;
			}
			/* to 48 kHz stereo, linear interpolation */
			double step = (double)hz / 48000.0;
			int guard = 0;
			for (;;) {
				int i0 = (int)floor(rpos);
				if (i0 + 1 >= nf || guard++ > 4000) break;
				double f = rpos - i0;
				int l0, r0, l1, r1;
				if (i0 < 0) { l0 = prev_l; r0 = prev_r; }
				else { l0 = pcm[i0 * ch]; r0 = pcm[i0 * ch + (ch > 1 ? 1 : 0)]; }
				l1 = pcm[(i0 + 1) * ch]; r1 = pcm[(i0 + 1) * ch + (ch > 1 ? 1 : 0)];
				unsigned w = mus_w & (MUS_RING - 1);
				mus_ring[w * 2]     = (int16_t)(l0 + (l1 - l0) * f);
				mus_ring[w * 2 + 1] = (int16_t)(r0 + (r1 - r0) * f);
				mus_w++;
				rpos += step;
			}
			rpos -= nf;
			prev_l = pcm[(nf - 1) * ch]; prev_r = pcm[(nf - 1) * ch + (ch > 1 ? 1 : 0)];
		} else {
			sceKernelDelayThread(6000);
		}
	}
	return 0;
}

static unsigned music_elapsed_ms(void)
{
	return mus_base_ms + (unsigned)(mus_consumed * 1000ULL / 48000ULL);
}

static void music_start(int index)
{
	trace("music_start(%d) count=%d\n", index, menus[M_TRACKS].count);
	if (index < 0 || index >= menus[M_TRACKS].count) return;
	music_index = index;
	snprintf(mus_req_path, sizeof(mus_req_path), "%s", menus[M_TRACKS].items[index].path);
	mus_cmd = MC_LOAD;
}
static void music_stop(void) { mus_cmd = MC_STOP; music_index = -1; }

/* Re-reads the music and video lists (the Extra Storage setting changed). */
static void rescan_media(void)
{
	if (mus_ready) music_stop();
	scan_videos();
	scan_music_wrapper();
	menus[M_VIDEOS].pos = (float)menus[M_VIDEOS].sel;
	menus[M_TRACKS].pos = (float)menus[M_TRACKS].sel;
}
static void music_toggle(void) { if (mus_ready) mus_playing = !mus_playing; }
static void music_seek_rel(int delta_ms)
{
	if (!mus_ready) return;
	int target = (int)music_elapsed_ms() + delta_ms;
	mus_req_seek_ms = target;
	mus_cmd = MC_SEEK;
}

/* ---- ID3 tags ---- */
static void id3_text(const uint8_t *d, int len, char *out, size_t cap)
{
	out[0] = 0;
	if (len < 2) return;
	int enc = d[0];
	const uint8_t *p = d + 1;
	int n = len - 1;
	size_t o = 0;
	if (enc == 0 || enc == 3) {                       /* Latin-1 / UTF-8 */
		for (int i = 0; i < n && o + 4 < cap && p[i]; i++) {
			if (enc == 0 && p[i] >= 0x80) { out[o++] = (char)(0xC0 | (p[i] >> 6)); out[o++] = (char)(0x80 | (p[i] & 0x3F)); }
			else out[o++] = (char)p[i];
		}
	} else {                                           /* UTF-16 (BOM or BE) */
		int le = 1, i = 0;
		if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { le = 0; i = 2; }
		else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { le = 1; i = 2; }
		else if (enc == 2) le = 0;
		for (; i + 1 < n && o + 4 < cap; i += 2) {
			unsigned cp = le ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1]);
			if (!cp) break;
			if (cp < 0x80) out[o++] = (char)cp;
			else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
			else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
		}
	}
	out[o] = 0;
}

static void id3_read(const char *path, char *title, size_t tn, char *artist, size_t an)
{
	title[0] = artist[0] = 0;
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return;
	uint8_t h[10];
	if (sceIoRead(fd, h, 10) == 10 && !memcmp(h, "ID3", 3)) {
		int ver = h[3];
		uint32_t size = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
		if (size > 0 && size < 400 * 1024) {
			uint8_t *tag = malloc(size);
			if (tag && sceIoRead(fd, tag, size) == (int)size) {
				uint32_t pos = 0;
				while (pos + 10 < size) {
					const uint8_t *fh = tag + pos;
					if (!fh[0]) break;
					uint32_t fs = ver == 4 ? ((fh[4] << 21) | (fh[5] << 14) | (fh[6] << 7) | fh[7])
					                       : ((fh[4] << 24) | (fh[5] << 16) | (fh[6] << 8) | fh[7]);
					if (fs == 0 || pos + 10 + fs > size) break;
					if (!memcmp(fh, "TIT2", 4)) id3_text(fh + 10, (int)fs, title, tn);
					else if (!memcmp(fh, "TPE1", 4)) id3_text(fh + 10, (int)fs, artist, an);
					pos += 10 + fs;
				}
			}
			free(tag);
		}
	}
	if (!title[0]) {                                  /* ID3v1 fallback */
		int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
		if (size > 128) {
			sceIoLseek32(fd, size - 128, SCE_SEEK_SET);
			uint8_t t[128];
			if (sceIoRead(fd, t, 128) == 128 && !memcmp(t, "TAG", 3)) {
				snprintf(title, tn, "%.30s", (char *)t + 3);
				snprintf(artist, an, "%.30s", (char *)t + 33);
			}
		}
	}
	sceIoClose(fd);
}

/* ---- tags for the other formats: Vorbis comments (FLAC, Ogg), RIFF INFO (WAV), NAME / AUTH (AIFF) ---- */
static void meta_copy(char *out, size_t cap, const uint8_t *s, int n, int latin1)
{
	size_t o = 0;
	for (int i = 0; i < n && s[i] && o + 3 < cap; i++) {
		if (latin1 && s[i] >= 0x80) { out[o++] = (char)(0xC0 | (s[i] >> 6)); out[o++] = (char)(0x80 | (s[i] & 0x3F)); }
		else out[o++] = (char)s[i];
	}
	out[o] = 0;
}

/* p points at the vendor length of a Vorbis comment block */
static void vorbis_comments(const uint8_t *p, int n, char *title, size_t tn, char *artist, size_t an)
{
	if (n < 8) return;
	uint32_t vl = rd_le(p, 4);
	if (vl > (uint32_t)n - 8) return;
	uint32_t cnt = rd_le(p + 4 + vl, 4), pos = 8 + vl;
	for (uint32_t i = 0; i < cnt && pos + 4 <= (uint32_t)n; i++) {
		uint32_t l = rd_le(p + pos, 4);
		pos += 4;
		if (l > (uint32_t)n - pos) break;
		const char *s = (const char *)p + pos;
		if (l > 6 && !strncasecmp(s, "TITLE=", 6) && !title[0]) meta_copy(title, tn, p + pos + 6, l - 6, 0);
		else if (l > 7 && !strncasecmp(s, "ARTIST=", 7) && !artist[0]) meta_copy(artist, an, p + pos + 7, l - 7, 0);
		pos += l;
	}
}

static void meta_read(const char *path, char *title, size_t tn, char *artist, size_t an)
{
	title[0] = artist[0] = 0;
	const char *dot = strrchr(path, '.');
	if (!dot) return;
	if (!strcasecmp(dot, ".mp3")) { id3_read(path, title, tn, artist, an); return; }
	BFile bf;
	if (bf_open(&bf, path)) return;
	uint8_t h[12];
	if (!strcasecmp(dot, ".flac")) {
		if (bf_read(&bf, h, 4) == 4 && !memcmp(h, "fLaC", 4)) {
			for (int blocks = 0; blocks < 64; blocks++) {
				if (bf_read(&bf, h, 4) != 4) break;
				int last = h[0] >> 7, type = h[0] & 0x7F, len = (int)rd_be(h + 1, 3);
				int next = bf.pos + len;
				if (type == 4 && len < 256 * 1024) {
					uint8_t *vc = malloc(len);
					if (vc && bf_read(&bf, vc, len) == len) vorbis_comments(vc, len, title, tn, artist, an);
					free(vc);
					break;
				}
				if (last) break;
				bf_seek(&bf, next);
			}
		}
	} else if (!strcasecmp(dot, ".ogg")) {
		int n = bf.size < 65536 ? bf.size : 65536;
		uint8_t *buf = malloc(n);
		if (buf && bf_read(&bf, buf, n) == n)
			for (int i = 0; i + 7 < n; i++)
				if (!memcmp(buf + i, "\x03vorbis", 7)) { vorbis_comments(buf + i + 7, n - i - 7, title, tn, artist, an); break; }
		free(buf);
	} else if (bf_read(&bf, h, 12) == 12) {
		int aiff = !memcmp(h, "FORM", 4);
		if (aiff || (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4))) {
			uint8_t c[8];
			for (int64_t pos = 12; pos + 8 <= bf.size && (!title[0] || !artist[0]);) {
				bf_seek(&bf, pos);
				if (bf_read(&bf, c, 8) != 8) break;
				int64_t cl = aiff ? rd_be(c + 4, 4) : rd_le(c + 4, 4);
				if (cl < 0 || pos + 8 + cl > bf.size) cl = bf.size - pos - 8;
				if (aiff && !memcmp(c, "NAME", 4) && cl < 200) { uint8_t t[200]; int r = bf_read(&bf, t, (int)cl); meta_copy(title, tn, t, r, 1); }
				else if (aiff && !memcmp(c, "AUTH", 4) && cl < 200) { uint8_t t[200]; int r = bf_read(&bf, t, (int)cl); meta_copy(artist, an, t, r, 1); }
				else if (!aiff && !memcmp(c, "LIST", 4) && cl > 4 && cl < 16 * 1024) {
					uint8_t *l = malloc(cl);
					if (l && bf_read(&bf, l, (int)cl) == (int)cl && !memcmp(l, "INFO", 4))
						for (int64_t q = 4; q + 8 <= cl;) {
							int64_t sl = rd_le(l + q + 4, 4);
							if (q + 8 + sl > cl) break;
							if (!memcmp(l + q, "INAM", 4)) meta_copy(title, tn, l + q + 8, (int)sl, 1);
							else if (!memcmp(l + q, "IART", 4)) meta_copy(artist, an, l + q + 8, (int)sl, 1);
							q += 8 + sl + (sl & 1);
						}
					free(l);
				}
				pos += 8 + cl + (cl & 1);
			}
		}
	}
	bf_close(&bf);
}

/* The same song reached twice is listed once: the Vita's Music folder is open as music0: and as ux0:music, and a card or the PSP's MUSIC folder often holds a
 * copy; two files with the same name (any case) and size are one song, the first found wins. */
static uint32_t mus_seen_h[MAX_ITEMS];
static uint32_t mus_seen_sz[MAX_ITEMS];
static int mus_seen_n;

static uint32_t mus_name_hash(const char *n)
{
	uint32_t h = 2166136261u;
	for (; *n; n++) h = (h ^ (uint32_t)(unsigned char)tolower((unsigned char)*n)) * 16777619u;
	return h;
}

static void scan_music_dir(const char *dir, int depth)
{
	SceUID d = sceIoDopen(dir);
	trace("music scan %s -> %08x\n", dir, d);
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0 && menus[M_TRACKS].count < MAX_ITEMS) {
		trace("  entry %s mode=%x\n", e.d_name, (unsigned)e.d_stat.st_mode);
		char full[256];
		snprintf(full, sizeof(full), "%s/%s", dir, e.d_name);
		if (SCE_S_ISDIR(e.d_stat.st_mode)) {
			if (depth < 3) scan_music_dir(full, depth + 1);
		} else {
			const char *dot = strrchr(e.d_name, '.');
			int dup = 0;
			for (int i = 0; i < menus[M_TRACKS].count; i++)
				if (!strcasecmp(menus[M_TRACKS].items[i].path, full)) { dup = 1; break; }
			const uint32_t nh = mus_name_hash(e.d_name), nsz = (uint32_t)e.d_stat.st_size;
			for (int i = 0; i < mus_seen_n && !dup; i++)
				if (mus_seen_h[i] == nh && mus_seen_sz[i] == nsz) dup = 1;
			if (dot && !dup && (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".flac") || !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".wav") ||
			                    !strcasecmp(dot, ".aif") || !strcasecmp(dot, ".aiff") || !strcasecmp(dot, ".aifc"))) {
				char title[64], artist[48];
				meta_read(full, title, sizeof(title), artist, sizeof(artist));
				if (!title[0]) snprintf(title, sizeof(title), "%.*s", (int)(dot - e.d_name) > 60 ? 60 : (int)(dot - e.d_name), e.d_name);
				Item *it = add_item(M_TRACKS, KIND_TRACK, title, artist, NULL, tex_music_s);
				if (it) {
					snprintf(it->path, sizeof(it->path), "%s", full);
					if (mus_seen_n < MAX_ITEMS) { mus_seen_h[mus_seen_n] = nh; mus_seen_sz[mus_seen_n] = nsz; mus_seen_n++; }
				}
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

static void scan_music(void)
{
	clear_menu(M_TRACKS);
	mus_seen_n = 0;
	/* The Vita's own Music folder is closed to an app's file calls (ux0:music answers 80010001); the system opens it for apps that ask through AppUtil, as music0: */
	static int music0 = -1;
	if (music0 < 0) {
		lang_system();                                               /* (starts AppUtil) */
		int r = sceAppUtilMusicMount();
		trace("music: sceAppUtilMusicMount -> %08x\n", (unsigned)r);
		music0 = r >= 0;
	}
	if (music0) scan_music_dir("music0:", 0);
	scan_music_dir("ux0:music", 0);
	scan_music_dir("ux0:pspemu/MUSIC", 0);
	if (extra_storage) {
		static const char *suffix[2] = { "music", "pspemu/MUSIC" };
		char devs[4][8];
		int n = extra_devs(devs);
		for (int i = 0; i < n; i++)
			for (int k = 0; k < 2; k++) {
				char path[32];
				snprintf(path, sizeof(path), "%s%s", devs[i], suffix[k]);
				scan_music_dir(path, 0);
			}
	}
	qsort(menus[M_TRACKS].items, menus[M_TRACKS].count, sizeof(Item), item_cmp);
	if (menus[M_TRACKS].count == 0) { int keep = items_tr; items_tr = 3; add_item(M_TRACKS, KIND_INFO, "No songs", "Put MP3, FLAC, OGG, WAV or AIFF files in ux0:music", NULL, tex_music_s); items_tr = keep; }
	if (menus[M_TRACKS].sel >= menus[M_TRACKS].count) menus[M_TRACKS].sel = 0;
	music_index = -1;
}

/* ---- spectrum: 12 bands via Goertzel over the last 1024 played samples ---- */
#define LED_COLS 12
#define LED_ROWS 8
static float led_level[LED_COLS];

static void spectrum_update(int playing)
{
	static const float centers[LED_COLS] = { 60, 110, 190, 330, 520, 800, 1200, 1800, 2800, 4300, 6500, 10000 };
	static float win_tab[VIS_N];
	static int win_ready;
	if (!win_ready) { for (int i = 0; i < VIS_N; i++) win_tab[i] = 0.5f - 0.5f * cosf(6.2831853f * i / (VIS_N - 1)); win_ready = 1; }
	int16_t snap[VIS_N];
	unsigned w = vis_w;
	for (int i = 0; i < VIS_N; i++) snap[i] = vis_buf[(w + i) % VIS_N];
	for (int b = 0; b < LED_COLS; b++) {
		float target = 0.0f;
		if (playing) {
			float coeff = 2.0f * cosf(6.2831853f * centers[b] / 48000.0f);
			float s0, s1 = 0, s2 = 0;
			for (int i = 0; i < VIS_N; i++) {
				s0 = snap[i] * win_tab[i] + coeff * s1 - s2;
				s2 = s1; s1 = s0;
			}
			float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
			float mag = sqrtf(p > 0 ? p : 0) / (VIS_N * 0.25f * 32768.0f);
			float db = 20.0f * log10f(mag + 1e-7f) + b * 2.4f;            /* tilt: highs carry less energy */
			target = clampf((db + 62.0f) / 56.0f * LED_ROWS, 0.0f, (float)LED_ROWS);
		}
		led_level[b] = target > led_level[b] ? lerpf(led_level[b], target, 0.55f) : fmaxf(target, led_level[b] - 0.22f);
	}
}

/* A soft blurred bar, tinted per row, gives each lit LED the PSP's diffuse bloom. */
static vita2d_texture *led_glow_tex(void)
{
	static vita2d_texture *tex;
	static int tried;
	if (tex || tried) return tex;
	tried = 1;
	const int w = 64, h = 48;
	uint8_t *a = calloc((size_t)w * h, 1), *tmp = calloc((size_t)w * h, 1);
	if (a && tmp) {
		for (int y = 18; y < 30; y++)
			for (int x = 22; x < 42; x++) a[y * w + x] = 255;
		for (int pass = 0; pass < 10; pass++) box_blur(a, tmp, w, h);
		tex = vita2d_create_empty_texture(w, h);
		if (tex) {
			uint8_t *o = vita2d_texture_get_datap(tex);
			int stride = vita2d_texture_get_stride(tex);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					int v = a[y * w + x] * 4;
					uint8_t *px = o + y * stride + x * 4;
					px[0] = px[1] = px[2] = 255;
					px[3] = (uint8_t)(v > 255 ? 255 : v);
				}
			vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
		}
	}
	free(a); free(tmp);
	return tex;
}

static void draw_led(float cx, float cy, int row, int lit_amount_255, int page_a)
{
	/* row 0 = bottom. bottom four cyan, next three amber, top red */
	int r = row < 4 ? 60 : 255;
	int g = row < 4 ? 232 : (row < 7 ? 178 : 40);
	int b = row < 4 ? 252 : (row < 7 ? 52 : 52);
	int a = lit_amount_255 * page_a / 255;
	vita2d_draw_rectangle(cx - 10, cy - 6, 20, 12, RGBA8(r / 12, g / 12, b / 12, page_a));  /* unlit pad */
	if (a <= 4) return;
	vita2d_texture *gl = led_glow_tex();
	if (gl) vita2d_draw_texture_tint_scale(gl, cx - 32, cy - 24, 1.0f, 1.0f, RGBA8(r, g, b, a * 85 / 100));
	vita2d_draw_rectangle(cx - 10, cy - 6, 20, 12, RGBA8(r, g, b, a));
	vita2d_draw_rectangle(cx - 8, cy - 4, 16, 8, RGBA8(255, 255, 255, a * 50 / 100));       /* hot core */
}

static void fmt_time(unsigned ms, char *out)
{
	unsigned s = ms / 1000;
	snprintf(out, 12, "%02u:%02u", s / 60, s % 60);
}

/* The list's pictures (number box, digits, codec pill and their shadows) are the PSP's own artwork redrawn at the Vita's native size
 * (assets/psp/music/*.png, made by tools/make_native_music.py from the firmware's music_browser_plugin.rco): the stock bitmaps are 1 / 2 of
 * the screen resolution, stretched they are blurry. They are drawn 1:1, on whole pixels. */
static vita2d_texture *mus_png(const char *name)
{
	static char names[48][28];
	static vita2d_texture *tex[48];
	for (int i = 0; i < 48; i++) {
		if (!names[i][0]) {
			snprintf(names[i], sizeof(names[i]), "%s", name);
			char path[80];
			snprintf(path, sizeof(path), "app0:assets/psp/music/%s.png", name);
			tex[i] = vita2d_load_PNG_file(path);
			if (!tex[i]) trace("music picture missing: %s\n", name);
			return tex[i];
		}
		if (!strcmp(names[i], name)) return tex[i];
	}
	return NULL;
}

/* a picture centred on (cx, cy), one image pixel per screen pixel */
static void mus_draw(const char *name, float cx, float cy, int a)
{
	vita2d_texture *t = mus_png(name);
	if (!t || a <= 0) return;
	float x = floorf(cx - vita2d_texture_get_width(t) / 2.0f + 0.5f), y = floorf(cy - vita2d_texture_get_height(t) / 2.0f + 0.5f);
	vita2d_draw_texture_tint(t, x, y, RGBA8(255, 255, 255, a));
}

/* A shadow picture sits two PSP pixels down and right of the picture it belongs to (the same rule as every other shadow in the app). */
static void mus_draw_shadow(const char *name, float cx, float cy, int a) { mus_draw(name, cx + 4.0f, cy + 4.0f, a); }

/* The focus glow's breathing: alpha swings between GLOW_LO and 1 once every GLOW_PERIOD seconds. NOT read from the firmware (the stock RCO
 * has no animation and paf's item code was not found): an estimate to be matched against a real PSP. */
#define GLOW_LO     0.45f
#define GLOW_PERIOD 2.0f
static float mus_breath(void)
{
	float t = (float)(sceKernelGetProcessTimeWide() % 100000000ULL) / 1.0e6f;
	return GLOW_LO + (1.0f - GLOW_LO) * (0.5f + 0.5f * sinf(t * 6.2831853f / GLOW_PERIOD));
}

/* The codec pill: the PSP's block with the file extension cut out of it, then the speaker arcs. (x is where its white body starts.) */
static void draw_mp3_pill(float x, float y, int a, const char *path)
{
	const char *dot = strrchr(path, '.'), *name = "mp3";
	if (dot && !strcasecmp(dot, ".flac")) name = "flac";
	else if (dot && !strcasecmp(dot, ".ogg")) name = "ogg";
	else if (dot && !strcasecmp(dot, ".wav")) name = "wav";
	else if (dot && !strncasecmp(dot, ".aif", 4)) name = "aiff";
	char pic[24];
	snprintf(pic, sizeof(pic), "music_pill_%s", name);
	float cx = x - 14.0f + 72.0f, cy = y + 11.0f;                  /* the picture starts 7 PSP pixels left of the body */
	mus_draw_shadow("music_pill_shadow", cx, cy, a);
	mus_draw(pic, cx, cy, a);
}

/* One row of the track list (sub-list layout): number box, title/rule/artist, codec pill. */
static void draw_track_row(const Item *it, int number, float y, float xoff, float sel, int a)
{
	float bx = 272.0f + xoff, tx = 343.0f + xoff;
	mus_draw_shadow("music_box_shadow", bx, y, a);
	mus_draw("music_box_back", bx, y, a);
	if (sel > 0.02f) mus_draw("music_box_focus", bx, y, (int)(a * sel * mus_breath()));
	mus_draw("music_box", bx, y, a);
	int n = number % 1000, nd = n >= 100 ? 3 : (n >= 10 ? 2 : 1);
	for (int pass = 0; pass < 2; pass++) {                           /* the digits' shadows first, then the digits */
		float dx = bx - 18.0f * nd / 2.0f + 9.0f;
		int div = nd == 3 ? 100 : (nd == 2 ? 10 : 1);
		for (int k = 0; k < nd; k++, div /= 10, dx += 18.0f) {
			char lab[28];
			snprintf(lab, sizeof(lab), pass ? "music_digit_%d" : "music_digit_%d_shadow", n / div % 10);
			if (pass) mus_draw(lab, dx, y, a);
			else mus_draw_shadow(lab, dx, y, a);
		}
	}

	float maxw = 820.0f - tx;
	ptext_vc_fit(tx - text_bearing(28, item_title(it)), y - 20, WHITE(a), 28, item_title(it), maxw);
	if (item_sub(it)[0]) ptext_vc_fit(tx - text_bearing(22, item_sub(it)), y + 22, WHITE(a), 22, item_sub(it), maxw);
	else if (sel < 0.5f) ptext_vc_fit(tx - text_bearing(22, "-"), y + 22, WHITE(a), 22, "-", maxw);   /* no artist: the PSP shows a dash */
	if (sel > 0.3f) {
		draw_rule(tx - 1, 948, y, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		draw_mp3_pill(828, y + 11, (int)(a * sel), it->path);
	}
}

/* The full music player screen (black, LED spectrum, progress). */
static void draw_player(float alpha, int closing)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, a));
	if (closing) return;           /* leaving: just the backdrop dissolves, no ghost of the LED grid over the XMB */
	/* header bar */
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (v) {
		unsigned int c0 = RGBA8(83, 50, 0, a), c1 = RGBA8(92, 55, 0, a);
		v[0] = (vita2d_color_vertex){ 0, 0, 0.5f, c0 };       v[1] = (vita2d_color_vertex){ SCREEN_W, 0, 0.5f, c0 };
		v[2] = (vita2d_color_vertex){ 0, 44, 0.5f, c1 };      v[3] = (vita2d_color_vertex){ SCREEN_W, 44, 0.5f, c1 };
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	}
	draw_icon_wh(cat_tex[CAT_MUSIC], 30, 22, 34, 34, a);
	ptext_vc(56, 22, WHITE(a), 28, "-");
	char cnt[24];
	int total = menus[M_TRACKS].count;
	snprintf(cnt, sizeof(cnt), "(%d/%d)", music_index + 1, total);
	ptext_right_vc(932, 22, WHITE(a), 28, cnt);

	if (music_index >= 0 && music_index < total) {
		const Item *it = &menus[M_TRACKS].items[music_index];
		/* number badge */
		unsigned int line = WHITE(a);
		vita2d_draw_rectangle(42, 80, 64, 3, line);  vita2d_draw_rectangle(42, 141, 64, 3, line);
		vita2d_draw_rectangle(42, 80, 3, 64, line);  vita2d_draw_rectangle(103, 80, 3, 64, line);
		char num[8];
		snprintf(num, sizeof(num), "%d", music_index + 1);
		ptext_vc(74 - ptext_width(34, num) / 2.0f, 112, WHITE(a), 34, num);
		ptext_vc_fit(150 - text_bearing(30, item_title(it)), 91, WHITE(a), 30, item_title(it), 790.0f);
		draw_rule(150, 948, 106, a);
		if (item_sub(it)[0]) ptext_vc_fit(150 - text_bearing(24, item_sub(it)), 133, WHITE(a), 24, item_sub(it), 640.0f);
		draw_mp3_pill(828, 119, a, it->path);
	}

	/* LED spectrum */
	for (int c = 0; c < LED_COLS; c++)
		for (int r = 0; r < LED_ROWS; r++) {
			float lit = clampf(led_level[c] - r, 0.0f, 1.0f);
			draw_led(73.5f + c * 74.0f, 454.5f - r * 42.0f, r, (int)(255 * lit), a);
		}

	/* time + progress */
	char e[12], tt[12], tail[24];
	fmt_time(music_elapsed_ms(), e);
	fmt_time((unsigned)mus_total_ms, tt);
	snprintf(tail, sizeof(tail), " / %s", tt);
	float tw = ptext_width(34, tail);
	ptext_right_vc(940 - tw, 494, pt_player_col_set ? RGBA8(pt_player_col[0], pt_player_col[1], pt_player_col[2], a) : RGBA8(40, 100, 255, a), 34, e);
	ptext_right_vc(940, 494, WHITE(a), 34, tail);
	float frac = mus_total_ms > 0 ? clampf((float)music_elapsed_ms() / mus_total_ms, 0.0f, 1.0f) : 0.0f;
	vita2d_draw_rectangle(482, 516, 466, 8, RGBA8(222, 222, 222, a));
	vita2d_draw_rectangle(482, 516, 466 * frac < 6 ? 6 : 466 * frac, 8, RGBA8(35, 85, 235, a));

	/* play / pause glyph */
	if (mus_playing) {
		for (int i = 0; i < 28; i++) {
			float half = (i < 14 ? i : 28 - i);
			vita2d_draw_rectangle(28, 506 + i, half * 3.0f + 1, 1, WHITE(a));
		}
	} else {
		vita2d_draw_rectangle(30, 506, 9, 28, WHITE(a));
		vita2d_draw_rectangle(47, 506, 9, 28, WHITE(a));
	}
}

/* Draws one vertical list. xoff slides it sideways, amul fades it, sub_p (0..1) shrinks it into the
 * icon-only "parent" column shown to the left of a Settings-style sub list. */
static float pm_pic0_a;                          /* pspmedia.h: how far a game's text block (PIC0) has faded in; the item text gives way to it */

static void draw_column(int m, float xoff, float amul, float grow, float sub_p)
{
	if (amul <= 0.01f) return;
	if (is_game_folder(m)) { draw_folder_column(m, xoff, amul, grow); return; }
	Menu *mn = &menus[m];
	if (m < CAT_COUNT && pt_text_list()) { if (sub_p < 0.01f) draw_text_rows(m, xoff, amul); return; }   /* Euphoria-style text list */
	if (pt_strip_mode && m < CAT_COUNT && sub_p < 0.01f) {          /* (inside a sub list the parent is the small icon at the left, as in every layout) */
		/* strip themes show only the open item: one big icon centred on the screen, its name below (tile
		 * icons that carry their own caption need no extra text) */
		if (!mn->count) return;
		const Item *it = &mn->items[mn->sel];
		vita2d_texture *tex = it->icon ? it->icon : pt_swap(it->stock);
		if (!tex) return;
		int a = (int)(255 * amul);
		float tw = (float)vita2d_texture_get_width(tex), th = (float)vita2d_texture_get_height(tex);
		int themed = pt_owned(tex) || tex != it->stock;
		float size = themed ? 2.0f * (tw > th ? tw : th) : 112.0f;
		float w, h;
		icon_dims(tex, size, &w, &h);
		if (!themed) { w = h = size; }
		const float bottom = 352.0f, cx = 480.0f + xoff;
		draw_icon_wh(tex, cx, bottom - h / 2.0f, w, h, a);
		if (!(themed && th >= 70.0f)) {
			ptext_vc(cx - ptext_width(PT_TITLE(28), item_title(it)) / 2.0f, bottom + 28.0f, WHITE(a), PT_TITLE(28), item_title(it));
			if (item_sub(it)[0]) ptext_vc(cx - ptext_width(PT_SUB(22), item_sub(it)) / 2.0f, bottom + 58.0f, WHITE(a * 8 / 10), PT_SUB(22), item_sub(it));
		}
		return;
	}
	float sp = sub_p;                                           /* sub_t is already the bar's 200 ms mode-4 step (the column is a child of the category item and moves with it) */
	/* the Game list (Saved Data / Memory Stick) uses a slightly tighter text column than Settings */
	const float base_icon_x = m == CAT_SETTINGS ? 207.0f : 218.0f;      /* Settings icons carry a wrench badge that hangs left */
	const float text_x = m == CAT_SETTINGS ? 300.0f : 283.0f;
	const float icon_x = lerpf(base_icon_x, 100.0f, sp);
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -1.6f || d > 3.0f) continue;

		/* the column is an XList of style 1: pitch and gap above in PSP pixels (65 / 60, which a theme can patch) */
		float spacing = lerpf(2.0f * xstyles[XS_COLUMN].pitch, 115.0f, sp);
		float y = ITEM_Y + d * spacing;
		if (d < 0) y -= 2.0f * xstyles[XS_COLUMN].gap_above * (1.0f - sp) * clampf(-d, 0.0f, 1.0f);  /* hop over the category row */
		float sel = 1.0f - clampf(fabsf(d), 0.0f, 1.0f);              /* position-based: icon size only */
		const Item *it = &mn->items[j];
		float emph = it->glow;                                         /* selection-based: brightness, rule, glow */
		float fade = d < 0 ? clampf(1.0f + d * 0.6f, 0.0f, 1.0f) * (1.0f - sp)
		                    : clampf(1.0f - d * 0.1f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.42f, 1.0f, emph));
		if (a <= 4) continue;

		float ix = icon_x + xoff;
		if (m == CAT_SETTINGS && sp < 0.5f && (pt_owned(pt_swap(it->stock)) || it->icon)) ix += (218.0f - 207.0f) * (1.0f - sp);   /* a theme's own icons have no hanging wrench badge: centre them under the category icon */
		/* the UMD (the game started last): the disc until the cursor is on it, then the game's landscape picture in a tile like the game list's,
		 * the film (ICON1.PMF) once that plays; the title moves right to stay clear of the tile */
		const int disc = (it->tr & 4) != 0 && sp < 0.01f;
		vita2d_texture *gpic = disc ? pm_icon_tex_for(it) : NULL;
		if (disc && !gpic) gpic = it->icon;
		const float dmix = disc && gpic ? clampf(emph * 1.25f, 0.0f, 1.0f) : 0.0f;
		if (disc) {
			if (it->stock && dmix < 0.995f) {
				const int own = (it->tr & 8) && it->icon;                         /* a placed app: its own icon */
				vita2d_texture *tex = own ? it->icon : pt_swap(it->stock);
				float isz = own ? 76.0f + 36.0f * grow * sel : lerpf(112.0f, 120.0f, sel) * pt_menu_scale + 36.0f * grow * sel;
				int da = (int)(a * (1.0f - dmix));
				if (!own && it->glow > 0.02f) draw_glow(tex, ix, y, isz, it->glow * 0.95f * (da / 255.0f));
				if (own) {                                                           /* its true proportions: landscape art is not squeezed into a square */
					float ow, oh;
					fit_box(tex, isz * 1.45f, isz, &ow, &oh);
					draw_icon_wh(tex, ix, y, ow, oh, da);
				} else draw_icon(tex, ix, y, isz, da);
			}
			if (gpic && dmix > 0.005f) {
				float w = 150.0f + 30.0f * grow * sel;                          /* the game list's own tile size */
				float h = w * vita2d_texture_get_height(gpic) / vita2d_texture_get_width(gpic);
				const float bw = w, bh = h;
				int ga = (int)(a * dmix);
				if (gpic == it->icon && !it->icon_rect) {                      /* not landscape art: framed like the game list does */
					vita2d_draw_rectangle(ix - bw / 2 + 2, y - bh / 2 + 3, bw, bh, RGBA8(0, 0, 0, ga * 25 / 100));
					vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, bh, RGBA8(18, 28, 38, ga * 80 / 100));
					vita2d_draw_rectangle(ix - bw / 2, y - bh / 2, bw, 2, WHITE(ga * 35 / 100));
				}
				draw_icon_wh(gpic, ix, y, w, h, ga);
			}
		} else if (it->icon && it->icon_rect) {
			/* LiveArea gate image: a landscape rectangle like the PSP's ICON0 */
			float w = 150.0f + 30.0f * grow * sel;
			float h = w * vita2d_texture_get_height(it->icon) / vita2d_texture_get_width(it->icon);
			draw_icon_wh(it->icon, ix, y, w, h, a);
		} else {
			float msc = it->icon ? 1.0f : pt_menu_scale;                 /* the theme's size for the menu's icons */
			float isz = (it->icon ? 72.0f : lerpf(112.0f, 120.0f, sel) * msc) + 36.0f * grow * sel;
			isz = lerpf(isz, it->icon ? 72.0f : lerpf(84.0f, 92.0f, sel) * msc, sp);
			vita2d_texture *tex = item_pic(it, m);
			if (tex && !it->icon && it->glow > 0.02f) draw_glow(tex, ix, y, isz, it->glow * 0.95f * (a / 255.0f));
			draw_icon(tex, ix, y, isz, a);
		}

		/* title above / subtitle below a thin rule; ink edges flush at the text column */
		int ta = (int)(a * (1.0f - sp) * (1.0f - clampf(pm_pic0_a * 1.6f, 0.0f, 1.0f)));       /* with the text block up, the PSP shows no item titles */
		if (ta <= 4) continue;
		float tx = text_x + xoff;
		if (dmix > 0.0f) tx = lerpf(tx, ix + 90.0f + 22.0f, dmix);               /* right of the tile */
		float maxw = SCREEN_W - 24.0f - (tx - xoff);
		if (item_sub(it)[0]) {
			ptext_vc_row(tx - text_bearing(PT_TITLE(28), item_title(it)), y - 20, WHITE(ta), PT_TITLE(28), item_title(it), maxw, emph > 0.5f);
			ptext_vc_row(tx - text_bearing(PT_SUB(22), item_sub(it)), y + 22, WHITE(ta), PT_SUB(22), item_sub(it), maxw, emph > 0.5f);
			if (emph > 0.3f) draw_rule(tx - 1, 950, y, (int)(ta * clampf(emph * 1.4f - 0.2f, 0.0f, 1.0f)));
		} else {
			ptext_vc_row(tx - text_bearing(PT_TITLE(28), item_title(it)), y, WHITE(ta), PT_TITLE(28), item_title(it), maxw, emph > 0.5f);
		}
	}
}

/* Glyph test page for tools/check_font.py: 32 cells per row (30 px wide), 3 rows per size. */
static const unsigned int test_sizes[4] = { 20, 22, 24, 28 };

static void draw_text_grid(void)
{
	for (int si = 0; si < 4; si++)
		for (int c = 0; c < 94; c++) {
			int row = si * 3 + c / 32, col = c % 32;
			char buf[2] = { (char)(33 + c), 0 };
			ptext(6 + col * 30, 40 + row * 44, WHITE(255), test_sizes[si], buf);
		}
}
