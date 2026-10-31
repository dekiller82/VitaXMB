/*
 * wave.h - the PSP's moving background.
 *
 * The XMB wave is a GMO model ("OMG.00.1PSP", system_plugin_bg.rco, mdl_bg): one cubic B-spline patch of 22 x 11
 * control points with two morph targets, a reflection map, and a 1500-frame motion at 30 frames a second
 * (a 4x4 matrix curve and the two morph weights). A theme may bring its own model; the firmware's is built in.
 * The model is read as it is stored, so any theme's file works as long as it has this one-patch layout.
 *
 * Camera: the stock scene looks down -Z from 500 units (the RCO camera animation holds 500 and 25), and
 * the model is shown at about 7.5 pixels a unit on a 480x272 screen.
 */

#define WAVE_MAX_U 40
#define WAVE_MAX_V 24
#define WAVE_GU 48                                   /* samples along the patch, columns */
#define WAVE_GV 24                                   /* and rows */
#define WAVE_FU 76                                   /* the firmware's look cuts every span of the 22 x 11 control points 4 x 4, as the GE does (19 x 4 columns) */
#define WAVE_FV 32                                   /* (8 x 4 rows) */
#define WAVE_MAX_T 6                                 /* morph targets (the wave has 2, the boot ribbon 5) */

typedef struct {
	int ok;
	int nu, nv;                                     /* control points */
	int nt;                                         /* morph targets */
	float ctrl[WAVE_MAX_T][1024][3];                /* morph targets, in control-grid order */
	float bu[WAVE_GU + 1][WAVE_MAX_U], bv[WAVE_GV + 1][WAVE_MAX_V];    /* basis functions at the samples */
	float bu2[WAVE_FU + 1][WAVE_MAX_U], bv2[WAVE_FV + 1][WAVE_MAX_V];  /* and at the firmware look's finer samples */
	float dbu[WAVE_GU + 1][WAVE_MAX_U], dbv[WAVE_GV + 1][WAVE_MAX_V];  /* their derivatives (the surface normals are the cross product of the surface's partials) */
	float dbu2[WAVE_FU + 1][WAVE_MAX_U], dbv2[WAVE_FV + 1][WAVE_MAX_V];
	float *mat;                                     /* matrix keys: time + 16 floats */
	int n_mat;
	float *mor;                                     /* morph keys: time + one weight per target */
	int n_mor;
	const uint8_t *tex;                             /* reflection map, 8-bit grey, 256 wide */
	int tex_h;
	float *refl;                                    /* the reflection map decoded from its 8-bit palette TGA: brightness 0..1, rw x rw */
	int rw;
	vita2d_texture *rtex;                           /* the same map as a texture, brightness scaled up to the full 8 bits (made on first use) */
	float rmax;                                     /* the map's brightest value, the scale used for rtex */
	float end;                                      /* loop length in frames */
	float fps;
	uint8_t *own;                                   /* copy of the model bytes */
} WaveModel;

static WaveModel wave_m, boot_m;
static int wave_off;                                  /* a theme whose background file has no wave: none is drawn */

