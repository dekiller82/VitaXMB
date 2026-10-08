#pragma once

/* ------------------------------------------------------------------ */
/* Settings-style sub lists, button glyphs, info pages, dialogs, panel */
/* ------------------------------------------------------------------ */

/* y of the row d places from the selected one in a Settings-style list: the engine's list style for that page (paf.h) */
static float sub_y(float d, int style)
{
	return ITEM_Y + xl_offset(style, d);
}

/* The 2px rule under a selected title: grey-white on the left, white on the right. */
static void draw_rule(float x0, float x1, float y, int la)
{
	if (la <= 2) return;
	vita2d_texture *ln = res_theme("system_plugin", "tex_line");        /* a theme's own separator line */
	if (!ln) ln = res_theme("savedata_plugin", "tex_line");
	if (ln) {
		vita2d_draw_texture_tint_scale(ln, x0, y - 2.0f, (x1 - x0) / (float)vita2d_texture_get_width(ln), 2.0f, WHITE(la));
		return;
	}
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (!v) return;
	unsigned int cl = RGBA8(208, 203, 192, la), cr = RGBA8(253, 247, 238, la);
	v[0] = (vita2d_color_vertex){ x0, y, 0.5f, cl };
	v[1] = (vita2d_color_vertex){ x1, y, 0.5f, cr };
	v[2] = (vita2d_color_vertex){ x0, y + 2, 0.5f, cl };
	v[3] = (vita2d_color_vertex){ x1, y + 2, 0.5f, cr };
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
	vita2d_draw_rectangle(x0, y + 2, x1 - x0, 2, RGBA8(0, 0, 0, la * 12 / 100));
}

static void draw_glow(vita2d_texture *tex, float cx, float cy, float size, float strength)
{
	vita2d_texture *gl = tex ? res_extra(tex, 0) : NULL;           /* the theme's own focus picture, else the blurred outline */
	if (!gl && tex) gl = glow_for(tex);
	if (!gl) return;
	float ew, eh;
	icon_dims(tex, size, &ew, &eh);
	float sc = ew / vita2d_texture_get_width(tex);
	float gw = vita2d_texture_get_width(gl) * sc, gh = vita2d_texture_get_height(gl) * sc;
	vita2d_draw_texture_tint_scale(gl, cx - gw / 2, cy - gh / 2, sc, sc, WHITE((int)(255 * strength)));
}

static void draw_track_row(const Item *it, int number, float y, float xoff, float sel, int a);

/* A theme that draws no icon beside the Settings rows (its tex_sysconf_icon is blank) and keeps the category bar as a strip along the
 * bottom (Euphoria) shows the rows as plain centred text, a bar behind the selected one. */
static int pt_text_list(void)
{
	if (pt_active < 0 || !pt_strip_mode) return 0;
	vita2d_texture *t = res_theme("sysconf_plugin", "tex_sysconf_icon");
	return t && vita2d_texture_get_width(t) <= 2;
}

static void draw_text_rows(int m, float xoff, float amul)
{
	const Menu *mn = &menus[m];
	const float cx = 480.0f + xoff;
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -5.2f || d > 5.2f) continue;
		const Item *it = &mn->items[j];
		float y = ITEM_Y + xl_offset(XS_COLUMN, d), emph = it->glow;      /* the column's XList style (a theme like Euphoria patches it to 20 / 20: 40 px rows, the rows above pushed up by one more) */
		float fade = d < 0 ? clampf(1.0f + d * 0.22f, 0.0f, 1.0f) : clampf(1.0f - d * 0.08f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.85f, 1.0f, emph));
		if (a <= 4) continue;
		int value = it->kind == KIND_VALUE && it->sub[0];
		float tw = ptext_width(26, it->title), vw = value ? ptext_width(PT_SUB(22), it->sub) : 0.0f;
		float total = tw + (value ? 28.0f + vw : 0.0f), x0 = cx - total / 2.0f;
		if (pt_focus && emph > 0.02f) {
			float fw = total + 96.0f, fh = vita2d_texture_get_height(pt_focus) * 1.9f;          /* the bar reaches past the text, arrows at its ends */
			if (fw < 200.0f) fw = 200.0f;
			vita2d_draw_texture_tint_scale(pt_focus, cx - fw / 2.0f, y - fh / 2.0f, fw / vita2d_texture_get_width(pt_focus), 1.9f, WHITE((int)(a * emph)));
		}
		ptext_vc(x0, y + 2, WHITE(a), 26, it->title);
		if (value) ptext_vc(x0 + tw + 28.0f, y + 2, WHITE(a * 9 / 10), PT_SUB(22), it->sub);
	}
}

/* The child list of a Settings-style page: wrench badge, title, value at the right. */
static void draw_sub_list(int m, float xoff, float amul)
{
	if (amul <= 0.01f) return;
	if (pt_text_list()) { draw_text_rows(m, xoff, amul); return; }
	Menu *mn = &menus[m];
	for (int j = 0; j < mn->count; j++) {
		float d = j - mn->pos;
		if (d < -3.2f || d > 4.2f) continue;
		float y = sub_y(d, m == M_VIDEOS ? XS_VIDEO : (m == M_TRACKS ? XS_MUSIC : XS_SETTINGS));
		const Item *it = &mn->items[j];
		float sel = it->glow;                     /* emphasis follows the selection, not the slide */
		float fade = d < 0 ? clampf(1.0f + d * 0.28f, 0.0f, 1.0f) : clampf(1.0f - d * 0.12f, 0.0f, 1.0f);
		int a = (int)(255 * fade * amul * lerpf(0.42f, 1.0f, sel));
		if (a <= 4) continue;

		if (m == M_TRACKS && it->kind == KIND_TRACK) { draw_track_row(it, j + 1, y, xoff, sel, a); continue; }
		float ix = 262.0f + xoff, tx = 303.0f + xoff;
		vita2d_texture *tex = pt_swap((m == M_VIDEOS) ? it->stock : tex_badge);
		float isz = (m == M_VIDEOS) ? 84.0f : 64.0f;
		if (tex && it->glow > 0.02f) draw_glow(tex, ix, y, isz, it->glow * 0.95f * (a / 255.0f));
		draw_icon(tex, ix, y, isz, a);

		int two_line = it->sub[0] && it->kind != KIND_VALUE;
		float maxw = (it->kind == KIND_VALUE ? 745.0f : 940.0f) - 303.0f - 14.0f;
		if (two_line) {
			ptext_vc_fit(tx - text_bearing(28, it->title), y - 20, WHITE(a), 28, it->title, maxw);
			ptext_vc_fit(tx - text_bearing(PT_SUB(22), it->sub), y + 22, WHITE(a), PT_SUB(22), it->sub, maxw);
			if (sel > 0.3f) draw_rule(tx - 1, 948, y, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		} else {
			ptext_vc_fit(tx - text_bearing(28, it->title), y + 3, WHITE(a), 28, it->title, maxw);
			if (it->kind == KIND_VALUE)
				ptext_vc_fit(760.0f + xoff, y + 7, WHITE(a * 9 / 10), 24, it->sub, 180.0f);
			if (sel > 0.3f) draw_rule(tx - 1, 948, y + 22, (int)(a * clampf(sel * 1.4f - 0.2f, 0.0f, 1.0f)));
		}
	}
}
