/*
 * rco.h - a small version of the PSP's resource engine (paf), enough to run its screens.
 *
 * An RCO file holds the screens of one plugin as an object tree (pages holding planes, groups, models, lists...)
 * and an animation tree (one page per animation; each holds Fade / Resize / MoveTo / Recolour steps, Delays and
 * Fire events). The firmware plays an animation by running its steps in order: steps start together until a Delay, which
 * moves the clock on; a Fire hands a named event ("native:/system_bg_plane_show") to the plugin's code. This does the same,
 * reading the same files, so a theme's own RCO plays as it was made.
 *
 * Layout read from the files (and checked against RCOMage's definitions):
 *   header: 41 words; [8] model table, [9] image table, [12] object tree, [13] animation tree, [16] labels, [18] events,
 *           [32] image data, [36] model data.
 *   node:   10 words {typeId, label, eHead, eSize, children, next, ...}, then its attribute words, then its children.
 *           typeId low byte is the class (1 page, 2 plane, 4 xmenu, 6 xlist, 9 mlist, 13 text, 14 model, 18 group ...);
 *           in the animation tree 2 MoveTo, 3 Recolour, 4 Rotate, 5 Resize, 6 Fade, 7 Delay, 8 Fire, 9 Lock, 10 Unlock.
 *           next is the byte size of the node with everything below it. Objects with an attribute block have eSize > 0x28;
 *           an animation step has none and its size is next - 0x28.
 *   objects: positions are measured from the screen centre, y up, in PSP pixels (480x272).
 */

#define RCO_MAX_NODES 6000

typedef struct {
	int type, tid;
	const char *label;
	uint32_t pos;
	int parent, child, next, nchild;
	const uint8_t *attr;                    /* the node's attribute words */
	int nattr;
	/* the state of an object as it is drawn */
	float x, y, z, r, g, b, a, w, h, sx, sy, sz;
	float ca;                               /* the colour's own alpha (Recolour); Fade works on a, and both count */
} RcoNode;

typedef struct {
	uint8_t *data;
	size_t size;
	uint32_t H[41];
	RcoNode *nodes;
	int n;
	struct { uint32_t pos; vita2d_texture *tex; } img[64];
	int nimg;
	uint8_t *mdl[16];                       /* models that were stored compressed, unpacked on first use */
} Rco;