static uint32_t wv_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float wv_f(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static int wv_container(int t) { return t == 2 || t == 3 || t == 4 || t == 5 || t == 6 || t == 8 || t == 9 || t == 0xa || t == 0xb || t == 0xf; }

/* The GE has no knot input: a spline patch is a uniform B-spline whose two ends are clamped or open by the patch's edge flags (bit 0 the start, bit 1 the end;
 * the firmware passes them on from the model's patch chunk, bits 12-13 for u and 14-15 for v, paf.prx 0x299bc). The model's own knot chunks (the stock
 * wave's v knots are not uniform) are ignored by the hardware, so they are here. */
static void wv_knots(int n, int edge, float *kn)
{
	int sp = n - 3;
	for (int i = 0; i < n + 4; i++) {
		int k = i - 3;
		if ((edge & 1) && k < 0) k = 0;
		if ((edge & 2) && k > sp) k = sp;
		kn[i] = (float)k / (float)sp;
	}
}

static void wv_basis(const float *kn, int nk, int n, float u, float *out, float *dout)
{
	float B[4][64];
	memset(B, 0, sizeof(B));
	int last = 0;
	for (int i = 0; i + 1 < nk; i++) if (kn[i] < 1.0f) last = i;
	for (int i = 0; i + 1 < nk; i++)
		B[0][i] = ((kn[i] <= u && u < kn[i + 1]) || (u >= 1.0f && i == last)) ? 1.0f : 0.0f;
	for (int k = 1; k <= 3; k++)
		for (int i = 0; i + 1 + k < nk; i++) {
			float a = 0.0f, d1 = kn[i + k] - kn[i], d2 = kn[i + k + 1] - kn[i + 1];
			if (d1 > 0.0f) a += (u - kn[i]) / d1 * B[(k - 1) & 3][i];
			if (d2 > 0.0f) a += (kn[i + k + 1] - u) / d2 * B[(k - 1) & 3][i + 1];
			B[k & 3][i] = a;
		}
	/* B[0..3] is a ring, the cubic row is index 3 */
	for (int i = 0; i < n; i++) out[i] = B[3][i];
	for (int i = 0; i < n; i++) {                   /* N' = 3 (N2[i] / (k[i+3] - k[i]) - N2[i+1] / (k[i+4] - k[i+1])) from the quadratic row */
		float e1 = kn[i + 3] - kn[i], e2 = kn[i + 4] - kn[i + 1];
		dout[i] = 3.0f * ((e1 > 0.0f ? B[2][i] / e1 : 0.0f) - (e2 > 0.0f ? B[2][i + 1] / e2 : 0.0f));
	}
}

static void wv_free(WaveModel *m)
{
	free(m->mat); free(m->mor); free(m->own); free(m->refl);
	if (m->rtex) defer_free(m->rtex);                /* the GPU may still be drawing with it */
	memset(m, 0, sizeof(*m));
}

/* Parses a GMO; returns 1 when it holds a patch this renderer can draw. */
static int wv_parse(WaveModel *M, const uint8_t *g, size_t n)
{
	if (n < 64 || memcmp(g, "OMG.00.1PSP", 11) != 0) return 0;
	const uint8_t *arr = NULL, *patch = NULL, *ku = NULL, *kv = NULL, *tex = NULL;
	size_t arr_n = 0, tex_n = 0;
	float end = 1500.0f, fps = 30.0f;
	const uint8_t *fc[4] = { 0 };
	size_t fc_n[4] = { 0 };
	int nfc = 0;
	/* iterative walk: containers are entered, other chunks skipped */
	size_t stack[32][2];
	int sp = 0;
	size_t pos = 16, end_pos = n;
	for (;;) {
		if (pos + 8 > end_pos) {
			if (sp == 0) break;
			sp--; pos = stack[sp][0]; end_pos = stack[sp][1];
			continue;
		}
		uint32_t tsh = wv_u32(g + pos), sz = wv_u32(g + pos + 4);
		int t = tsh & 0xFFFF, sh = tsh >> 16;
		if (sz < 8 || pos + sz > end_pos) break;
		const uint8_t *body = g + pos + 8;
		if (t == 7) { arr = g + pos; arr_n = sz; }
		else if (t == 0x8068) patch = g + pos;
		else if (t == 0x8064) ku = g + pos;
		else if (t == 0x8065) kv = g + pos;
		else if (t == 0x8013) { tex = g + pos; tex_n = sz; }
		else if (t == 0x80b1 && sz >= 16) end = wv_f(body + 4);
		else if (t == 0x80b2 && sz >= 12) fps = wv_f(body);
		else if (t == 0xc && nfc < 4) { fc[nfc] = g + pos; fc_n[nfc] = sz; nfc++; }
		size_t next = pos + sz;
		if (wv_container(t) && sz > 8 && sp < 31) {
			stack[sp][0] = next; stack[sp][1] = end_pos; sp++;
			end_pos = next;
			pos += sh > 8 ? (size_t)sh : 8;
			continue;
		}
		pos = next;
	}
	if (!arr || !patch || !ku || !kv || !tex || nfc < 2 || end <= 0.0f || fps <= 0.0f) return 0;

	int nu = (int)wv_u32(patch + 8 + 8), nv = (int)wv_u32(patch + 8 + 12);
	if (nu < 4 || nv < 4 || nu > WAVE_MAX_U || nv > WAVE_MAX_V || nu * nv > 1024) return 0;
	uint32_t count = wv_u32(arr + 8 + 24);
	if (count != (uint32_t)(nu * nv) || count == 0) return 0;
	int nt = (int)((arr_n - 44) / ((size_t)count * 12));
	if (nt < 1 || nt > WAVE_MAX_T) return 0;
	M->nt = nt;
	uint32_t psz = wv_u32(patch + 4);
	if (psz < 8 + 16 + (size_t)nu * nv * 2) return 0;

	M->nu = nu; M->nv = nv;
	const uint8_t *vd = arr + 44;
	for (int i = 0; i < nu * nv; i++) {
		uint16_t ix;
		memcpy(&ix, patch + 8 + 16 + 2 * i, 2);
		if (ix >= count) return 0;
		for (int m = 0; m < nt; m++)
			for (int c = 0; c < 3; c++) M->ctrl[m][i][c] = wv_f(vd + (size_t)ix * nt * 12 + m * 12 + c * 4);
	}

	float knu[64], knv[64];
	int nku = (ku[8] - '0') * 10 + (ku[9] - '0'), nkv = (kv[8] - '0') * 10 + (kv[9] - '0');
	if (nku != nu + 4 || nkv != nv + 4 || nku > 60 || nkv > 60) return 0;
	uint32_t pflags = wv_u32(patch + 8 + 4);
	wv_knots(nu, (int)((pflags >> 12) & 3), knu);       /* (the knot chunks only have to be there) */
	wv_knots(nv, (int)((pflags >> 14) & 3), knv);
	for (int i = 0; i <= WAVE_GU; i++) wv_basis(knu, nku, nu, fminf((float)i / WAVE_GU, 0.99999f), M->bu[i], M->dbu[i]);
	for (int j = 0; j <= WAVE_GV; j++) wv_basis(knv, nkv, nv, fminf((float)j / WAVE_GV, 0.99999f), M->bv[j], M->dbv[j]);
	for (int i = 0; i <= WAVE_FU; i++) wv_basis(knu, nku, nu, fminf((float)i / WAVE_FU, 0.99999f), M->bu2[i], M->dbu2[i]);
	for (int j = 0; j <= WAVE_FV; j++) wv_basis(knv, nkv, nv, fminf((float)j / WAVE_FV, 0.99999f), M->bv2[j], M->dbv2[j]);

	for (int k = 0; k < nfc; k++) {                 /* curves: header 36 bytes, then (time + dims) floats per key */
		uint32_t dims = wv_u32(fc[k] + 8 + 24), keys = wv_u32(fc[k] + 8 + 28);
		if ((dims != 16 && dims != (uint32_t)nt) || keys < 2 || keys > 4096 || 8 + 36 + (size_t)keys * (dims + 1) * 4 > fc_n[k]) continue;
		float *a = malloc((size_t)keys * (dims + 1) * sizeof(float));
		if (!a) return 0;
		memcpy(a, fc[k] + 8 + 36, (size_t)keys * (dims + 1) * sizeof(float));
		if (dims == 16) { M->mat = a; M->n_mat = (int)keys; }
		else { M->mor = a; M->n_mor = (int)keys; }
	}
	if (!M->mat || !M->mor) return 0;

	/* the reflection map: a header of 24 bytes, then 256 grey pixels per row */
	if (tex_n < 8 + 24 + 256 * 8) return 0;
	M->tex = tex + 8 + 24;
	M->tex_h = (int)((tex_n - 8 - 24) / 256);
	/* The texture chunk is really a size and then a colour-mapped TGA (the file name says reflection_white_09_s4_8bit.tga): 128 x 128 indices into a
	 * 24-bit palette whose brightest entry is 63. It is a sphere map: black in the middle, a faint rim. */
	if (tex_n > 8 + 4 + 18 + 768 + 128 * 128) {
		const uint8_t *t = tex + 8 + 4;
		int idl = t[0], cmt = t[1], it = t[2], cml = t[5] | (t[6] << 8), cmd = t[7], w = t[12] | (t[13] << 8), h = t[14] | (t[15] << 8), bpp = t[16];
		if (cmt == 1 && it == 1 && cmd == 24 && bpp == 8 && cml > 0 && cml <= 256 && w == h && w >= 16 && w <= 256 && !idl &&
		    tex_n >= (size_t)(8 + 4 + 18 + cml * 3 + w * h)) {
			float *r = malloc(sizeof(float) * (size_t)w * h);
			if (r) {
				const uint8_t *pal = t + 18, *px = pal + cml * 3;
				for (int i = 0; i < w * h; i++) { int ix = px[i] < cml ? px[i] : cml - 1; r[i] = (pal[ix * 3] + pal[ix * 3 + 1] + pal[ix * 3 + 2]) / (3.0f * 255.0f); }
				M->refl = r; M->rw = w;
			}
		}
	}
	M->end = end;
	M->fps = fps;
	return 1;
}

/* Installs a model into m from bytes (NULL or unreadable: the given fallback bytes). */
static void wv_install(WaveModel *m, const uint8_t *gmo, size_t n, const uint8_t *fallback, size_t fn)
{
	wv_free(m);
	for (int pass = 0; pass < 2; pass++) {
		const uint8_t *src = pass == 0 ? gmo : fallback;
		size_t len = pass == 0 ? n : fn;
		if (!src || !len) continue;
		uint8_t *own = malloc(len);
		if (!own) return;
		memcpy(own, src, len);
		m->own = own;
		if (wv_parse(m, own, len)) { m->ok = 1; return; }
		wv_free(m);
	}
}

/* The background wave: a theme's own model, or the firmware's when gmo is NULL. */
static void wave_set_model(const uint8_t *gmo, size_t n)
{
	wv_install(&wave_m, gmo, n, wave_stock_gmo, sizeof(wave_stock_gmo));
}

static void wv_interp(const float *a, int n, int dims, float t, float *out)
{
	int stride = dims + 1, i = 0;
	while (i + 2 < n && a[(i + 1) * stride] <= t) i++;
	float t0 = a[i * stride], t1 = a[(i + 1) * stride];
	float f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
	if (f < 0.0f) f = 0.0f;
	if (f > 1.0f) f = 1.0f;
	for (int k = 0; k < dims; k++) out[k] = a[i * stride + 1 + k] * (1.0f - f) + a[(i + 1) * stride + 1 + k] * f;
}

/* The reflection map's brightness (0..1) for a surface normal's x and y (each -1..1, the normal divided by its length), bilinear. */
static float wv_refl(const WaveModel *M, float nx, float ny)
{
	int W = M->rw;
	float fx = (0.5f - 0.5f * nx) * (W - 1), fy = (0.5f - 0.5f * ny) * (W - 1);
	int x0 = (int)fx, y0 = (int)fy;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x0 > W - 2) x0 = W - 2;
	if (y0 > W - 2) y0 = W - 2;
	float ax = fx - x0, ay = fy - y0;
	if (ax < 0.0f) ax = 0.0f;
	if (ax > 1.0f) ax = 1.0f;
	if (ay < 0.0f) ay = 0.0f;
	if (ay > 1.0f) ay = 1.0f;
	return (M->refl[y0 * W + x0] * (1 - ax) + M->refl[y0 * W + x0 + 1] * ax) * (1 - ay) +
	       (M->refl[(y0 + 1) * W + x0] * (1 - ax) + M->refl[(y0 + 1) * W + x0 + 1] * ax) * ay;
}

