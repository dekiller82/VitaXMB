#pragma once

#include "swave_data.h"

/* ------------------------------------------------------------------ */
/* The PSP's default XMB wave ("SolidWave", paf.prx 0x1177a0 / 0x1183b4 / 0x11855c)                    */
/* ------------------------------------------------------------------ */

/* What the firmware does (read from its code, run in an emulator and checked against the 6.61 theme picture of the Original theme):
 *  - a bank of slow sine oscillators (base + amp * sin(phase - x * freq)); each frame their phases advance by speed * dt, where dt is a
 *    slow oscillator itself (1.05 +- 0.05) times 1.5 / 60. Three of them (R0..R2) are the wave's "height" shapes; their own amplitude, speed
 *    and frequency are driven by the other oscillators (Ra, Rs, Rf).
 *  - two heights per column (16 columns): hA from t = i / 16, hB from t + 0.0498; plus the column ripples o304 / o31c (layer A) and o2ec (B).
 *  - the heights make two 16 x 9 control meshes: row j is a quarter turn of a cylinder, y = cos * ((1 - cos) * (h - 90) + h), z = 90 * sin, with the angles
 *    0, 9, 16, 25, 36, 49, 64, 80, 90 degrees; x = (i - 7.5) * 480 / 13, so the 13 segments of the open spline cover x -240..240.
 *  - each mesh is drawn as a cubic B-spline patch (4 x 4 divisions, here 3), lighting on with no lights, an environment map (lights 2 and 3 at (-1,0,0) and
 *    (0,-1,0)) of a white 32 x 64 texture with an alpha picture, texture function modulate with alpha, vertex colour = the month's colour with the row's alpha,
 *    and ADDITIVE blending (source * source alpha + destination).
 * Units are PSP pixels with y up from the bottom; the picture of the Original theme puts the top edge 21 pixels up from the bottom. */

typedef struct { float base, amp, speed, freq, phase; } SwOsc;

static SwOsc sw_o[SW_NREC];
static float sw_speed = 1.5f;                        /* 0x118440 */
static float sw_one_b = 1.0f;                        /* 0x1187d0: the "1" of layer B's (1 - t) and its ripple factor */
static float sw_amax = 255.0f;                       /* 0x118a04: the clamp of layer A's alpha */
static float sw_x0 = 0.0f;                           /* theme: horizontal offset of the wave */
static float sw_cos[9], sw_sin[9];
static unsigned char sw_env[64][32];                 /* the alpha of the environment map */
static int sw_ready;
static uint64_t sw_last_us;
static float sw_carry;
static float sw_fade;                                /* fades the wave in when it first appears */

#define SW_TWO_PI 6.28318548f

static void sw_osc_reset(void)
{
	for (int i = 0; i < SW_NREC; i++) {
		sw_o[i].base = sw_rec_init[i][0]; sw_o[i].amp = sw_rec_init[i][1];
		sw_o[i].speed = sw_rec_init[i][2]; sw_o[i].freq = sw_rec_init[i][3];
		sw_o[i].phase = 0.0f;
	}
	sw_speed = 1.5f; sw_one_b = 1.0f; sw_amax = 255.0f;
}

static void sw_reset_stock(void) { sw_osc_reset(); sw_x0 = 0.0f; }

static void sw_init(void)
{
	static const float deg[9] = { 0, 9, 16, 25, 36, 49, 64, 80, 90 };
	for (int j = 0; j < 9; j++) { sw_cos[j] = cosf(deg[j] * 0.0174532925f); sw_sin[j] = sinf(deg[j] * 0.0174532925f); }
	for (int v = 0; v < 64; v++)
		for (int u = 0; u < 32; u++)
			sw_env[v][u] = sw_env_q[(v < 32 ? v : 63 - v) * 16 + (u < 16 ? u : 31 - u)];
	sw_osc_reset();
	sw_ready = 1;
}