static uint32_t rco_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float rco_f(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

/* attribute words of an animation step without a known size (the last of its page) */
static int rco_anim_words(int type)
{
	switch (type) {
	case 2: case 4: case 5: return 7;
	case 3: return 8;
	case 6: return 5;
	case 7: case 9: case 10: return 1;
	case 8: return 2;
	}
	return 0;
}

/* attribute words of an object class (RCOMage's objattribdef-psp.ini), for the last leaf of a level whose size is not stored */
static int rco_obj_words(int type)
{
	static const unsigned char n[27] = { 0, 9, 19, 41, 27, 6, 31, 25, 30, 44, 6, 0, 6, 56, 18, 39, 2, 42, 16, 36, 6, 46, 44, 25, 4, 22, 33 };
	return type >= 0 && type < 27 ? n[type] : 0;
}

static int rco_parse(Rco *r, uint32_t pos, int parent, int count, int in_anim)
{
	int first = -1, prev = -1;
	for (int k = 0; k < count; k++) {
		if (pos + 0x28 > r->size || r->n >= RCO_MAX_NODES) break;
		const uint8_t *w = r->data + pos;
		int idx = r->n++;
		RcoNode *nd = &r->nodes[idx];
		memset(nd, 0, sizeof(*nd));
		uint32_t type_id = rco_u32(w), lab = rco_u32(w + 4), esz = rco_u32(w + 12), nsub = rco_u32(w + 16), next = rco_u32(w + 20);
		nd->type = type_id & 0xFF;
		nd->tid = (type_id >> 8) & 0xFF;
		nd->pos = pos;
		nd->parent = parent;
		nd->child = nd->next = -1;
		nd->label = (lab != 0xFFFFFFFFu && r->H[16] + lab < r->size) ? (const char *)r->data + r->H[16] + lab : "";
		int nattr;
		if (esz > 0x28) nattr = (int)((esz - 0x28) / 4);
		else if (nsub == 0 && next) nattr = (int)((next - 0x28) / 4);
		else nattr = in_anim ? rco_anim_words(nd->type) : rco_obj_words(nd->type);
		if (pos + 0x28 + (uint32_t)nattr * 4 > r->size) nattr = 0;
		nd->attr = r->data + pos + 0x28;
		nd->nattr = nattr;
		nd->nchild = (int)nsub;
		nd->ca = 1.0f;
		if (!in_anim && nattr >= 13) {
			nd->x = rco_f(nd->attr); nd->y = rco_f(nd->attr + 4); nd->z = rco_f(nd->attr + 8);
			nd->r = rco_f(nd->attr + 12); nd->g = rco_f(nd->attr + 16); nd->b = rco_f(nd->attr + 20); nd->a = rco_f(nd->attr + 24);
			nd->w = rco_f(nd->attr + 28); nd->h = rco_f(nd->attr + 32);
			nd->sx = rco_f(nd->attr + 40); nd->sy = rco_f(nd->attr + 44); nd->sz = rco_f(nd->attr + 48);
		}
		if (first < 0) first = idx;
		if (prev >= 0) r->nodes[prev].next = idx;
		prev = idx;
		if (nsub) nd->child = rco_parse(r, pos + 0x28 + (uint32_t)nattr * 4, idx, (int)nsub, in_anim);
		if (!next) break;
		pos += next;
	}
	return first;
}

static void rco_free(Rco *r)
{
	if (!r) return;
	for (int i = 0; i < r->nimg; i++) if (r->img[i].tex) defer_free(r->img[i].tex);        /* the GPU may still be drawing with it */
	for (int i = 0; i < 16; i++) free(r->mdl[i]);
	free(r->nodes);
	free(r->data);
	free(r);
}

/* Takes ownership of data. NULL if it is not an RCO this can read (flat: nothing compressed except per-image zlib). */
static Rco *rco_open(uint8_t *data, size_t size)
{
	if (!data || size < 0xA4 + 0x28 || memcmp(data, "\0PRF", 4) != 0) { free(data); return NULL; }
	if ((rco_u32(data + 12) >> 4) == 2) { free(data); return NULL; }        /* RLZ tables: needs the flat form */
	Rco *r = calloc(1, sizeof(*r));
	if (!r) { free(data); return NULL; }
	r->data = data;
	r->size = size;
	memcpy(r->H, data, sizeof(r->H));
	r->nodes = malloc(sizeof(RcoNode) * RCO_MAX_NODES);
	if (!r->nodes) { rco_free(r); return NULL; }
	if (r->H[12] != 0xFFFFFFFFu && r->H[12] < size) rco_parse(r, r->H[12], -1, 1, 0);
	if (r->H[13] != 0xFFFFFFFFu && r->H[13] < size) rco_parse(r, r->H[13], -1, 1, 1);
	return r;
}

static int rco_find(const Rco *r, const char *label)
{
	for (int i = 0; i < r->n; i++) if (strcmp(r->nodes[i].label, label) == 0) return i;
	return -1;
}

/* the node at a file position (object references are absolute positions) */
static int rco_node_at(const Rco *r, uint32_t pos)
{
	int lo = 0, hi = r->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (r->nodes[mid].pos == pos) return mid;
		if (r->nodes[mid].pos < pos) lo = mid + 1; else hi = mid - 1;
	}
	return -1;
}

/* ---- pictures and models ---- */

static vita2d_texture *rco_image_at(Rco *r, uint32_t pos)
{
	for (int i = 0; i < r->nimg; i++) if (r->img[i].pos == pos) return r->img[i].tex;
	if (r->nimg >= 64 || pos + 0x38 > r->size) return NULL;
	const uint8_t *e = r->data + pos + 0x28;
	uint32_t fc = rco_u32(e), packed = rco_u32(e + 4), off = rco_u32(e + 8), unp = rco_u32(e + 12);
	vita2d_texture *tex = NULL;
	if ((fc & 0xFFFF) == 5 && r->H[32] != 0xFFFFFFFFu && (size_t)r->H[32] + off + packed <= r->size) {
		const uint8_t *src = r->data + r->H[32] + off;
		uint8_t *tmp = NULL;
		uint32_t len = packed;
		if ((fc >> 16) == 1) { tmp = pt_inflate(src, packed, unp); src = tmp; len = unp; }
		if (((fc >> 16) == 0 || tmp) && src) {
			int w, h;
			uint8_t *pix = pt_gim_decode(src, len, &w, &h);
			tex = pt_tex(pix, w, h);
		}
		free(tmp);
	}
	r->img[r->nimg].pos = pos;
	r->img[r->nimg].tex = tex;
	r->nimg++;
	return tex;
}

