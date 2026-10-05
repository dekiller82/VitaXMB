#pragma once

/*
 * updater.h - updates from the project's GitHub releases.
 *
 * check:    asks https://api.github.com/repos/<repo>/releases/latest for the newest release (tag, notes, and the .vpk asset).
 * download: fetches the .vpk to ux0:data/VitaXMB/update.vpk.
 * install:  a .vpk is a zip. Every file is unpacked and its CRC checked in ux0:data/pkg first; when all are good the system's package
 *           installer is asked to install them (VitaShell's method, see below). An app cannot write into its own folder on this Vita.
 *           If the installer cannot be used the .vpk stays in ux0:data/VitaXMB for VitaShell. Then the app restarts itself.
 * All of it runs on a worker thread so the XMB keeps drawing; the page and the startup check read the state below.
 */

#include "cacerts.h"
#include "headbin.h"

#define UPD_REPO "dekiller82/VitaXMB"
#ifndef APP_VERSION
#define APP_VERSION "1.2.0"
#endif
#define UPD_DIR    "ux0:data/pkg"                    /* where the system installer expects an unpacked package */
#define UPD_VPK    CONFIG_DIR "/update.vpk"
#define UPD_HELPER_VER "01.03"                  /* the updater app's version in CMakeLists.txt: an older one on the Vita is replaced */

enum { UPD_IDLE, UPD_CHECKING, UPD_UPTODATE, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_INSTALLING, UPD_DONE, UPD_FAILED, UPD_SAVED };
enum { UPD_CMD_NONE, UPD_CMD_CHECK, UPD_CMD_INSTALL };

static volatile int upd_state = UPD_IDLE, upd_cmd, upd_cancel, upd_silent;
static volatile uint32_t upd_got, upd_total;
static char upd_tag[40], upd_url[700], upd_notes[3000], upd_err[140];
static volatile int upd_handover;                /* set by the update thread: the main thread starts the updater app */
static volatile int upd_scroll;                  /* first visible line of the release notes */
static volatile int upd_found;                    /* a newer release was found (also by the silent startup check) */

/* "v1.2.0" / "1.2" -> 10200 */
static int upd_ver(const char *s)
{
	int p[3] = { 0, 0, 0 }, k = 0;
	while (*s && (*s < '0' || *s > '9')) s++;
	while (*s && k < 3) {
		if (*s >= '0' && *s <= '9') p[k] = p[k] * 10 + (*s - '0');
		else if (*s == '.') k++;
		else break;
		s++;
	}
	return p[0] * 10000 + p[1] * 100 + p[2];
}

/* ---- network: HTTPS over mbedTLS (the Vita's own SSL library cannot talk to GitHub: its handshake fails) ---- */

static int upd_net_up;
static mbedtls_x509_crt upd_ca;

static int upd_rng(void *ctx, unsigned char *out, size_t len)
{
	(void)ctx;
	return sceKernelGetRandomNumber(out, (SceSize)len) < 0 ? MBEDTLS_ERR_ENTROPY_SOURCE_FAILED : 0;
}

static int upd_net_init(void)
{
	if (upd_net_up) return upd_net_up > 0;
	upd_net_up = -1;
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	SceNetInitParam np = { malloc(256 * 1024), 256 * 1024, 0 };
	if (np.memory) sceNetInit(&np);                                       /* (already up if the system information page ran) */
	sceNetCtlInit();
	mbedtls_x509_crt_init(&upd_ca);
	int r = mbedtls_x509_crt_parse(&upd_ca, (const unsigned char *)upd_roots_pem, sizeof(upd_roots_pem));
	trace("update: root certificates: %d failed to parse\n", r);
	if (r < 0) return 0;
	upd_net_up = 1;
	return 1;
}

static int upd_online(void)
{
	int state = 0;
	if (sceNetCtlInetGetState(&state) < 0) return 0;
	return state == SCE_NETCTL_STATE_CONNECTED;
}

typedef struct { uint8_t *buf; size_t cap, len; SceUID fd; } UpdSink;

