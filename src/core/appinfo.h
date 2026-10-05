#pragma once

/* ---- helpers ---- */

static int is_png_file(const char *path)
{
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	uint8_t b[8];
	int n = sceIoRead(fd, b, 8);
	sceIoClose(fd);
	return n == 8 && memcmp(b, sig, 8) == 0;
}

static int copy_file(const char *src, const char *dst)
{
	SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (in < 0) return 0;
	SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (out < 0) { sceIoClose(in); return 0; }
	static uint8_t buf[64 * 1024];            /* only the decrypt thread copies */
	int ok = 1, n;
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0)
		if (sceIoWrite(out, buf, n) != n) { ok = 0; break; }
	if (n < 0) ok = 0;
	sceIoClose(in);
	sceIoClose(out);
	return ok;
}

/* Finds the text of the first <tag ...>TEXT</tag> after `section` in `xml`. */
static int xml_first(const char *xml, const char *section, const char *tag, char *out, size_t cap)
{
	const char *p = section ? strstr(xml, section) : xml;
	if (!p) return 0;
	char open[40];
	snprintf(open, sizeof(open), "<%s", tag);
	p = strstr(p, open);
	if (!p) return 0;
	p = strchr(p, '>');
	if (!p) return 0;
	p++;
	const char *e = strchr(p, '<');
	if (!e) return 0;
	while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
	size_t n = (size_t)(e - p);
	while (n && (p[n - 1] == ' ' || p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == '\t')) n--;
	if (n == 0 || n >= cap) return 0;
	memcpy(out, p, n);
	out[n] = 0;
	return 1;
}

static int has_ci(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return 1;
	return 0;
}

/* Looks in `dir` (trailing slash) for a LiveArea gate image and background. Fills only the
 * outputs that are still empty, and only with files that really are PNGs (the retail ones
 * under ux0:app are encrypted). template.xml names them; a name scan is the fallback. */
static void livearea_scan(const char *dir, char *gate, char *bg)
{
	char full[112], name[64];
	if (gate[0] && bg[0]) return;

	char *xml = malloc(16384);
	if (xml) {
		char path[160];
		snprintf(path, sizeof(path), "%stemplate.xml", dir);
		SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (fd >= 0) {
			int n = sceIoRead(fd, xml, 16383);
			sceIoClose(fd);
			if (n > 0) {
				xml[n] = 0;
				if (!bg[0] && xml_first(xml, "<livearea-background", "image", name, sizeof(name))) {
					snprintf(full, sizeof(full), "%s%s", dir, name);
					if (is_png_file(full)) snprintf(bg, 112, "%s", full);
				}
				if (!gate[0] && xml_first(xml, "<gate", "startup-image", name, sizeof(name))) {
					snprintf(full, sizeof(full), "%s%s", dir, name);
					if (is_png_file(full)) snprintf(gate, 112, "%s", full);
				}
			}
		}
		free(xml);
	}

	if (!gate[0] || !bg[0]) {
		SceUID d = sceIoDopen(dir);
		if (d >= 0) {
			SceIoDirent e;
			memset(&e, 0, sizeof(e));
			while (sceIoDread(d, &e) > 0) {
				if (has_ci(e.d_name, ".png")) {
					int is_gate = has_ci(e.d_name, "gate") || has_ci(e.d_name, "startup");
					int is_bg = !is_gate && (has_ci(e.d_name, "bg") || has_ci(e.d_name, "background"));
					snprintf(full, sizeof(full), "%s%s", dir, e.d_name);
					if (is_gate && !gate[0] && is_png_file(full)) snprintf(gate, 112, "%s", full);
					else if (is_bg && !bg[0] && is_png_file(full)) snprintf(bg, 112, "%s", full);
				}
				memset(&e, 0, sizeof(e));
			}
			sceIoDclose(d);
		}
	}
}