/* a theme's CXMB patch of paf.prx changes the constants the constructor writes (and three in the update / mesh code) */
static void sw_apply_patches(void)
{
	if (!sw_ready) sw_init();
	sw_osc_reset();
	for (unsigned i = 0; i < sizeof(sw_sites) / sizeof(sw_sites[0]); i++) {
		const SwSite *s = &sw_sites[i];
		float v = s->lo_at ? pp_hilo(PM_PAF, s->hi_at, s->hi, s->lo) : pp_hi(PM_PAF, s->hi_at, s->hi);
		float *f = &sw_o[s->rec].base + s->field;
		if (v == v && v > -1.0e6f && v < 1.0e6f) *f = v;
	}
	float v = pp_hi(PM_PAF, 0x118440, 0x3fc0);
	if (v > 0.0f && v < 100.0f) sw_speed = v;
	v = pp_hi(PM_PAF, 0x1187d0, 0x3f80);
	if (v > -100.0f && v < 100.0f) sw_one_b = v;
	v = pp_hi(PM_PAF, 0x118a04, 0x437f);
	if (v > 0.0f && v < 100000.0f) sw_amax = v;
}

static float sw_eval(const SwOsc *o, float x) { return o->base + o->amp * sinf(o->phase - x * o->freq); }

static void sw_advance(SwOsc *o, float dt)
{
	float p = o->phase + o->speed * dt;
	if (p > SW_TWO_PI) { p -= SW_TWO_PI; while (p > SW_TWO_PI) p -= SW_TWO_PI; }
	if (p < -SW_TWO_PI) { while (p < -SW_TWO_PI) p += SW_TWO_PI; }
	o->phase = p;
}

/* one 1/60 second step of the firmware's per-frame update (0x1183b4) */
static void sw_step(void)
{
	sw_advance(&sw_o[SW_T0], 1.0f / 60.0f);
	float dt = sw_eval(&sw_o[SW_T0], 0.0f) * sw_speed / 60.0f;      /* the "1" of 0x50 is its stock value */
	sw_advance(&sw_o[SW_O2EC], dt); sw_advance(&sw_o[SW_O304], dt); sw_advance(&sw_o[SW_O31C], dt); sw_advance(&sw_o[SW_O2D4], dt);
	for (int k = 0; k < 3; k++) {
		sw_advance(&sw_o[SW_RA0 + k], dt); sw_advance(&sw_o[SW_RS0 + k], dt); sw_advance(&sw_o[SW_RF0 + k], dt);
		sw_o[SW_R0 + k].amp = sw_eval(&sw_o[SW_RA0 + k], 0.0f);
		sw_o[SW_R0 + k].speed = sw_eval(&sw_o[SW_RS0 + k], 0.0f);
		sw_o[SW_R0 + k].freq = sw_eval(&sw_o[SW_RF0 + k], 0.0f);
		sw_advance(&sw_o[SW_R0 + k], dt);
	}
}

static float sw_shape(float t, float omt)
{
	return sw_eval(&sw_o[SW_R0], t) + sw_eval(&sw_o[SW_R1], t) * t + sw_eval(&sw_o[SW_R2], t) * omt;
}

/* the two columns of heights (0x11855c, first half) */
static void sw_heights(float hA[16], float hB[16])
{
	float ripple = sw_one_b + sw_eval(&sw_o[SW_O2D4], 0.0f) * 0.0299072265625f;
	for (int i = 0; i < 16; i++) {
		float t = (float)i * 0.0625f;
		float c = t * 0.8984375f + 0.099609375f;
		float v = 110.0f + 0.5f * sw_shape(t, 1.0f - t);
		v += sw_eval(&sw_o[SW_O304], (float)i) * c + sw_eval(&sw_o[SW_O31C], (float)i);
		hA[i] = v;
		float t2 = t + 0.0498046875f;
		float w = 110.0f + 0.5f * sw_shape(t2, sw_one_b - t2);
		w += sw_eval(&sw_o[SW_O2EC], (float)i) * c;
		w *= ripple;
		hB[i] = w;
	}
}