static int upd_sink_write(UpdSink *s, const uint8_t *p, size_t n)
{
	if (s->fd >= 0) return sceIoWrite(s->fd, p, n) == (int)n ? 0 : -1;
	if (s->len + n + 1 > s->cap) {
		size_t nc = s->cap ? s->cap * 2 : 65536;
		while (nc < s->len + n + 1) nc *= 2;
		if (nc > 8 * 1024 * 1024) return -1;
		uint8_t *nb = realloc(s->buf, nc);
		if (!nb) return -1;
		s->buf = nb; s->cap = nc;
	}
	memcpy(s->buf + s->len, p, n);
	s->len += n;
	s->buf[s->len] = 0;
	return 0;
}

typedef struct {
	mbedtls_net_context net;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_ctr_drbg_context drbg;
	unsigned char buf[8192];                      /* what was read from the connection and not used yet */
	size_t pos, end;
} UpdConn;

static void upd_conn_close(UpdConn *c)
{
	mbedtls_ssl_close_notify(&c->ssl);
	mbedtls_net_free(&c->net);
	mbedtls_ssl_free(&c->ssl);
	mbedtls_ssl_config_free(&c->conf);
	mbedtls_ctr_drbg_free(&c->drbg);
}

static int upd_conn_open(UpdConn *c, const char *host, const char *port)
{
	memset(c, 0, sizeof(*c));
	mbedtls_net_init(&c->net);
	mbedtls_ssl_init(&c->ssl);
	mbedtls_ssl_config_init(&c->conf);
	mbedtls_ctr_drbg_init(&c->drbg);
	int r = mbedtls_ctr_drbg_seed(&c->drbg, upd_rng, NULL, (const unsigned char *)"vitaxmb", 7);
	if (r == 0) r = mbedtls_net_connect(&c->net, host, port, MBEDTLS_NET_PROTO_TCP);
	if (r != 0) { trace("update: connect %s failed (-0x%04x)\n", host, -r); upd_conn_close(c); return r; }
	r = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (r == 0) {
		mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
		mbedtls_ssl_conf_ca_chain(&c->conf, &upd_ca, NULL);
		mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);
		mbedtls_ssl_conf_read_timeout(&c->conf, 30000);
		r = mbedtls_ssl_setup(&c->ssl, &c->conf);
	}
	if (r == 0) r = mbedtls_ssl_set_hostname(&c->ssl, host);
	if (r != 0) { trace("update: tls setup failed (-0x%04x)\n", -r); upd_conn_close(c); return r; }
	mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
	while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
		if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
			char why[100];
			mbedtls_strerror(r, why, sizeof(why));
			uint32_t fl = mbedtls_ssl_get_verify_result(&c->ssl);
			trace("update: handshake with %s failed: %s (-0x%04x, certificate flags %08x)\n", host, why, -r, (unsigned)fl);
			upd_conn_close(c);
			return r;
		}
	}
	return 0;
}

/* one byte-oriented read: from what is buffered, else from the connection; -1 at the end or on an error */
static int upd_conn_getc(UpdConn *c)
{
	if (c->pos >= c->end) {
		for (;;) {
			int n = mbedtls_ssl_read(&c->ssl, c->buf, sizeof(c->buf));
			if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
			if (n <= 0) return -1;
			c->pos = 0; c->end = (size_t)n;
			break;
		}
	}
	return c->buf[c->pos++];
}

/* a block of up to n bytes: what is buffered, else one read; 0 at the end, negative on an error */
static int upd_conn_read(UpdConn *c, uint8_t *out, size_t n)
{
	if (c->pos < c->end) {
		size_t k = c->end - c->pos < n ? c->end - c->pos : n;
		memcpy(out, c->buf + c->pos, k);
		c->pos += k;
		return (int)k;
	}
	for (;;) {
		int r = mbedtls_ssl_read(&c->ssl, out, n);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
		if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
		return r;
	}
}