/* A picture of the image table by its label (e.g. "tex_cross"), or NULL. */
static vita2d_texture *rco_image_by_label(Rco *r, const char *label)
{
	if (r->H[9] == 0xFFFFFFFFu || r->H[9] + 0x28 > r->size) return NULL;
	uint32_t count = rco_u32(r->data + r->H[9] + 16), pos = r->H[9] + 0x28;
	for (uint32_t i = 0; i < count && pos + 0x38 <= r->size; i++) {
		uint32_t lab = rco_u32(r->data + pos + 4), next = rco_u32(r->data + pos + 20);
		if (lab != 0xFFFFFFFFu && r->H[16] + lab < r->size && strcmp((const char *)r->data + r->H[16] + lab, label) == 0)
			return rco_image_at(r, pos);
		pos += next ? next : 0x38;
	}
	return NULL;
}

/* a plane's picture */
static vita2d_texture *rco_node_image(Rco *r, int node)
{
	const RcoNode *n = &r->nodes[node];
	if (n->type != 2 || n->nattr < 18) return NULL;
	uint32_t kind = rco_u32(n->attr + 64), pos = rco_u32(n->attr + 68);
	if ((kind & 0xFFFF) != 0x402 || pos == 0xFFFFFFFFu) return NULL;
	return rco_image_at(r, pos);
}

/* model number n of the file (a GMO), not copied */
static const uint8_t *rco_model_n(Rco *r, int n, size_t *len)
{
	if (r->H[8] == 0xFFFFFFFFu || r->H[36] == 0xFFFFFFFFu || r->H[8] + 0x28 + 0x38 > r->size) return NULL;
	uint32_t count = rco_u32(r->data + r->H[8] + 16), pos = r->H[8] + 0x28;
	if ((uint32_t)n >= count) return NULL;
	for (int i = 0; i < n; i++) {
		uint32_t next = rco_u32(r->data + pos + 20);
		pos += next ? next : 0x38;
	}
	if (pos + 0x38 > r->size) return NULL;
	const uint8_t *e = r->data + pos + 0x28;
	uint32_t fc = rco_u32(e), packed = rco_u32(e + 4), off = rco_u32(e + 8), unp = rco_u32(e + 12);
	if ((size_t)r->H[36] + off + packed > r->size) return NULL;
	if ((fc >> 16) == 1 && n < 16) {                                  /* zlib: unpack once and keep */
		if (!r->mdl[n]) r->mdl[n] = pt_inflate(r->data + r->H[36] + off, packed, unp);
		if (!r->mdl[n]) return NULL;
		*len = unp;
		return r->mdl[n];
	}
	if ((fc >> 16) != 0) return NULL;
	*len = packed;
	return r->data + r->H[36] + off;
}

static const uint8_t *rco_model(Rco *r, size_t *len) { return rco_model_n(r, 0, len); }

/* ---- animation ---- */

enum { RA_MOVE, RA_COLOUR, RA_FADE, RA_SCALE, RA_EVENT, RA_LOOP };

typedef struct { float t; int kind, node, acc; float dur, v[4]; const char *event; } RcoAct;
typedef struct { int kind, node, acc; float t0, dur, from[4], to[4]; } RcoTween;

typedef struct {
	Rco *rco;
	RcoAct *acts;
	int n, next;
	RcoTween tw[48];
	int ntw;
	int done;
	float end;                              /* time of the last step, ms */
	float base;                             /* when the animation (last) started, ms: it loops by firing itself again */
	void (*on_event)(const char *event, float t, void *user);
	void *user;
} RcoPlayer;

static float rco_ease(float p, int acc)
{
	if (p < 0.0f) p = 0.0f;
	if (p > 1.0f) p = 1.0f;
	if (acc == 1) return 1.0f - (1.0f - p) * (1.0f - p);
	if (acc == 2) return p * p;
	if (acc == 3) return p * p * (3.0f - 2.0f * p);
	return p;
}

