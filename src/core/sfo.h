#pragma once

/* ------------------------------------------------------------------ */
/* param.sfo parsing                                                   */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t magic, version, key_off, data_off, count; } SfoHeader;
typedef struct { uint16_t key_off, fmt; uint32_t len, max_len, data_off; } SfoEntry;

static int sfo_get_string(const char *path, const char *key, char *out, size_t outlen)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;

	int size = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	if (size < (int)sizeof(SfoHeader) || size > 64 * 1024) { sceIoClose(fd); return 0; }

	uint8_t *buf = malloc(size);
	if (!buf) { sceIoClose(fd); return 0; }
	int rd = sceIoRead(fd, buf, size);
	sceIoClose(fd);

	int found = 0;
	if (rd == size) {
		const SfoHeader *h = (const SfoHeader *)buf;
		if (h->magic == 0x46535000 && h->count < 128 &&
		    sizeof(SfoHeader) + h->count * sizeof(SfoEntry) <= (size_t)size) {
			const SfoEntry *e = (const SfoEntry *)(buf + sizeof(SfoHeader));
			for (uint32_t i = 0; i < h->count; i++) {
				if (h->key_off + e[i].key_off >= (uint32_t)size) continue;
				const char *k = (const char *)buf + h->key_off + e[i].key_off;
				if (strncmp(k, key, 32) != 0) continue;
				if (h->data_off + e[i].data_off + e[i].len > (uint32_t)size) break;
				size_t n = e[i].len < outlen - 1 ? e[i].len : outlen - 1;
				memcpy(out, buf + h->data_off + e[i].data_off, n);
				out[n] = 0;
				found = 1;
				break;
			}
		}
	}
	free(buf);
	return found;
}