static int upd_readline(UpdConn *c, char *line, size_t cap)
{
	size_t n = 0;
	for (;;) {
		int ch = upd_conn_getc(c);
		if (ch < 0) return -1;
		if (ch == '\n') break;
		if (ch != '\r' && n + 1 < cap) line[n++] = (char)ch;
	}
	line[n] = 0;
	return (int)n;
}

/* https://host[:port]/path -> parts; 0 on a url this cannot use */
static int upd_split_url(const char *url, char *host, size_t hc, char *port, size_t pc, char *path, size_t pathc)
{
	if (strncmp(url, "https://", 8) != 0) return 0;
	const char *h = url + 8, *slash = strchr(h, '/');
	size_t hl = slash ? (size_t)(slash - h) : strlen(h);
	snprintf(path, pathc, "%s", slash ? slash : "/");
	const char *colon = memchr(h, ':', hl);
	snprintf(port, pc, "%s", "443");
	if (colon) { size_t pl = hl - (size_t)(colon - h) - 1; if (pl >= pc) pl = pc - 1; memcpy(port, colon + 1, pl); port[pl] = 0; hl = (size_t)(colon - h); }
	if (hl >= hc) return 0;
	memcpy(host, h, hl); host[hl] = 0;
	return 1;
}

/* GET url into memory (sink->fd < 0) or a file (fd >= 0). Follows redirects. Returns the HTTP status, or a negative error. */
static int upd_http_get(const char *url0, UpdSink *sink, int json)
{
	char url[900];
	snprintf(url, sizeof(url), "%s", url0);
	for (int hop = 0; hop < 6; hop++) {
		char host[120], port[8], path[800];
		if (!upd_split_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path))) return -10;
		static UpdConn conn;
		int r = upd_conn_open(&conn, host, port);
		if (r != 0) return r < 0 ? r : -11;
		char req[1100];
		int rl = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: VitaXMB-updater\r\nAccept: %s\r\nConnection: close\r\n\r\n",
		                  path, host, json ? "application/vnd.github+json" : "application/octet-stream");
		int w = 0;
		while (w < rl) {
			int k = mbedtls_ssl_write(&conn.ssl, (const unsigned char *)req + w, (size_t)(rl - w));
			if (k == MBEDTLS_ERR_SSL_WANT_READ || k == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
			if (k < 0) { upd_conn_close(&conn); return k; }
			w += k;
		}
		char line[900], loc[900] = "";
		int status = -12;
		long clen = -1;
		int chunked = 0;
		if (upd_readline(&conn, line, sizeof(line)) > 0 && strncmp(line, "HTTP/", 5) == 0) status = atoi(line + 9);
		while (upd_readline(&conn, line, sizeof(line)) > 0) {                          /* headers up to the empty line */
			if (strncasecmp(line, "Location:", 9) == 0) { const char *v = line + 9; while (*v == ' ') v++; snprintf(loc, sizeof(loc), "%s", v); }
			else if (strncasecmp(line, "Content-Length:", 15) == 0) clen = atol(line + 15);
			else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && has_ci(line + 18, "chunked")) chunked = 1;
		}
		if (status >= 300 && status < 400 && loc[0]) {
			upd_conn_close(&conn);
			snprintf(url, sizeof(url), "%s", loc);
			continue;
		}
		if (status == 200) {
			upd_total = clen > 0 ? (uint32_t)clen : 0;
			upd_got = 0;
			static uint8_t chunk[16384];
			long left = chunked ? 0 : clen;               /* bytes left in the current block (or in the whole body) */
			int done = 0;
			while (!done) {
				if (upd_cancel) { status = -2; break; }
				if (chunked && left == 0) {                 /* the next chunk's size line */
					if (upd_readline(&conn, line, sizeof(line)) < 0) { status = -13; break; }
					if (line[0] == 0 && upd_readline(&conn, line, sizeof(line)) < 0) { status = -13; break; }
					left = strtol(line, NULL, 16);
					if (left == 0) break;                      /* the last chunk */
				}
				size_t want = sizeof(chunk);
				if ((chunked || clen >= 0) && (long)want > left) want = (size_t)left;
				if (!chunked && clen >= 0 && left == 0) break;
				int n = upd_conn_read(&conn, chunk, want);
				if (n == 0) break;
				if (n < 0) { status = -14; break; }
				if (upd_sink_write(sink, chunk, (size_t)n) < 0) { status = -3; break; }
				upd_got += (uint32_t)n;
				if (chunked || clen >= 0) left -= n;
			}
			if (status == 200 && clen > 0 && !chunked && (long)upd_got < clen) status = -15;               /* cut short */
		}
		upd_conn_close(&conn);
		return status;
	}
	return -4;
}

