#pragma once

/*
 * menuscale.h - how big a theme draws the top menu's icons.
 *
 * The menu (the xmenu object of topmenu_plugin.rco) has a size of its own, and a theme may change it, usually with an
 * animation that runs as the menu appears (Jumbled starts it at 50 and anim_menu settles it at 0.9). Every icon in the
 * menu is drawn at that scale. This reads the xmenu, runs its animation to the end, and keeps the size it ends at.
 */

static float pt_menu_scale = 1.0f;
static int pt_player_col_set;                 /* the music player's elapsed-time colour, when the theme sets one (music_player_plugin.rco, text AV) */
static unsigned char pt_player_col[3];

static void player_style_reload(void)
{
	pt_player_col_set = 0;
	if (pt_active < 0) return;
	size_t n = 0;
	uint8_t *d = pt_active_file("/vsh/resource/music_player_plugin.rco", &n);
	if (!d) return;
	Rco *r = rco_open(d, n);
	if (!r) return;
	int t = rco_find(r, "AV");                                       /* the elapsed time */
	if (t >= 0 && r->nodes[t].type == 13 && r->nodes[t].nattr >= 26) {
		const uint8_t *a = r->nodes[t].attr;
		float cr = rco_f(a + 92), cg = rco_f(a + 96), cb = rco_f(a + 100);          /* topRed, topGreen, topBlue */
		pt_player_col[0] = (unsigned char)(255.0f * fminf(fmaxf(cr, 0.0f), 1.0f));
		pt_player_col[1] = (unsigned char)(255.0f * fminf(fmaxf(cg, 0.0f), 1.0f));
		pt_player_col[2] = (unsigned char)(255.0f * fminf(fmaxf(cb, 0.0f), 1.0f));
		pt_player_col_set = 1;
	}
	rco_free(r);
}

/* A turn the theme gives the XMB (Clear XMB Black: XMB_RT rotates the plane around the bar): a Rotate step that takes no time and
 * whose object holds the menu. Sets up xform.h. */
static void xmb_turn_reload(Rco *r, int xm)
{
	xf_config(0, 0, 0, 0, 0, 0);
	if (xm < 0 || r->H[13] == 0xFFFFFFFFu) return;
	for (int i = 0; i < r->n; i++) {
		const RcoNode *pg = &r->nodes[i];
		if (pg->pos <= r->H[13] || pg->type != 1) continue;
		for (int c = pg->child; c >= 0; c = r->nodes[c].next) {
			const RcoNode *s = &r->nodes[c];
			if (s->type != 4 || s->nattr < 7) continue;
			const uint8_t *a = s->attr;
			if (rco_f(a + 8) > 0.5f) continue;                                    /* only a fixed turn */
			int tn = rco_node_at(r, rco_u32(a + 4));
			if (tn < 0) continue;
			int anc = 0;
			for (int p = r->nodes[xm].parent; p >= 0; p = r->nodes[p].parent) if (p == tn) anc = 1;
			if (!anc) continue;
			float rx = rco_f(a + 16), ry = rco_f(a + 20), rz = rco_f(a + 24);
			if (fabsf(rx) + fabsf(ry) + fabsf(rz) < 0.01f || fabsf(rx) > 1.5f || fabsf(ry) > 1.5f || fabsf(rz) > 3.2f) continue;
			xf_config(1, rx, ry, rz, r->nodes[tn].x, r->nodes[tn].y);
			trace("xmb turn: %.3f %.3f %.3f at %.1f,%.1f\n", rx, ry, rz, r->nodes[tn].x, r->nodes[tn].y);
			return;
		}
	}
}

static void menu_scale_reload(void)
{
	player_style_reload();
	pt_menu_scale = 1.0f;
	xf_config(0, 0, 0, 0, 0, 0);
	if (pt_active < 0) return;
	size_t n = 0;
	uint8_t *d = pt_active_file("/vsh/resource/topmenu_plugin.rco", &n);
	if (!d) return;
	Rco *r = rco_open(d, n);
	if (!r) return;
	int xm = -1;
	for (int i = 0; i < r->n; i++) {
		if (r->H[13] != 0xFFFFFFFFu && r->nodes[i].pos >= r->H[13]) break;
		if (r->nodes[i].type == 4) { xm = i; break; }
	}
	xmb_turn_reload(r, xm);
	if (xm >= 0) {
		RcoPlayer pl;
		if (rco_play_start(&pl, r, "anim_menu", NULL, NULL)) {
			rco_play_step(&pl, 1.0e7f);                                 /* to the end */
			rco_play_free(&pl);
		}
		float s = r->nodes[xm].sx;
		if (s > 0.2f && s < 3.0f) pt_menu_scale = s;
	}
	rco_free(r);
}