static void rco_play_free(RcoPlayer *p) { free(p->acts); memset(p, 0, sizeof(*p)); }

/* Prepares the animation page called label: its steps with their start times. */
static int rco_play_start(RcoPlayer *p, Rco *r, const char *label, void (*on_event)(const char *, float, void *), void *user)
{
	memset(p, 0, sizeof(*p));
	int pg = -1;
	for (int i = 0; i < r->n; i++)
		if (r->H[13] != 0xFFFFFFFFu && r->nodes[i].pos > r->H[13] && r->nodes[i].type == 1 && strcmp(r->nodes[i].label, label) == 0) { pg = i; break; }
	if (pg < 0) return 0;
	p->rco = r; p->on_event = on_event; p->user = user;
	p->acts = malloc(sizeof(RcoAct) * (size_t)(r->nodes[pg].nchild + 1));
	if (!p->acts) return 0;
	float t = 0.0f;
	for (int c = r->nodes[pg].child; c >= 0; c = r->nodes[c].next) {
		const RcoNode *s = &r->nodes[c];
		const uint8_t *a = s->attr;
		RcoAct act;
		memset(&act, 0, sizeof(act));
		act.t = t;
		int obj = (s->type >= 2 && s->type <= 6 && s->nattr >= 2) ? rco_node_at(r, rco_u32(a + 4)) : -1;
		act.node = obj;
		switch (s->type) {
		case 7: t += rco_f(a); continue;                                          /* Delay */
		case 6: if (obj < 0 || s->nattr < 5) continue;                              /* Fade: to alpha */
			act.kind = RA_FADE; act.dur = rco_f(a + 8); act.acc = (int)rco_u32(a + 12); act.v[0] = rco_f(a + 16); break;
		case 5: if (obj < 0 || s->nattr < 7) continue;                              /* Resize: to scale */
			act.kind = RA_SCALE; act.dur = rco_f(a + 8); act.acc = (int)rco_u32(a + 12);
			act.v[0] = rco_f(a + 16); act.v[1] = rco_f(a + 20); act.v[2] = rco_f(a + 24); break;
		case 2: if (obj < 0 || s->nattr < 7) continue;                              /* MoveTo */
			act.kind = RA_MOVE; act.dur = rco_f(a + 8); act.acc = (int)rco_u32(a + 12);
			act.v[0] = rco_f(a + 16); act.v[1] = rco_f(a + 20); act.v[2] = rco_f(a + 24); break;
		case 3: if (obj < 0 || s->nattr < 8) continue;                              /* Recolour */
			act.kind = RA_COLOUR; act.dur = rco_f(a + 8); act.acc = (int)rco_u32(a + 12);
			act.v[0] = rco_f(a + 16); act.v[1] = rco_f(a + 20); act.v[2] = rco_f(a + 24); act.v[3] = rco_f(a + 28); break;
		case 8:                                                                     /* Fire: a named event of the plugin, or (kind 0x408) another animation */
			if (s->nattr >= 2 && (rco_u32(a) & 0xFFFF) == 0x408) { act.kind = RA_LOOP; break; }
			if (s->nattr < 2 || r->H[18] == 0xFFFFFFFFu || r->H[18] + rco_u32(a + 4) >= r->size) continue;
			act.kind = RA_EVENT; act.event = (const char *)r->data + r->H[18] + rco_u32(a + 4); break;
		default: continue;                                                          /* Lock, Unlock, Rotate */
		}
		p->acts[p->n++] = act;
	}
	p->end = t;
	return 1;
}

static void rco_tween_apply(Rco *r, const RcoTween *w, const float *v)
{
	RcoNode *n = &r->nodes[w->node];
	switch (w->kind) {
	case RA_FADE: n->a = v[0]; break;
	case RA_SCALE: n->sx = v[0]; n->sy = v[1]; n->sz = v[2]; break;
	case RA_MOVE: n->x = v[0]; n->y = v[1]; n->z = v[2]; break;
	case RA_COLOUR: n->r = v[0]; n->g = v[1]; n->b = v[2]; n->ca = v[3]; break;
	}
}