/* ---- the release ---- */

/* the string value after "key": in a JSON text, unescaped; NULL if absent */
static int upd_json_str(const char *json, const char *key, char *out, size_t cap)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n') p++;
	if (*p != '"') return 0;
	p++;
	size_t o = 0;
	while (*p && *p != '"' && o + 1 < cap) {
		if (*p == '\\' && p[1]) {
			p++;
			char c = *p;
			if (c == 'n') out[o++] = '\n';
			else if (c == 'r') { /* skip */ }
			else if (c == 't') out[o++] = ' ';
			else if (c == 'u' && p[1] && p[2] && p[3] && p[4]) { out[o++] = '?'; p += 4; }
			else out[o++] = c;
			p++;
			continue;
		}
		out[o++] = *p++;
	}
	out[o] = 0;
	return 1;
}

/* the download url of the .vpk among the release's assets */
static int upd_find_vpk(const char *json, char *out, size_t cap)
{
	const char *p = json;
	while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
		char url[700];
		if (upd_json_str(p, "browser_download_url", url, sizeof(url))) {
			size_t n = strlen(url);
			if (n > 4 && strcasecmp(url + n - 4, ".vpk") == 0) { snprintf(out, cap, "%s", url); return 1; }
		}
		p += 22;
	}
	return 0;
}

static void upd_fail(const char *msg, int code)
{
	snprintf(upd_err, sizeof(upd_err), code ? "%s (%08X)" : "%s", msg, (unsigned)code);
	trace("update: %s (%08x)\n", msg, (unsigned)code);
	upd_state = UPD_FAILED;
}

static void upd_check(void)
{
	upd_state = UPD_CHECKING;
	upd_err[0] = 0;
	if (!upd_net_init()) { upd_fail("Network is not available", 0); return; }
	if (!upd_online()) { upd_fail("Not connected to Wi-Fi", 0); return; }
	UpdSink sink = { NULL, 0, 0, -1 };
	int st = upd_http_get("https://api.github.com/repos/" UPD_REPO "/releases/latest", &sink, 1);
	if (st != 200) {
		free(sink.buf);
		upd_fail(st == 404 ? "No release found" : (st == 403 ? "GitHub is limiting requests, try again later" : "Could not reach GitHub"), st);
		return;
	}
	char tag[40] = "";
	if (!upd_json_str((const char *)sink.buf, "tag_name", tag, sizeof(tag))) { free(sink.buf); upd_fail("Unexpected answer from GitHub", 0); return; }
	snprintf(upd_tag, sizeof(upd_tag), "%s", tag);
	upd_notes[0] = 0;
	upd_json_str((const char *)sink.buf, "body", upd_notes, sizeof(upd_notes));
	int has_vpk = upd_find_vpk((const char *)sink.buf, upd_url, sizeof(upd_url));
	free(sink.buf);
	trace("update: latest %s, this %s, vpk %s\n", tag, APP_VERSION, has_vpk ? "yes" : "no");
	if (upd_ver(tag) > upd_ver(APP_VERSION)) {
		if (!has_vpk) { upd_fail("The new release has no .vpk file", 0); return; }
		upd_found = 1;
		upd_state = UPD_AVAILABLE;
	} else {
		upd_found = 0;
		upd_state = UPD_UPTODATE;
	}
}

/* ---- unpacking a .vpk (zip) ---- */

