#pragma once

/* ---- confirmation dialog ---- */
static void draw_dialog(float t, const char *l1, const char *l2, int sel)
{
	int a = (int)(255 * t);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(195 * t)));
	float cy = 200.0f;
	ptext_vc(480 - ptext_width(28, l1) / 2.0f, cy, WHITE(a), 28, l1);
	if (l2 && l2[0]) ptext_vc(480 - ptext_width(24, l2) / 2.0f, cy + 40, WHITE(a * 8 / 10), 24, l2);
	static const char *opts[2] = { "Yes", "No" };
	for (int i = 0; i < 2; i++) {
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
	glyph_ring(515, 508, 9, a);
	ptext_vc(534, 508, BTN_LABEL(a), 24, "Cancel");
}