/* The way the firmware draws the ribbon (smooth = 3). Read from the firmware (paf.prx 0x1149ac, the model object's draw) and fitted to a capture of a real
 * XMB (shots/cmp_PspEmu.png): the model is drawn with face culling on, with its material (lighting and texture on) adding the reflection map to
 * the picture behind (ADD): nothing else, no translucent sheet. The map is looked up with the surface normal; the GE interpolates that lookup
 * coordinate across each triangle and samples the map per pixel, which keeps the crest lines thin. Here the map is a texture drawn additively over the
 * model's triangles with the normal's lookup coordinates as their UVs, the same. Which faces are culled was decided by comparing the picture with the
 * capture (the other direction correlates 0.14 against 0.38), the gain by the distribution of the lift over the sky in the capture (the median is
 * zero, the 90th percentile 30 of 255, the brightest rims about 57): 0.75 reproduces it (30 / 34, 57 / 58) once the white veil is gone from the sky.
 * (Looked up per vertex the lines smear into broad glows, because the model's samples are further apart than the lines are wide.) */
#define WV_REFL_GAIN 0.75f

/* Draws a ribbon model at the given frame in the given colour; alpha 0..1 scales the whole thing. */
static void wv_draw(const WaveModel *M, float frame, const unsigned char col[3], float alpha, int smooth)
{
	if (!M->ok || alpha <= 0.0f) return;
	const int NU = M->nu, NV = M->nv;
	if (smooth == 3 && M->refl && !M->rtex && M->rw > 1) {      /* the map as a texture, once */
		uint8_t *rgba = malloc((size_t)M->rw * M->rw * 4);
		if (rgba) {
			float mx = 0.0f;
			for (int i = 0; i < M->rw * M->rw; i++) if (M->refl[i] > mx) mx = M->refl[i];
			if (mx < 0.01f) mx = 0.01f;
			for (int i = 0; i < M->rw * M->rw; i++) {
				int v = (int)(M->refl[i] / mx * 255.0f + 0.5f);
				rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = (uint8_t)(v > 255 ? 255 : v);
				rgba[i * 4 + 3] = 255;
			}
			((WaveModel *)M)->rtex = texture_from_rgba(rgba, M->rw, M->rw);
			((WaveModel *)M)->rmax = mx;
			free(rgba);
		}
	}
	const int firm = smooth == 3 && M->refl && M->rtex;         /* the firmware's look: the reflection added, nothing else */
	const int GUc = firm ? WAVE_FU : WAVE_GU, GVc = firm ? WAVE_FV : WAVE_GV;       /* the samples along the patch and across it */
	const float (*BU)[WAVE_MAX_U] = firm ? M->bu2 : M->bu, (*BV)[WAVE_MAX_V] = firm ? M->bv2 : M->bv;
	const float (*DBU)[WAVE_MAX_U] = firm ? M->dbu2 : M->dbu, (*DBV)[WAVE_MAX_V] = firm ? M->dbv2 : M->dbv;
	float m[16], w[WAVE_MAX_T];
	wv_interp(M->mat, M->n_mat, 16, frame, m);
	wv_interp(M->mor, M->n_mor, M->nt, frame, w);

	static float P[1024][3], row[WAVE_MAX_V][WAVE_FU + 1][3], rowD[WAVE_MAX_V][WAVE_FU + 1][3], S[WAVE_FU + 1][WAVE_FV + 1][3], Q[WAVE_FU + 1][WAVE_FV + 1][3];
	static float Su[WAVE_FU + 1][WAVE_FV + 1][3], Sv[WAVE_FU + 1][WAVE_FV + 1][3];
	for (int i = 0; i < NU * NV; i++)
		for (int c = 0; c < 3; c++) {
			float s = 0.0f;
			for (int k = 0; k < M->nt; k++) s += M->ctrl[k][i][c] * w[k];
			P[i][c] = s;
		}
	for (int v = 0; v < NV; v++)                  /* control points run along u first */
		for (int i = 0; i <= GUc; i++)
			for (int c = 0; c < 3; c++) {
				float s = 0.0f;
				for (int u = 0; u < NU; u++) s += BU[i][u] * P[v * NU + u][c];
				row[v][i][c] = s;
				float d = 0.0f;
				for (int u = 0; u < NU; u++) d += DBU[i][u] * P[v * NU + u][c];
				rowD[v][i][c] = d;
			}
	for (int i = 0; i <= GUc; i++)
		for (int j = 0; j <= GVc; j++)
			for (int c = 0; c < 3; c++) {
				float s = 0.0f;
				for (int v = 0; v < NV; v++) s += BV[j][v] * row[v][i][c];
				S[i][j][c] = s;
				float a = 0.0f, b = 0.0f;
				for (int v = 0; v < NV; v++) { a += BV[j][v] * rowD[v][i][c]; b += DBV[j][v] * row[v][i][c]; }
				Su[i][j][c] = a; Sv[i][j][c] = b;
			}
	/* the motion's matrix, rows as stored: x' = M * x */
	for (int i = 0; i <= GUc; i++)
		for (int j = 0; j <= GVc; j++)
			for (int r = 0; r < 3; r++)
				Q[i][j][r] = m[r * 4] * S[i][j][0] + m[r * 4 + 1] * S[i][j][1] + m[r * 4 + 2] * S[i][j][2];

	const float dist = 500.0f, scale = 15.0f;       /* 7.5 px a unit at 480x272, doubled */
	static vita2d_color_vertex pts[(WAVE_FU + 1) * (WAVE_FV + 1)];
	static float nxy[(WAVE_FU + 1) * (WAVE_FV + 1)][2];       /* the normal's x and y over its length, per sample */
	if (smooth == 3) smooth = 1;                                /* the sheet is the smooth = 1 look, only fainter */
	for (int i = 0; i <= GUc; i++)
		for (int j = 0; j <= GVc; j++) {
			/* the surface's own normal: the model has none (its vertex type is positions and two morph targets), so the GE takes the cross product of the
			 * spline's partial derivatives; differences between neighbouring samples are one-sided at the patch edge and put a 13 degree jump across
			 * the model's seam, where u = 0 and u = 1 meet smoothly */
			float du[3], dv[3], n[3];
			for (int r = 0; r < 3; r++) {
				du[r] = m[r * 4] * Su[i][j][0] + m[r * 4 + 1] * Su[i][j][1] + m[r * 4 + 2] * Su[i][j][2];
				dv[r] = m[r * 4] * Sv[i][j][0] + m[r * 4 + 1] * Sv[i][j][1] + m[r * 4 + 2] * Sv[i][j][2];
			}
			n[0] = du[1] * dv[2] - du[2] * dv[1];
			n[1] = du[2] * dv[0] - du[0] * dv[2];
			n[2] = du[0] * dv[1] - du[1] * dv[0];
			float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-9f;
			nxy[i * (GVc + 1) + j][0] = n[0] / len;
			nxy[i * (GVc + 1) + j][1] = n[1] / len;
			int tu = (int)((n[0] / len * 0.5f + 0.5f) * 255.0f), tv = (int)((n[1] / len * 0.5f + 0.5f) * (M->tex_h - 1));
			if (tu < 0) tu = 0;
			if (tu > 255) tu = 255;
			if (tv < 0) tv = 0;
			if (tv > M->tex_h - 1) tv = M->tex_h - 1;
			float g = M->tex[tv * 256 + tu] / 255.0f;          /* 1 = bright sky in the reflection */
			float z = dist - Q[i][j][2];
			float s = scale * dist / z;
			vita2d_color_vertex *p = &pts[i * (GVc + 1) + j];
			p->x = 480.0f + Q[i][j][0] * s;
			p->y = 272.0f - Q[i][j][1] * s;
			p->z = 0.5f;
			if (smooth == 2 && M->refl) {
				/* the firmware's material: ADD, SRC_ALPHA, ONE - the reflection (about 0.87 of the map's grey, from a capture of the real XMB) is added to what is behind */
				float b = wv_refl(M, n[0] / len, n[1] / len);
				b *= 0.87f * alpha;
				p->color = RGBA8((int)(col[0] * b), (int)(col[1] * b), (int)(col[2] * b), 255);
				continue;
			}
			int a = (int)((26.0f + 46.0f * (1.0f - g)) * alpha);
			int mix = (int)(g * 120.0f);                            /* bright parts lean to white */
			if (smooth) {                                           /* the reflection photo is cloudy: use the facing angle instead, so the ribbon is smooth with lit creases */
				float edge = 1.0f - fabsf(n[2]) / len;
				a = (int)((16.0f + 54.0f * edge + 8.0f * (1.0f - g)) * alpha);
				mix = (int)(60.0f + 150.0f * edge);
				if (mix > 255) mix = 255;
			}
			p->color = RGBA8(col[0] + (255 - col[0]) * mix / 255, col[1] + (255 - col[1]) * mix / 255, col[2] + (255 - col[2]) * mix / 255, a);
		}
	if (!firm) {                                                  /* (the firmware's look draws only the reflection, below) */
		int nv_out = GUc * GVc * 6;
		vita2d_color_vertex *v = vita2d_pool_memalign(nv_out * sizeof(*v), sizeof(*v));
		if (!v) return;
		int k = 0;
		for (int i = 0; i < GUc; i++)
			for (int j = 0; j < GVc; j++) {
				const vita2d_color_vertex *a = &pts[i * (GVc + 1) + j], *b = &pts[(i + 1) * (GVc + 1) + j];
				const vita2d_color_vertex *c = &pts[(i + 1) * (GVc + 1) + j + 1], *d = &pts[i * (GVc + 1) + j + 1];
				v[k++] = *a; v[k++] = *b; v[k++] = *c;
				v[k++] = *a; v[k++] = *c; v[k++] = *d;
			}
		if (smooth == 2 && M->refl) vita2d_set_blend_mode_add(1);
		vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, nv_out);
		if (smooth == 2 && M->refl) vita2d_set_blend_mode_add(0);
	}

	if (firm) {
		const float W = (float)M->rw;
		/* face culling: of each triangle only the side the firmware keeps is drawn (positive area with y down) */
		#define WV_KEEP(p0, p1, p2) (((p1)->x - (p0)->x) * ((p2)->y - (p0)->y) - ((p2)->x - (p0)->x) * ((p1)->y - (p0)->y) > 0.0f)
		int kept = 0;
		for (int i = 0; i < GUc; i++)
			for (int j = 0; j < GVc; j++) {
				const vita2d_color_vertex *q0 = &pts[i * (GVc + 1) + j], *q1 = &pts[(i + 1) * (GVc + 1) + j], *q2 = &pts[(i + 1) * (GVc + 1) + j + 1], *q3 = &pts[i * (GVc + 1) + j + 1];
				kept += (WV_KEEP(q0, q1, q2) ? 3 : 0) + (WV_KEEP(q0, q2, q3) ? 3 : 0);
			}
		if (kept == 0) return;
		vita2d_texture_vertex *tv = vita2d_pool_memalign((size_t)kept * sizeof(*tv), sizeof(*tv));
		if (!tv) return;
		int k2 = 0;
		for (int i = 0; i < GUc; i++)
			for (int j = 0; j < GVc; j++) {
				int id[4] = { i * (GVc + 1) + j, (i + 1) * (GVc + 1) + j, (i + 1) * (GVc + 1) + j + 1, i * (GVc + 1) + j + 1 };
				vita2d_texture_vertex q[4];
				for (int c = 0; c < 4; c++) {
					q[c].x = pts[id[c]].x; q[c].y = pts[id[c]].y; q[c].z = 0.5f;
					q[c].u = ((0.5f - 0.5f * nxy[id[c]][0]) * (W - 1.0f) + 0.5f) / W;
					q[c].v = ((0.5f - 0.5f * nxy[id[c]][1]) * (W - 1.0f) + 0.5f) / W;
				}
				if (WV_KEEP(&q[0], &q[1], &q[2])) { tv[k2++] = q[0]; tv[k2++] = q[1]; tv[k2++] = q[2]; }
				if (WV_KEEP(&q[0], &q[2], &q[3])) { tv[k2++] = q[0]; tv[k2++] = q[2]; tv[k2++] = q[3]; }
			}
		#undef WV_KEEP
		int g = (int)(M->rmax * WV_REFL_GAIN * alpha * 255.0f + 0.5f);       /* the tint scales the map back to its own brightness times the gain */
		if (g < 0) g = 0;
		if (g > 255) g = 255;
		vita2d_set_blend_mode_add(1);
		vita2d_draw_array_textured(M->rtex, SCE_GXM_PRIMITIVE_TRIANGLES, tv, (size_t)k2, RGBA8((g * col[0]) / 255, (g * col[1]) / 255, (g * col[2]) / 255, 255));
		vita2d_set_blend_mode_add(0);
	}
}

/* The background wave at time t seconds. */
static void wave_draw(float t, const unsigned char col[3], float alpha)
{
	if (wave_m.ok) wv_draw(&wave_m, fmodf(t * wave_m.fps, wave_m.end), col, alpha, wave_m.refl ? 3 : 1);
}