static uint32_t zr16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t zr32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void upd_mkdirs(const char *path)
{
	char tmp[300];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *c = tmp + 5; *c; c++)
		if (*c == '/') { *c = 0; sceIoMkdir(tmp, 0777); *c = '/'; }
}

static void upd_rmtree(const char *dir)
{
	SceUID d = sceIoDopen(dir);
	if (d >= 0) {
		SceIoDirent e;
		memset(&e, 0, sizeof(e));
		while (sceIoDread(d, &e) > 0) {
			char p[300];
			snprintf(p, sizeof(p), "%s/%s", dir, e.d_name);
			if (SCE_S_ISDIR(e.d_stat.st_mode)) upd_rmtree(p); else sceIoRemove(p);
			memset(&e, 0, sizeof(e));
		}
		sceIoDclose(d);
	}
	sceIoRmdir(dir);
}

typedef struct { char name[200]; uint32_t method, csize, usize, off, crc; } ZipEnt;
#define ZIP_MAX 700

/* Unpacks every file of the zip in zdata into UPD_DIR, checking the sizes and CRCs. Returns the number of files, or -1. */
static int upd_unzip(const uint8_t *z, size_t zn, ZipEnt *ents, char *why, size_t whycap)
{
	if (zn < 22) { snprintf(why, whycap, "File is too small"); return -1; }
	size_t e = zn - 22;
	while (e > 0 && zr32(z + e) != 0x06054b50u && zn - e < 70000) e--;
	if (zr32(z + e) != 0x06054b50u) { snprintf(why, whycap, "Not a zip file"); return -1; }
	uint32_t count = zr16(z + e + 10), cdoff = zr32(z + e + 16);
	if (count == 0 || count > ZIP_MAX || cdoff >= zn) { snprintf(why, whycap, "Unreadable zip"); return -1; }
	size_t p = cdoff;
	int n = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (p + 46 > zn || zr32(z + p) != 0x02014b50u) { snprintf(why, whycap, "Broken zip directory"); return -1; }
		uint32_t method = zr16(z + p + 10), crc = zr32(z + p + 16), cs = zr32(z + p + 20), us = zr32(z + p + 24);
		uint32_t nl = zr16(z + p + 28), xl = zr16(z + p + 30), cl = zr16(z + p + 32), lo = zr32(z + p + 42);
		if (p + 46 + nl > zn || nl >= sizeof(ents[0].name)) { snprintf(why, whycap, "Broken zip directory"); return -1; }
		ZipEnt *en = &ents[n];
		memcpy(en->name, z + p + 46, nl);
		en->name[nl] = 0;
		en->method = method; en->csize = cs; en->usize = us; en->off = lo; en->crc = crc;
		p += 46 + nl + xl + cl;
		if (nl && en->name[nl - 1] == '/') continue;                          /* a folder */
		if (strstr(en->name, "..")) { snprintf(why, whycap, "Unsafe path in the update"); return -1; }
		n++;
	}
	upd_rmtree(UPD_DIR);
	sceIoMkdir(UPD_DIR, 0777);
	for (int i = 0; i < n; i++) {
		ZipEnt *en = &ents[i];
		if (en->off + 30 > zn || zr32(z + en->off) != 0x04034b50u) { snprintf(why, whycap, "Broken zip entry"); return -1; }
		size_t data = en->off + 30 + zr16(z + en->off + 26) + zr16(z + en->off + 28);
		if (data + en->csize > zn) { snprintf(why, whycap, "Truncated zip"); return -1; }
		uint8_t *out = malloc(en->usize ? en->usize : 1);
		if (!out) { snprintf(why, whycap, "Out of memory"); return -1; }
		if (en->method == 0) {
			if (en->csize != en->usize) { free(out); snprintf(why, whycap, "Broken zip entry"); return -1; }
			memcpy(out, z + data, en->usize);
		} else if (en->method == 8) {
			z_stream zs;
			memset(&zs, 0, sizeof(zs));
			if (inflateInit2(&zs, -15) != Z_OK) { free(out); snprintf(why, whycap, "Inflate failed"); return -1; }
			zs.next_in = (Bytef *)(z + data); zs.avail_in = en->csize;
			zs.next_out = out; zs.avail_out = en->usize;
			int r = inflate(&zs, Z_FINISH);
			inflateEnd(&zs);
			if ((r != Z_STREAM_END && !(r == Z_OK && zs.avail_out == 0)) || zs.total_out != en->usize) { free(out); snprintf(why, whycap, "Inflate failed"); return -1; }
		} else {
			free(out); snprintf(why, whycap, "Unsupported zip method"); return -1;
		}
		if ((uint32_t)crc32(0, out, en->usize) != en->crc) { free(out); snprintf(why, whycap, "Checksum mismatch in %s", en->name); return -1; }
		char path[400];
		snprintf(path, sizeof(path), UPD_DIR "/%s", en->name);
		upd_mkdirs(path);
		SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		int ok = fd >= 0 && sceIoWrite(fd, out, en->usize) == (int)en->usize;
		if (fd >= 0) sceIoClose(fd);
		free(out);
		if (!ok) { snprintf(why, whycap, "Could not write %s", en->name); return -1; }
	}
	return n;
}

