#pragma once

/* ------------------------------------------------------------------ */
/* PSP games: the files inside an ISO / CSO image or an EBOOT.PBP       */
/* ------------------------------------------------------------------ */

/* Adrenaline's games are an ISO or CSO image (ux0:pspemu/ISO) or an EBOOT.PBP (ux0:pspemu/PSP/GAME/<name>); a bubble that Adrenaline's tools
 * make only holds the path of one in its data/boot.bin. The game's title, icon, background, film (ICON1.PMF) and sound (SND0.AT3) are inside it. */

#define PM_MAX_FILE (8 * 1024 * 1024)

/* ---- the game's file: PBP, ISO or CSO ---- */

typedef struct {
	SceUID fd;
	int kind;                                     /* 0 EBOOT.PBP, 1 ISO, 2 CSO */
	uint64_t size;                                /* the image's size (CSO: the uncompressed size) */
	uint32_t block, nblocks, *index;              /* CSO */
	int align;
	uint8_t *blk; int64_t blk_n;                  /* CSO: the block in cache */
	uint8_t *raw;
} PmSrc;

static void pm_src_close(PmSrc *s)
{
	if (s->fd >= 0) sceIoClose(s->fd);
	free(s->index); free(s->blk); free(s->raw);
	memset(s, 0, sizeof(*s));
	s->fd = -1;
}

static int pm_io_read(PmSrc *s, uint64_t off, void *dst, uint32_t n)
{
	if (sceIoLseek(s->fd, (SceOff)off, SCE_SEEK_SET) < 0) return 0;
	uint8_t *p = dst;
	while (n) {
		int r = sceIoRead(s->fd, p, n > 0x40000 ? 0x40000 : n);
		if (r <= 0) return 0;
		p += r; n -= (uint32_t)r;
	}
	return 1;
}

static int pm_src_open(PmSrc *s, const char *path)
{
	memset(s, 0, sizeof(*s));
	s->fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (s->fd < 0) return 0;
	uint8_t h[24];
	SceOff sz = sceIoLseek(s->fd, 0, SCE_SEEK_END);
	if (sz < 32 || !pm_io_read(s, 0, h, sizeof(h))) { pm_src_close(s); return 0; }
	s->size = (uint64_t)sz;
	if (!memcmp(h, "\0PBP", 4)) { s->kind = 0; return 1; }
	if (!memcmp(h, "CISO", 4)) {
		uint32_t hs = pt_rd32(h + 4);
		uint64_t total = (uint64_t)pt_rd32(h + 8) | (uint64_t)pt_rd32(h + 12) << 32;
		s->block = pt_rd32(h + 16);
		s->align = h[21];
		if (s->block < 512 || s->block > 0x10000 || !total || hs < 24) { pm_src_close(s); return 0; }
		s->nblocks = (uint32_t)((total + s->block - 1) / s->block);
		s->index = malloc((size_t)(s->nblocks + 1) * 4);
		s->blk = malloc(s->block);
		s->raw = malloc(s->block * 2 + 64);
		if (!s->index || !s->blk || !s->raw || !pm_io_read(s, hs, s->index, (s->nblocks + 1) * 4)) { pm_src_close(s); return 0; }
		s->kind = 2; s->size = total; s->blk_n = -1;
		return 1;
	}
	s->kind = 1;
	return 1;
}

/* n bytes at off of the image (uncompressed) */
static int pm_src_read(PmSrc *s, uint64_t off, void *dst, uint32_t n)
{
	if (s->kind != 2) return off + n <= s->size && pm_io_read(s, off, dst, n);
	uint8_t *o = dst;
	while (n) {
		uint32_t b = (uint32_t)(off / s->block), in = (uint32_t)(off % s->block);
		if (b >= s->nblocks) return 0;
		if (s->blk_n != (int64_t)b) {
			uint32_t e0 = s->index[b], e1 = s->index[b + 1];
			uint64_t p0 = (uint64_t)(e0 & 0x7FFFFFFFu) << s->align, p1 = (uint64_t)(e1 & 0x7FFFFFFFu) << s->align;
			uint32_t len = (uint32_t)(p1 - p0);
			if (p1 < p0 || len > s->block * 2 + 64) return 0;
			if (e0 & 0x80000000u) {                                   /* stored plain */
				if (len < s->block || !pm_io_read(s, p0, s->blk, s->block)) return 0;
			} else {
				if (!pm_io_read(s, p0, s->raw, len)) return 0;
				z_stream z;
				memset(&z, 0, sizeof(z));
				if (inflateInit2(&z, -15) != Z_OK) return 0;
				z.next_in = s->raw; z.avail_in = len; z.next_out = s->blk; z.avail_out = s->block;
				int r = inflate(&z, Z_FINISH);
				inflateEnd(&z);
				if (r != Z_STREAM_END && r != Z_OK && r != Z_BUF_ERROR) return 0;
			}
			s->blk_n = b;
		}
		uint32_t c = s->block - in;
		if (c > n) c = n;
		memcpy(o, s->blk + in, c);
		o += c; off += c; n -= c;
	}
	return 1;
}

