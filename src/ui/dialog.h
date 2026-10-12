#pragma once

/* ---- confirmation dialog ---- */
static void draw_dialog(float t, const char *l1, const char *l2, int sel)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(195 * t)));
	int nl = 0;                                                  /* l2 may hold several lines, split by \n */
	for (const char *c = l2 ? l2 : ""; *c; c++) if (*c == '\n') nl++;
	if (l2 && l2[0]) nl++;
	float cy = nl > 1 ? 200.0f - 22.0f * (nl - 1) : 200.0f;
	float pitch = nl > 1 ? 32.0f : 40.0f;
	ptext_vc(480 - ptext_width(28, l1) / 2.0f, cy, WHITE(a), 28, l1);
	if (nl) {
		char ln[128];
		const char *c = l2;
		for (int k = 0; k < nl && *c; k++) {
			const char *e = strchr(c, '\n');
			size_t n = e ? (size_t)(e - c) : strlen(c);
			if (n >= sizeof(ln)) n = sizeof(ln) - 1;
			memcpy(ln, c, n); ln[n] = 0;
			ptext_vc(480 - ptext_width(24, ln) / 2.0f, cy + 38.0f + k * pitch, WHITE(a * 8 / 10), 24, ln);
			c = e ? e + 1 : c + n;
		}
	}
	int force = sel == 2;                                        /* sel 2: a single "Restart now" button, no way to cancel */
	static const char *opts[2] = { "Yes", "No" };
	if (force) { opts[0] = "Restart now"; sel = 0; }
	for (int i = 0; i < (force ? 1 : 2); i++) {
		float y = 330.0f + i * 56.0f;
		int oa = i == sel ? a : a * 5 / 10;
		float w = ptext_width(28, opts[i]);
		if (i == sel) {
			vita2d_draw_rectangle(480 - 90, y - 22, 180, 44, RGBA8(255, 255, 255, a * 18 / 100));
			draw_rule(480 - 90, 480 + 90, y + 18, a);
		}
		ptext_vc(480 - w / 2.0f, y, WHITE(oa), 28, opts[i]);
	}
	glyph_cross(405, 508, 8, a);
	ptext_vc(424, 508, BTN_LABEL(a), 24, "OK");
	if (force) return;
	glyph_ring(515, 508, 9, a);
	ptext_vc(534, 508, BTN_LABEL(a), 24, "Cancel");
}