/* ---- installing through the system's package installer ----
 * This is VitaShell's method (GPL-3.0, Copyright (C) 2015-2018 TheFloW; package_installer.c): the package is unpacked into
 * ux0:data/pkg, a head.bin is made from VitaShell's template with the app's own title id, and the system's scePromoterUtil
 * installs it. It needs VitaShell's patch module (already how this app mounts encrypted game folders: vs_init). */

#define UPD_HEAD_BIN UPD_DIR "/sce_sys/package/head.bin"

static void upd_fpkg_hmac(const uint8_t *data, size_t len, uint8_t hmac[16])
{
	uint8_t sha[20], buf[64];
	mbedtls_sha1(data, len, sha);
	memset(buf, 0, 64);
	memcpy(&buf[0], &sha[4], 8);
	memcpy(&buf[8], &sha[4], 8);
	memcpy(&buf[16], &sha[12], 4);
	buf[20] = sha[16];
	buf[21] = sha[1];
	buf[22] = sha[2];
	buf[23] = sha[3];
	memcpy(&buf[24], &buf[16], 8);
	mbedtls_sha1(buf, 64, sha);
	memcpy(hmac, sha, 16);
}

static int upd_make_head_bin(void)
{
	char titleid[16] = "", contentid[64] = "";
	if (!sfo_get_string(UPD_DIR "/sce_sys/param.sfo", "TITLE_ID", titleid, sizeof(titleid))) return -3;
	sfo_get_string(UPD_DIR "/sce_sys/param.sfo", "CONTENT_ID", contentid, sizeof(contentid));
	if (strlen(titleid) != 9) return -3;
	uint8_t head[sizeof(upd_head_bin)];
	memcpy(head, upd_head_bin, sizeof(head));
	char full[48];
	snprintf(full, sizeof(full), "EP9000-%s_00-0000000000000000", titleid);
	strncpy((char *)&head[0x30], contentid[0] ? contentid : full, 48);
	uint8_t hmac[16];
	uint32_t off, len, out;
	memcpy(&len, &head[0xD0], 4); len = __builtin_bswap32(len);
	upd_fpkg_hmac(&head[0], len, hmac);                                         /* hmac of the pkg header */
	memcpy(&head[len], hmac, 16);
	memcpy(&off, &head[0x8], 4); off = __builtin_bswap32(off);
	memcpy(&len, &head[0x10], 4); len = __builtin_bswap32(len);
	memcpy(&out, &head[0xD4], 4); out = __builtin_bswap32(out);
	upd_fpkg_hmac(&head[off], len - 64, hmac);                                  /* hmac of the pkg info */
	memcpy(&head[out], hmac, 16);
	memcpy(&len, &head[0xE8], 4); len = __builtin_bswap32(len);
	upd_fpkg_hmac(&head[0], len, hmac);                                         /* hmac of everything */
	memcpy(&head[len], hmac, 16);
	sceIoMkdir(UPD_DIR "/sce_sys/package", 0777);
	SceUID fd = sceIoOpen(UPD_HEAD_BIN, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return -4;
	int ok = sceIoWrite(fd, head, sizeof(head)) == (int)sizeof(head);
	sceIoClose(fd);
	return ok ? 0 : -4;
}

static int upd_promote_dir(const char *path)
{
	if (!vs_init()) return -2;                                                  /* VitaShell's patch module */
	int mk = upd_make_head_bin();
	if (mk < 0) return mk;
	static uint32_t argp[] = { 0x180000, (uint32_t)-1, (uint32_t)-1, 1, (uint32_t)-1, (uint32_t)-1 };
	int result = -1;
	uint32_t opt[4] = { sizeof(opt), (uint32_t)(uintptr_t)&result, (uint32_t)-1, (uint32_t)-1 };
	int res = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, (const SceSysmoduleOpt *)opt);
	if (res < 0) { trace("update: paf load failed %08x\n", (unsigned)res); return res; }
	res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	trace("update: promoter module load %08x\n", (unsigned)res);
	if (res >= 0) { res = scePromoterUtilityInit(); trace("update: promoter init %08x\n", (unsigned)res); }
	if (res >= 0) { res = scePromoterUtilityPromotePkgWithRif(path, 1); trace("update: promote returned %08x\n", (unsigned)res); }
	scePromoterUtilityExit();
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	uint32_t unload = 0;
	sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, (const SceSysmoduleOpt *)&unload);
	return res;
}