#define SW_GAIN 0.45f                               /* fitted to the Original theme's own picture (the edge jumps about half as much as colour * alpha says) */
#define SW_DIV 3
#define SW_NU (13 * SW_DIV + 1)
#define SW_NV (6 * SW_DIV + 1)

typedef struct { float x, y, z, a; } SwVert;

/* uniform cubic B-spline weights of the SW_DIV + 1 samples of a segment */
static void sw_basis(float w[SW_DIV + 1][4])
{
	for (int k = 0; k <= SW_DIV; k++) {
		float t = (float)k / (float)SW_DIV, t2 = t * t, t3 = t2 * t;
		w[k][0] = (1 - 3 * t + 3 * t2 - t3) / 6.0f;
		w[k][1] = (4 - 6 * t2 + 3 * t3) / 6.0f;
		w[k][2] = (1 + 3 * t + 3 * t2 - 3 * t3) / 6.0f;
		w[k][3] = t3 / 6.0f;
	}
}

/* a control mesh (9 rows x 16 columns) -> the patch's sample grid, positions and alpha */
static void sw_patch(const float h[16], float alpha_scale, float amax, SwVert g[SW_NV][SW_NU])
{
	float ctl[9][16][4];
	for (int j = 0; j < 9; j++)
		for (int i = 0; i < 16; i++) {
			float cs = sw_cos[j];
			ctl[j][i][0] = ((float)i - 7.5f) * 480.0f / 13.0f;
			ctl[j][i][1] = cs * ((1.0f - cs) * (h[i] - 90.0f) + h[i]);
			ctl[j][i][2] = sw_sin[j] * 90.0f;
			float a = (float)sw_row_alpha[j] * alpha_scale;
			ctl[j][i][3] = a < 0.0f ? 0.0f : (a > amax ? 255.0f : (float)(int)a);       /* above the clamp the firmware saturates to opaque */
		}
	float w[SW_DIV + 1][4];
	sw_basis(w);
	for (int sv = 0; sv < 6; sv++)
		for (int a = 0; a <= SW_DIV; a++) {
			int gj = sv * SW_DIV + a;
			for (int su = 0; su < 13; su++)
				for (int b = 0; b <= SW_DIV; b++) {
					int gi = su * SW_DIV + b;
					float s[4] = { 0, 0, 0, 0 };
					for (int vv = 0; vv < 4; vv++)
						for (int uu = 0; uu < 4; uu++) {
							float wt = w[a][vv] * w[b][uu];
							const float *p = ctl[sv + vv][su + uu];
							s[0] += wt * p[0]; s[1] += wt * p[1]; s[2] += wt * p[2]; s[3] += wt * p[3];
						}
					g[gj][gi].x = s[0]; g[gj][gi].y = s[1]; g[gj][gi].z = s[2]; g[gj][gi].a = s[3];
				}
		}
}

/* alpha of the environment map at (u, v) in 0..1, bilinear */
static float sw_envalpha(float u, float v)
{
	float fx = u * 32.0f - 0.5f, fy = v * 64.0f - 0.5f;
	int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
	float ax = fx - x0, ay = fy - y0;
	float acc = 0.0f;
	for (int dy = 0; dy < 2; dy++)
		for (int dx = 0; dx < 2; dx++) {
			int xx = x0 + dx, yy = y0 + dy;
			xx = xx < 0 ? 0 : (xx > 31 ? 31 : xx);
			yy = yy < 0 ? 0 : (yy > 63 ? 63 : yy);
			acc += (dx ? ax : 1.0f - ax) * (dy ? ay : 1.0f - ay) * (float)sw_env[yy][xx];
		}
	return acc / 255.0f;
}

