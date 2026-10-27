#pragma once

/* ---- the sort wizard's screen: tick the kinds of app and the retro systems that get sorted, and say where the retro games go ---- */
static int sw_open, sw_sel;
static float sw_t, sw_top;

#define SW_ROW_Y0 130.0f
#define SW_ROW_H  42.0f
#define SW_VIS    7

static int sw_ticked_count(void)
{
	int c = 0;
	for (int k = 0; k < SW_KINDS; k++) if (sw_on[k]) c += sw_count[k];
	for (int s = 0; s < n_retro_sys; s++) if (sw_sys_on[s]) c += sw_sys_count[s];
	return c;
}

static void sw_start(void)
{
	sw_scan();
	sw_sel = 0;
	sw_top = 0.0f;
	for (int k = 0; k < SW_KINDS; k++) sw_on[k] = sw_count[k] > 0;
	for (int s = 0; s < RETRO_SYS_MAX; s++) sw_sys_on[s] = sw_sys_count[s] > 0;
	sw_open = 1;
}

/* Cross on a row, or left / right on the layout row */
static void sw_toggle(int dir)
{
	SwRow *r = &sw_row[sw_sel];
	if (r->type == SR_KIND) sw_on[r->idx] = !sw_on[r->idx];
	else if (r->type == SR_SYS) sw_sys_on[r->idx] = !sw_sys_on[r->idx];
	else sw_layout = (sw_layout + dir + SL_COUNT) % SL_COUNT;
}

static void sw_all_none(void)
{
	int all = 1;
	for (int k = 0; k < SW_KINDS; k++) if (sw_count[k] && !sw_on[k]) all = 0;
	for (int s = 0; s < n_retro_sys; s++) if (sw_sys_count[s] && !sw_sys_on[s]) all = 0;
	for (int k = 0; k < SW_KINDS; k++) sw_on[k] = !all;
	for (int s = 0; s < RETRO_SYS_MAX; s++) sw_sys_on[s] = !all;
}

static void sw_checkbox(float bx, float by, int on, int sel, int ra)
{
	unsigned int bc = WHITE(ra * (sel ? 100 : 70) / 100);
	vita2d_texture *tbox = res_theme("system_plugin", "tex_box"), *tchk = res_theme("system_plugin", "tex_check");     /* a theme's own check box */
	if (tbox && tchk) {
		float s = 20.0f / (float)vita2d_texture_get_width(tbox);
		vita2d_draw_texture_tint_scale(tbox, bx, by, s, s, bc);
		if (on) vita2d_draw_texture_tint_scale(tchk, bx + 1, by - 2, s * 1.2f, s * 1.2f, WHITE(ra));
	} else {
		vita2d_draw_rectangle(bx, by, 20, 2, bc);
		vita2d_draw_rectangle(bx, by + 18, 20, 2, bc);
		vita2d_draw_rectangle(bx, by, 2, 20, bc);
		vita2d_draw_rectangle(bx + 18, by, 2, 20, bc);
		if (on) vita2d_draw_rectangle(bx + 5, by + 5, 10, 10, WHITE(ra));
	}
}