static uint8_t *upd_read_file(const char *path, int *len)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return NULL;
	int n = sceIoLseek32(fd, 0, SCE_SEEK_END);
	sceIoLseek32(fd, 0, SCE_SEEK_SET);
	uint8_t *z = n > 0 ? malloc((size_t)n) : NULL;
	if (z && sceIoRead(fd, z, n) != n) { free(z); z = NULL; }
	sceIoClose(fd);
	if (z) *len = n;
	return z;
}

/* unpacks a .vpk into ux0:data/pkg (checking every CRC); the number of files, or -1 with a reason */
static int upd_unpack_vpk(const char *path, char *why, size_t cap)
{
	int zn = 0;
	uint8_t *z = upd_read_file(path, &zn);
	if (!z) { snprintf(why, cap, "Could not read %s", path); return -1; }
	static ZipEnt ents[ZIP_MAX];
	int n = upd_unzip(z, (size_t)zn, ents, why, cap);
	free(z);
	if (n <= 0) { if (!why[0]) snprintf(why, cap, "Could not unpack %s", path); return -1; }
	int have_eboot = 0;
	for (int i = 0; i < n; i++) if (strcmp(ents[i].name, "eboot.bin") == 0) have_eboot = 1;
	if (!have_eboot) { snprintf(why, cap, "The package has no eboot.bin"); return -1; }
	return n;
}

/* the "VitaXMB Updater" app installs updates while VitaXMB is closed; its package is carried inside VitaXMB (updater.vpk) and put on the Vita the first time */
static int upd_ensure_helper(char *why, size_t cap)
{
	SceIoStat st;
	if (sceIoGetstat("ux0:app/VXMBUPDTR/eboot.bin", &st) >= 0) {
		char have[16] = "";
		sfo_get_string("ux0:app/VXMBUPDTR/sce_sys/param.sfo", "APP_VER", have, sizeof(have));
		if (strcmp(have, UPD_HELPER_VER) == 0) return 0;                      /* there and current */
	}
	if (upd_unpack_vpk("app0:updater.vpk", why, cap) < 0) return -1;
	int pr = upd_promote_dir(UPD_DIR);
	upd_rmtree(UPD_DIR);
	if (pr < 0) {
		if (pr == -2) snprintf(why, cap, "VitaShell's modules are needed to install (ux0:VitaShell)");
		else snprintf(why, cap, "Could not install the updater app (%08X)", (unsigned)pr);
		return -1;
	}
	trace("update: the updater app is installed\n");
	return 0;
}