/* Runs the animation up to time t (milliseconds since it started). */
static void rco_tweens_at(RcoPlayer *p, float t)
{
	Rco *r = p->rco;
	for (int i = 0; i < p->ntw; i++) {
		const RcoTween *w = &p->tw[i];
		float e = rco_ease((t - w->t0) / w->dur, w->acc), v[4];
		for (int k = 0; k < 4; k++) v[k] = w->from[k] + (w->to[k] - w->from[k]) * e;
		rco_tween_apply(r, w, v);
		if (t >= w->t0 + w->dur) { p->tw[i--] = p->tw[--p->ntw]; }
	}
}

static void rco_play_step(RcoPlayer *p, float t)
{
	if (!p->rco || p->done) return;
	Rco *r = p->rco;
	t -= p->base;
	while (p->next < p->n && p->acts[p->next].t <= t) {
		const RcoAct *a = &p->acts[p->next++];
		rco_tweens_at(p, a->t);
		if (a->kind == RA_LOOP) {
			if (a->t <= 0.0f) { p->done = 1; return; }                 /* an animation that only restarts itself */
			p->base += a->t; t -= a->t; p->next = 0; p->ntw = 0;
			continue;
		}
		if (a->kind == RA_EVENT) { if (p->on_event) p->on_event(a->event, a->t, p->user); continue; }
		RcoTween w;
		memset(&w, 0, sizeof(w));
		w.kind = a->kind; w.node = a->node; w.acc = a->acc; w.t0 = a->t; w.dur = a->dur;
		const RcoNode *n = &r->nodes[a->node];
		switch (a->kind) {
		case RA_FADE: w.from[0] = n->a; break;
		case RA_SCALE: w.from[0] = n->sx; w.from[1] = n->sy; w.from[2] = n->sz; break;
		case RA_MOVE: w.from[0] = n->x; w.from[1] = n->y; w.from[2] = n->z; break;
		case RA_COLOUR: w.from[0] = n->r; w.from[1] = n->g; w.from[2] = n->b; w.from[3] = n->ca; break;
		}
		memcpy(w.to, a->v, sizeof(w.to));
		if (w.dur <= 0.0f) { rco_tween_apply(r, &w, w.to); continue; }
		for (int i = 0; i < p->ntw; i++)                                          /* a newer step on the same property replaces the old one */
			if (p->tw[i].node == w.node && p->tw[i].kind == w.kind) { p->tw[i] = w; w.dur = -1.0f; break; }
		if (w.dur > 0.0f && p->ntw < 48) p->tw[p->ntw++] = w;
	}
	rco_tweens_at(p, t);
	if (p->next >= p->n && p->ntw == 0) p->done = 1;
}

/* ---- drawing ---- */

/* A plane: centred where the object says unless it is one of the full-width streaks, whose position is its left edge. */
static void rco_draw_plane(Rco *r, int node, float extra_alpha, int additive)
{
	RcoNode *n = &r->nodes[node];
	float a = n->a * n->ca * extra_alpha;
	if (a <= 0.003f) return;
	if (a > 1.0f) a = 1.0f;
	vita2d_texture *tex = rco_node_image(r, node);
	if (!tex) return;
	float tw = (float)vita2d_texture_get_width(tex), th = (float)vita2d_texture_get_height(tex);
	float w = n->w > 0.0f ? n->w : tw, h = n->h > 0.0f ? n->h : th;
	float cx = (n->w > 0.0f && fabsf(n->x) >= w / 4.0f) ? n->x + w / 2.0f : n->x;
	float dw = w * n->sx * 2.0f, dh = h * n->sy * 2.0f;
	float px = (240.0f + cx) * 2.0f - dw / 2.0f, py = (136.0f - n->y) * 2.0f - dh / 2.0f;
	int cr = (int)(255.0f * fminf(fmaxf(n->r, 0.0f), 1.0f)), cg = (int)(255.0f * fminf(fmaxf(n->g, 0.0f), 1.0f)), cb = (int)(255.0f * fminf(fmaxf(n->b, 0.0f), 1.0f));
	if (additive) vita2d_set_blend_mode_add(1);                  /* light effects add to what is behind them */
	vita2d_draw_texture_tint_scale(tex, px, py, dw / tw, dh / th, RGBA8(cr, cg, cb, (int)(255.0f * a)));
	if (additive) vita2d_set_blend_mode_add(0);
}
