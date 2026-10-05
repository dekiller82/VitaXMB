#pragma once

/*
 * bgscene.h - the system background as the theme describes it (system_plugin_bg.rco).
 *
 * The file holds the wave models (default_theme_model, or several: Jumbled has default_theme_model and model0..4) and the
 * animations that fade and recolour them (anim_waves cross-fades five models, anim_wave cycles the colour of the main one...).
 * The scene loads every model, plays those animations in a loop, and draws each model with the colour and the
 * transparency the animation leaves it with. A theme without such a file, or with a single plain model, keeps the plain wave.
 */

#define BGS_MAX 8

typedef struct {
	Rco *r;
	WaveModel m[BGS_MAX];
	int node[BGS_MAX];
	int n;
	RcoPlayer pl[3];
	int npl;
	uint64_t t0;
	int ok;
} BgScene;

static BgScene bgs;

static void bgs_free(void)
{
	for (int i = 0; i < bgs.npl; i++) rco_play_free(&bgs.pl[i]);
	for (int i = 0; i < bgs.n; i++) wv_free(&bgs.m[i]);
	if (bgs.r) rco_free(bgs.r);
	memset(&bgs, 0, sizeof(bgs));
}

/* Loads the active theme's background scene; call when the theme changes. Only used when the file brings more than the plain wave. */
static void bgs_reload(void)
{
	bgs_free();
	if (pt_active < 0) return;
	size_t n = 0;
	uint8_t *d = pt_active_file("/vsh/resource/system_plugin_bg.rco", &n);
	if (!d) return;
	Rco *r = rco_open(d, n);
	if (!r) return;                                           /* (a file this reader cannot use leaves the default wave) */
	bgs.r = r;
	int models = 0;
	for (int i = 0; i < r->n && bgs.n < BGS_MAX; i++) {
		if (r->H[13] != 0xFFFFFFFFu && r->nodes[i].pos >= r->H[13]) break;              /* the object tree only */
		if (r->nodes[i].type != 14) continue;
		size_t len = 0;
		const uint8_t *g = rco_model_n(r, models++, &len);
		trace("bgscene: node '%s' model %d len %u data %s\n", r->nodes[i].label, models - 1, (unsigned)len, g ? "yes" : "no");
		if (!g) continue;
		wv_install(&bgs.m[bgs.n], g, len, NULL, 0);
		if (!bgs.m[bgs.n].ok) { trace("  parse failed\n"); continue; }
		bgs.node[bgs.n++] = i;
	}
	static const char *anims[] = { "anim_waves", "anim_wave" };
	for (int i = 0; i < 2; i++)
		if (rco_play_start(&bgs.pl[bgs.npl], r, anims[i], NULL, NULL)) bgs.npl++;
	/* worth running only for what the plain wave cannot do: several models, or an animation on one */
	/* the theme's file replaces the default wave: its first model is the plain wave, and without one there is none */
	if (bgs.n >= 1) {
		size_t l0 = 0;
		const uint8_t *g0 = NULL;
		for (int i = 0, k = 0; i < r->n; i++) {
			if (r->H[13] != 0xFFFFFFFFu && r->nodes[i].pos >= r->H[13]) break;
			if (r->nodes[i].type != 14) continue;
			if (k++ == 0) { g0 = rco_model_n(r, 0, &l0); break; }
		}
		if (g0) wv_install(&wave_m, g0, l0, NULL, 0);
		if (!wave_m.ok) wave_off = 1;
	} else {
		wave_off = 1;
	}
	trace("bgscene: %d models, %d animations, nodes %d\n", bgs.n, bgs.npl, r->n);
	for (int i = 0; i < bgs.n; i++) {
		const RcoNode *nd = &r->nodes[bgs.node[i]];
		trace("  model %d '%s' a=%.2f ca=%.2f rgb=%.2f/%.2f/%.2f nattr=%d frames=%.0f\n", i, nd->label, nd->a, nd->ca, nd->r, nd->g, nd->b, nd->nattr, bgs.m[i].end);
	}
	bgs.ok = bgs.n > 1 || (bgs.n == 1 && bgs.npl > 0);
	if (!bgs.ok) bgs_free();
	else bgs.t0 = sceKernelGetProcessTimeWide();
}

static void bgs_draw(float alpha)
{
	if (!bgs.ok) return;
	float ms = (float)(sceKernelGetProcessTimeWide() - bgs.t0) / 1000.0f;
	for (int i = 0; i < bgs.npl; i++) rco_play_step(&bgs.pl[i], ms);
	for (int i = 0; i < bgs.n; i++) {
		const RcoNode *nd = &bgs.r->nodes[bgs.node[i]];
		float a = nd->a * nd->ca * alpha;
		if (a <= 0.003f) continue;
		if (a > 1.0f) a = 1.0f;
		unsigned char col[3];
		col[0] = (unsigned char)(255.0f * fminf(fmaxf(nd->r, 0.0f), 1.0f));
		col[1] = (unsigned char)(255.0f * fminf(fmaxf(nd->g, 0.0f), 1.0f));
		col[2] = (unsigned char)(255.0f * fminf(fmaxf(nd->b, 0.0f), 1.0f));
		const WaveModel *m = &bgs.m[i];
		wv_draw(m, fmodf(ms / 1000.0f * m->fps, m->end), col, a, 1);
	}
}