static void upd_install(void)
{
	upd_state = UPD_INSTALLING;
	upd_err[0] = 0;
	char why[140] = "";
	if (upd_ensure_helper(why, sizeof(why)) < 0) { snprintf(upd_err, sizeof(upd_err), "%s", why); upd_state = UPD_SAVED; return; }
	if (upd_unpack_vpk(UPD_VPK, why, sizeof(why)) < 0) { upd_rmtree(UPD_DIR); upd_fail(why, 0); return; }
	/* the package waits unpacked in ux0:data/pkg with its head.bin; the updater app installs it once VitaXMB is closed */
	if (!vs_init()) { upd_rmtree(UPD_DIR); snprintf(upd_err, sizeof(upd_err), "VitaShell's modules are needed to install (ux0:VitaShell)"); upd_state = UPD_SAVED; return; }
	if (upd_make_head_bin() < 0) { upd_rmtree(UPD_DIR); upd_fail("Could not prepare the package", 0); return; }
	sceIoRemove(CONFIG_DIR "/update_result.txt");
	/* The hand-over (start the updater app, leave at once) is done by the main thread, exactly as a game is started: see main(). */
	upd_state = UPD_DONE;
	upd_handover = 1;
	for (;;) sceKernelDelayThread(1000 * 1000);
}

static void upd_download_and_install(void)
{
	upd_state = UPD_DOWNLOADING;
	upd_err[0] = 0;
	upd_cancel = 0;
	if (!upd_net_init() || !upd_online()) { upd_fail("Not connected to Wi-Fi", 0); return; }
	sceIoRemove(UPD_VPK);
	SceUID fd = sceIoOpen(UPD_VPK, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) { upd_fail("Could not create the download file", fd); return; }
	UpdSink sink = { NULL, 0, 0, fd };
	int st = upd_http_get(upd_url, &sink, 0);
	sceIoClose(fd);
	if (st != 200) {
		sceIoRemove(UPD_VPK);
		if (st == -2) { upd_state = UPD_AVAILABLE; return; }                  /* cancelled by the user */
		upd_fail("Download failed", st);
		return;
	}
	upd_install();
}

static int upd_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	for (;;) {
		int c = upd_cmd;
		if (c == UPD_CMD_NONE) { sceKernelDelayThread(100 * 1000); continue; }
		upd_cmd = UPD_CMD_NONE;
		if (c == UPD_CMD_CHECK) upd_check();
		else if (c == UPD_CMD_INSTALL) upd_download_and_install();
	}
	return 0;
}

/* after the updater app has run: what it wrote (VitaXMB is started again by it) */
static char upd_result_msg[100];
static void upd_read_result(void)
{
	int n = 0;
	uint8_t *r = upd_read_file(CONFIG_DIR "/update_result.txt", &n);
	if (!r) return;
	char t[64];
	snprintf(t, sizeof(t), "%.*s", n < 63 ? n : 63, (const char *)r);
	free(r);
	sceIoRemove(CONFIG_DIR "/update_result.txt");
	if (strncmp(t, "ok", 2) == 0) snprintf(upd_result_msg, sizeof(upd_result_msg), "VitaXMB was updated to %s", upd_tag[0] ? upd_tag : "the new version");
	else snprintf(upd_result_msg, sizeof(upd_result_msg), "The update did not install (%s)", t);
	trace("update: updater said: %s\n", t);
}

static void upd_start_thread(void)
{
	static SceUID th = -1;
	if (th >= 0) return;
	th = sceKernelCreateThread("xmb_update", upd_thread, 0x10000110, 0x20000, 0, 0, NULL);
	if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

static void upd_request(int cmd)
{
	upd_start_thread();
	if (cmd == UPD_CMD_CHECK && (upd_state == UPD_CHECKING || upd_state == UPD_DOWNLOADING || upd_state == UPD_INSTALLING)) return;
	upd_cmd = cmd;
}