static void draw_sortwiz(float t)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(225 * t)));
	ptext_vc_fit(60, 46, WHITE(a), 28, T("Sort Apps into Columns"), 600);
	char cnt[48];
	snprintf(cnt, sizeof(cnt), T("%d to sort"), sw_ticked_count());
	ptext_vc(900 - ptext_width(22, cnt), 46, WHITE(a * 7 / 10), 22, cnt);
	vita2d_draw_rectangle(50, 76, 860, 2, WHITE(a * 30 / 100));
	ptext_vc_fit(60, 100, WHITE(a * 6 / 10), 20, T("Tick what VitaXMB should sort. Apps you placed yourself stay where they are."), 840);

	float want = sw_sel - 3.0f, maxtop = sw_nrows - (float)SW_VIS;
	if (want > maxtop) want = maxtop;
	if (want < 0.0f) want = 0.0f;
	sw_top = lerpf(sw_top, want, 0.3f);
	for (int i = 0; i < sw_nrows; i++) {
		float y = SW_ROW_Y0 + (i - sw_top) * SW_ROW_H;
		if (y < SW_ROW_Y0 - 26.0f || y > SW_ROW_Y0 + SW_VIS * SW_ROW_H) continue;
		const SwRow *r = &sw_row[i];
		int sel = i == sw_sel;
		int cnt_n = r->type == SR_KIND ? sw_count[r->idx] : (r->type == SR_SYS ? sw_sys_count[r->idx] : 1);
		int ra = cnt_n == 0 ? a * 55 / 100 : a;
		if (y < SW_ROW_Y0 - 2.0f) ra = ra * (int)(100 - (SW_ROW_Y0 - y) * 4) / 100;
		if (y > SW_ROW_Y0 + (SW_VIS - 1) * SW_ROW_H + 8.0f) ra = ra * (int)(100 - (y - (SW_ROW_Y0 + (SW_VIS - 1) * SW_ROW_H + 8.0f)) * 3) / 100;
		if (ra <= 3) continue;
		if (sel) vita2d_draw_rectangle(50, y - 18, 860, 36, WHITE(ra * 20 / 100));
		int ta = ra * (sel ? 100 : 78) / 100;
		if (r->type == SR_LAYOUT) {
			ptext_vc_fit(70, y, WHITE(ta), 24, T("Retro games go to"), 380);
			char v[48];
			snprintf(v, sizeof(v), "< %s >", T(sl_label[sw_layout]));
			ptext_vc_fit(890 - ptext_width(22, v), y, WHITE(ra * (sel ? 100 : 85) / 100), 22, v, 440);
			continue;
		}
		int on = r->type == SR_KIND ? sw_on[r->idx] : sw_sys_on[r->idx];
		float ind = r->type == SR_SYS ? 24.0f : 0.0f;
		sw_checkbox(70 + ind, y - 10, on, sel, ra);
		const char *label = r->type == SR_KIND ? T(sw_label[r->idx]) : retro_sys[r->idx].label;
		ptext_vc_fit(110 + ind, y, WHITE(ta), 24, label, 420);
		char n[40];
		if (cnt_n == 0) snprintf(n, sizeof(n), "%s", T("none"));
		else if (r->type == SR_SYS) snprintf(n, sizeof(n), T(cnt_n == 1 ? "%d game" : "%d games"), cnt_n);
		else snprintf(n, sizeof(n), T(cnt_n == 1 ? "%d app" : "%d apps"), cnt_n);
		ptext_vc(890 - ptext_width(20, n), y, WHITE(ra * 6 / 10), 20, n);
	}

	/* a line about the selected row */
	{
		char hint[200] = "";
		const SwRow *r = &sw_row[sw_sel];
		if (r->type == SR_KIND) snprintf(hint, sizeof(hint), "%s", T(sw_hint[r->idx]));
		else if (r->type == SR_SYS) snprintf(hint, sizeof(hint), "%s", retro_sys[r->idx].db);
		else {
			snprintf(hint, sizeof(hint), "%s", T(sl_hint[sw_layout]));
			if (!psp_exists("ux0:app/RETROVITA") && !psp_exists("ux0:app/EMU4VPLUS")) snprintf(hint, sizeof(hint), "%s", T("RetroArch and Emu4Vita+ are not installed: these games cannot start yet."));
			else if (retro_dropped) snprintf(hint, sizeof(hint), T("%d games are over the limit and are not listed."), retro_dropped);
		}
		ptext_vc_fit(70, 462, WHITE(a * 5 / 10), 18, hint, 820);
	}

	const SwRow *cur = &sw_row[sw_sel];
	glyph_cross(110, 508, 8, a);
	ptext_vc(129, 508, BTN_LABEL(a), 24, cur->type == SR_LAYOUT ? T("Change") : T("Select"));
	glyph_square(290, 508, 8, a);
	ptext_vc(309, 508, BTN_LABEL(a), 24, T("All / None"));
	glyph_triangle(500, 508, 9, a);
	ptext_vc(519, 508, BTN_LABEL(a), 24, T("Sort"));
	glyph_ring(760, 508, 9, a);
	ptext_vc(779, 508, BTN_LABEL(a), 24, T("Cancel"));
}