/* draws one layer additively: colour (0..1 rgb) times the environment map's alpha and the vertex alpha */
static void sw_draw_layer(const float h[16], const float rgb[3], float alpha_scale, float amax, float gain, float x0)
{
	static SwVert g[SW_NV][SW_NU];
	static float lit[SW_NV][SW_NU];
	static vita2d_color_vertex pts[SW_NV][SW_NU];
	sw_patch(h, alpha_scale, amax, g);
	for (int j = 0; j < SW_NV; j++)
		for (int i = 0; i < SW_NU; i++) {
			int i0 = i > 0 ? i - 1 : i, i1 = i < SW_NU - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < SW_NV - 1 ? j + 1 : j;
			float du[3] = { g[j][i1].x - g[j][i0].x, g[j][i1].y - g[j][i0].y, g[j][i1].z - g[j][i0].z };
			float dv[3] = { g[j1][i].x - g[j0][i].x, g[j1][i].y - g[j0][i].y, g[j1][i].z - g[j0][i].z };
			float nx = du[1] * dv[2] - du[2] * dv[1], ny = du[2] * dv[0] - du[0] * dv[2], nz = du[0] * dv[1] - du[1] * dv[0];
			float len = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-9f;
			float ea = sw_envalpha(0.5f - 0.5f * nx / len, 0.5f - 0.5f * ny / len);
			float k = ea * (g[j][i].a / 255.0f) * gain;
			if (k > 1.0f) k = 1.0f;
			lit[j][i] = k;
			vita2d_color_vertex *p = &pts[j][i];
			p->x = 480.0f + (g[j][i].x + x0) * 2.0f;
			p->y = 544.0f - (g[j][i].y + 21.0f) * 2.0f;
			p->z = 0.5f;
			p->color = RGBA8((int)(rgb[0] * k * 255.0f), (int)(rgb[1] * k * 255.0f), (int)(rgb[2] * k * 255.0f), 255);
		}
	int n = (SW_NV - 1) * (SW_NU - 1) * 6;
	vita2d_color_vertex *v = vita2d_pool_memalign(n * sizeof(*v), sizeof(*v));
	if (!v) return;
	int c = 0;
	for (int j = 0; j < SW_NV - 1; j++)
		for (int i = 0; i < SW_NU - 1; i++) {
			const vita2d_color_vertex *a = &pts[j][i], *b = &pts[j][i + 1], *d = &pts[j + 1][i], *e = &pts[j + 1][i + 1];
			v[c++] = *a; v[c++] = *b; v[c++] = *e;
			v[c++] = *a; v[c++] = *e; v[c++] = *d;
		}
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n);
}

/* the colours of the wave for a month (or a special day) from the firmware's table: layer B uses the first colour, layer A the second */
static void sw_draw(int entry, float alpha)
{
	if (!sw_ready) sw_init();
	uint64_t now = sceKernelGetProcessTimeWide();
	if (sw_last_us == 0 || now < sw_last_us) { sw_last_us = now; sw_fade = 0.0f; }
	float dt = (float)(now - sw_last_us) / 1.0e6f;
	sw_last_us = now;
	if (dt > 0.25f) dt = 0.25f;
	sw_carry += dt * 60.0f;
	int steps = (int)sw_carry;
	sw_carry -= (float)steps;
	for (int i = 0; i < steps; i++) sw_step();
	sw_fade += dt / 1.0f;
	if (sw_fade > 1.0f) sw_fade = 1.0f;
	if (entry < 0 || entry > 34) entry = 0;
	const float *c = sw_colors[entry];
	float hA[16], hB[16];
	sw_heights(hA, hB);
	float g = alpha * sw_fade * SW_GAIN;
	if (g <= 0.003f) return;
	vita2d_set_blend_mode_add(1);
	float rgbA[3] = { c[4], c[5], c[6] }, rgbB[3] = { c[0], c[1], c[2] };
	sw_draw_layer(hA, rgbA, c[7], sw_amax, g, sw_x0);                 /* the first mesh of the firmware: colour pair 2 */
	sw_draw_layer(hB, rgbB, c[3], 255.0f, g, sw_x0);                  /* the second one: colour pair 1 */
	vita2d_set_blend_mode_add(0);
}