/* one directory level of an ISO 9660 image: the extent of the entry called name */
static int pm_iso_find(PmSrc *s, uint32_t lba, uint32_t size, const char *name, uint32_t *out_lba, uint32_t *out_size, int *is_dir)
{
	if (size > 0x100000) return 0;
	uint8_t *d = malloc(size);
	if (!d) return 0;
	int found = 0;
	if (pm_src_read(s, (uint64_t)lba * 2048, d, size)) {
		for (uint32_t p = 0; p + 34 <= size; ) {
			uint32_t len = d[p];
			if (!len) { p = (p / 2048 + 1) * 2048; continue; }              /* records do not cross a sector */
			uint32_t nl = d[p + 32];
			if (p + 33 + nl <= size) {
				char nm[64];
				uint32_t k = nl < 63 ? nl : 63;
				memcpy(nm, d + p + 33, k); nm[k] = 0;
				char *semi = strchr(nm, ';');
				if (semi) *semi = 0;
				if (!strcasecmp(nm, name)) {
					*out_lba = pt_rd32(d + p + 2); *out_size = pt_rd32(d + p + 10); *is_dir = d[p + 25] & 2;
					found = 1; break;
				}
			}
			p += len;
		}
	}
	free(d);
	return found;
}

/* the files of a game: the slots of an EBOOT.PBP's header, and the names they have in an ISO's PSP_GAME folder */
enum { PM_F_SFO, PM_F_ICON0, PM_F_ICON1, PM_F_PIC0, PM_F_PIC1, PM_F_SND0 };
static const char *pm_f_name[6] = { "PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC0.PNG", "PIC1.PNG", "SND0.AT3" };

/* one file of the game, malloc'ed (64 spare zero bytes after it), or NULL */
static uint8_t *pm_game_file(PmSrc *s, int what, size_t *len)
{
	uint64_t off = 0; uint32_t n = 0;
	if (s->kind == 0) {                                          /* EBOOT.PBP: PARAM.SFO ICON0 ICON1 PIC0 PIC1 SND0 DATA.PSP DATA.PSAR */
		uint8_t h[40];
		if (!pm_src_read(s, 0, h, 40)) return NULL;
		int idx = what;
		uint32_t a = pt_rd32(h + 8 + idx * 4), b = pt_rd32(h + 8 + (idx + 1) * 4);
		if (b <= a || b > s->size) return NULL;
		off = a; n = b - a;
	} else {
		uint8_t pvd[2048];
		uint32_t lba, size, l2, s2;
		int dir;
		if (!pm_src_read(s, 16 * 2048, pvd, 2048) || memcmp(pvd + 1, "CD001", 5)) return NULL;
		lba = pt_rd32(pvd + 158); size = pt_rd32(pvd + 166);
		if (!pm_iso_find(s, lba, size, "PSP_GAME", &l2, &s2, &dir) || !dir) return NULL;
		if (!pm_iso_find(s, l2, s2, pm_f_name[what], &lba, &size, &dir) || dir) return NULL;
		off = (uint64_t)lba * 2048; n = size;
	}
	if (n < 16 || n > PM_MAX_FILE) return NULL;
	uint8_t *buf = malloc(n + 64);
	if (!buf) return NULL;
	if (!pm_src_read(s, off, buf, n)) { free(buf); return NULL; }
	memset(buf + n, 0, 64);
	*len = n;
	return buf;
}

/* the game file a bubble starts, from its data/boot.bin (made by Adrenaline's bubble tools: "ABB", then the path) */
static int pm_bubble_game(const char *id, char *out, size_t on)
{
	char p[96];
	snprintf(p, sizeof(p), "ux0:app/%.20s/data/boot.bin", id);
	SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	uint8_t b[512];
	int n = sceIoRead(fd, b, sizeof(b) - 1);
	sceIoClose(fd);
	if (n < 80 || memcmp(b, "ABB", 3)) return 0;
	b[n] = 0;
	for (int i = 3; i + 4 < n; i++) {
		if ((!memcmp(b + i, "ux0:", 4) || !memcmp(b + i, "ms0:", 4)) && (i == 0 || b[i - 1] == 0)) {
			const char *g = (const char *)b + i;
			if (!memcmp(g, "ms0:", 4)) snprintf(out, on, "ux0:pspemu%s", g + 4);
			else snprintf(out, on, "%s", g);
			return 1;
		}
	}
	return 0;
}


/* a PARAM.SFO in memory: one string value */
static int pm_sfo_string(const uint8_t *buf, size_t size, const char *key, char *out, size_t outlen)
{
	if (size < sizeof(SfoHeader)) return 0;
	const SfoHeader *h = (const SfoHeader *)buf;
	if (h->magic != 0x46535000 || h->count > 128 || sizeof(SfoHeader) + h->count * sizeof(SfoEntry) > size) return 0;
	const SfoEntry *e = (const SfoEntry *)(buf + sizeof(SfoHeader));
	for (uint32_t i = 0; i < h->count; i++) {
		if (h->key_off + e[i].key_off >= size) continue;
		if (strncmp((const char *)buf + h->key_off + e[i].key_off, key, 32) != 0) continue;
		if (h->data_off + e[i].data_off + e[i].len > size) return 0;
		size_t n = e[i].len < outlen - 1 ? e[i].len : outlen - 1;
		memcpy(out, buf + h->data_off + e[i].data_off, n);
		out[n] = 0;
		return 1;
	}
	return 0;
}
