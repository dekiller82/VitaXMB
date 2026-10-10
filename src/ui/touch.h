#pragma once

#include <psp2/touch.h>

/* ------------------------------------------------------------------ */
/* Touch screen                                                        */
/* ------------------------------------------------------------------ */
/* The front touch screen is turned into the button presses the rest of the program already understands (a stream of Up / Down /
 * Left / Right / Cross / Circle / Triangle / L / R, one per frame), so every list, panel and page works with a finger exactly as it
 * does with the pad:
 *   drag up or down   scroll the list (one row per row height of finger travel)
 *   drag sideways     change the category on the main bar; in a folder or sub list a swipe to the right goes back
 *   tap               select a row (a tap on the selected row opens it), tap a category icon to go there
 *   press and hold    Options of the row under the finger
 * Panels, dialogs and pages get their own taps (a row of the Options panel, Yes / No, a tap to close). */

enum { TM_NONE, TM_XMB, TM_OPT, TM_DLG, TM_PAGE, TM_PAGE_UPDATE, TM_PLAYER, TM_PICKER, TM_CHOOSER };

typedef struct {
	int mode;                       /* what is on top of the screen: one of TM_* */
	int lay, menu;                  /* TM_XMB: layout (LAY_*) and menu shown */
	int sel, count, depth;          /* the shown menu's selection, row count and folder depth */
	int cat, cat_n;                 /* category shown; slot of that category */
	float pos;                      /* the list's animated position (tap hit tests) */
	int opt_n, opt_sel;             /* TM_OPT */
	float opt_px, opt_y0, opt_row;
	int dlg_sel;                    /* TM_DLG */
	int pk_sel; float pk_top;       /* TM_PICKER */
} TouchCtx;

#define TQ_MAX 48
static unsigned tq[TQ_MAX];
static int tq_n;

static void tq_push(unsigned b) { if (tq_n < TQ_MAX) tq[tq_n++] = b; }
static void tq_clear(void) { tq_n = 0; }
/* rows to move: more than a few queued presses would overshoot after the finger has lifted */
static int tq_room(void) { return tq_n < 6; }

static void tq_steps(int diff)           /* move the selection by diff rows */
{
	for (int k = 0; k < abs(diff) && k < 24; k++) tq_push(diff < 0 ? SCE_CTRL_UP : SCE_CTRL_DOWN);
}

/* y of row j when row `sel` is selected, in the layout the menu is drawn with (the same numbers as draw_column and friends) */
static float touch_row_y(const TouchCtx *c, int j)
{
	float d = (float)(j - c->sel);
	if (c->lay == LAY_GAME) return folder_y(d);
	if (c->lay == LAY_SUB) return sub_y(d, c->menu == M_VIDEOS ? XS_VIDEO : (c->menu == M_TRACKS ? XS_MUSIC : XS_SETTINGS));
	if (c->menu < CAT_COUNT && pt_text_list()) return ITEM_Y + xl_offset(XS_COLUMN, d);
	float y = ITEM_Y + d * 2.0f * xstyles[XS_COLUMN].pitch;
	if (d < 0) y -= 2.0f * xstyles[XS_COLUMN].gap_above * clampf(-d, 0.0f, 1.0f);
	return y;
}

/* the row under a point, or -1 */
static int touch_row_at(const TouchCtx *c, float ty)
{
	int best = -1;
	float bd = 1.0e9f, reach = c->lay == LAY_GAME ? 60.0f : 52.0f;
	for (int j = 0; j < c->count; j++) {
		if (abs(j - c->sel) > 5) continue;
		float dy = fabsf(touch_row_y(c, j) - ty);
		if (dy < bd) { bd = dy; best = j; }
	}
	return bd <= reach ? best : -1;
}

/* the category icon under a point on the main bar: returns its slot, or -1 */
static int touch_cat_at(const TouchCtx *c, float tx)
{
	int best = -1;
	float bd = 1.0e9f;
	for (int i = 0; i < CAT_COUNT; i++) {
		if (cat_hidden[i]) continue;
		float d = (float)(cat_slot(i) - c->cat_n);
		float x = CAT_X + pt_row_dx + CAT_OFFSET(d);
		if (fabsf(x - tx) < bd) { bd = fabsf(x - tx); best = i; }
	}
	return bd <= 80.0f ? best : -1;
}

static void touch_tap(const TouchCtx *c, float tx, float ty)
{
	switch (c->mode) {
	case TM_XMB: {
		if (c->lay == LAY_GAME && c->depth > 0 && tx < 70.0f) { tq_push(SCE_CTRL_CIRCLE); return; }                 /* the arrow at the left edge */
		if (c->lay == LAY_SUB && tx < 215.0f) { tq_push(SCE_CTRL_CIRCLE); return; }                                  /* the parent column */
		if (c->lay == LAY_COLUMN && ty >= 70.0f && ty < 215.0f && !pt_blade_mode && !pt_strip_mode) {
			int cat = touch_cat_at(c, tx);
			if (cat >= 0 && cat != c->cat) {
				int slots = cat_slot(cat) - c->cat_n;
				for (int k = 0; k < abs(slots) && k < 6; k++) tq_push(slots < 0 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT);
			}
			return;
		}
		if (c->lay == LAY_COLUMN && pt_strip_mode) { if (ty > 150.0f && c->count) tq_push(SCE_CTRL_CROSS); return; }   /* only the open item shows */
		int j = touch_row_at(c, ty);
		if (j < 0) return;
		if (j == c->sel) tq_push(SCE_CTRL_CROSS);
		else tq_steps(j - c->sel);
		break; }
	case TM_OPT:
		if (tx >= c->opt_px && c->opt_row > 1.0f) {
			int i = (int)floorf((ty - (c->opt_y0 - c->opt_row / 2.0f)) / c->opt_row);
			if (i >= 0 && i < c->opt_n) { tq_steps(i - c->opt_sel); tq_push(SCE_CTRL_CROSS); }
		} else tq_push(SCE_CTRL_CIRCLE);                                       /* outside the panel closes it */
		break;
	case TM_DLG: {
		int want = -1;
		if (fabsf(tx - 480.0f) <= 100.0f) {
			if (fabsf(ty - 330.0f) <= 28.0f) want = 0;            /* Yes */
			else if (fabsf(ty - 386.0f) <= 28.0f) want = 1;       /* No */
		}
		if (want < 0) break;
		if (want != c->dlg_sel) tq_push(SCE_CTRL_UP);               /* the dialog's two rows swap with either direction */
		tq_push(SCE_CTRL_CROSS);
		break; }
	case TM_PAGE: tq_push(SCE_CTRL_CIRCLE); break;
	case TM_PAGE_UPDATE:                                                     /* Network Update: the bottom-left half is the Cross action */
		tq_push(ty > 470.0f && tx < 480.0f ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE);
		break;
	case TM_PLAYER: tq_push(ty < 50.0f ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS); break;
	case TM_PICKER:
		if (ty < 80.0f) { tq_push(SCE_CTRL_CIRCLE); break; }                         /* the heading saves and closes */
		{
			int i = (int)floorf(c->pk_top + (ty - 112.0f) / 38.0f + 0.5f);
			if (i >= 0 && i < n_all) { tq_steps(i - c->pk_sel); tq_push(SCE_CTRL_CROSS); }
		}
		break;
	case TM_CHOOSER: tq_push(tx < 430.0f ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS); break;
	default: break;
	}
}

/* Reads the touch screen and returns the one virtual button press for this frame. */
static unsigned touch_poll(const TouchCtx *c)
{
	static int started, down, locked, fired;
	static float sx, sy, lx, ly, ax, ay;
	static uint64_t t_down;

	if (!started) {
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		started = 1;
	}
	SceTouchData td;
	memset(&td, 0, sizeof(td));
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 0) td.reportNum = 0;
	int touching = td.reportNum > 0 && c->mode != TM_NONE;
	float x = touching ? td.report[0].x * 0.5f : lx, y = touching ? td.report[0].y * 0.5f : ly;      /* the panel reports 1920 x 1088 */
	uint64_t now = sceKernelGetProcessTimeWide();

	if (c->mode == TM_NONE) { tq_clear(); down = 0; }

	if (touching) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);              /* a touch counts as activity: no auto-sleep while browsing */
		if (!down) {
			down = 1; locked = 0; fired = 0;
			sx = lx = x; sy = ly = y; ax = ay = 0.0f; t_down = now;
		} else {
			float dx = x - lx, dy = y - ly;
			lx = x; ly = y;
			ax += dx; ay += dy;
			if (!locked && (fabsf(x - sx) > 16.0f || fabsf(y - sy) > 16.0f)) locked = fabsf(x - sx) > fabsf(y - sy) ? 2 : 1;
			if (locked == 1 && c->mode != TM_PAGE && c->mode != TM_PAGE_UPDATE && c->mode != TM_DLG && c->mode != TM_PLAYER) {
				float pitch = 40.0f;                                   /* one row of finger travel per row */
				if (c->mode == TM_XMB) pitch = c->lay == LAY_COLUMN ? 100.0f : 80.0f;
				else if (c->mode == TM_PICKER) pitch = 38.0f;
				while (ay <= -pitch) { ay += pitch; if (tq_room()) tq_push(SCE_CTRL_DOWN); }     /* the content follows the finger */
				while (ay >= pitch)  { ay -= pitch; if (tq_room()) tq_push(SCE_CTRL_UP); }
			} else if (locked == 2) {
				if (c->mode == TM_XMB && c->lay == LAY_COLUMN) {
					while (ax <= -140.0f) { ax += 140.0f; if (tq_room()) tq_push(SCE_CTRL_RIGHT); }    /* drag left: the next category */
					while (ax >=  140.0f) { ax -= 140.0f; if (tq_room()) tq_push(SCE_CTRL_LEFT); }
				} else if (c->mode == TM_XMB && !fired && ax >= 120.0f && c->depth > 0) { fired = 1; tq_push(SCE_CTRL_CIRCLE); }    /* swipe right: back */
				else if (c->mode == TM_PLAYER && !fired && fabsf(ax) >= 120.0f) { fired = 1; tq_push(ax < 0 ? SCE_CTRL_RTRIGGER : SCE_CTRL_LTRIGGER); }
			}
			if (!locked && !fired && c->mode == TM_XMB && now - t_down > 650000) {             /* press and hold: Options of that row */
				fired = 1;
				if (!(c->lay == LAY_COLUMN && y < 215.0f)) {
					int j = touch_row_at(c, y);
					if (j >= 0) { tq_steps(j - c->sel); tq_push(SCE_CTRL_TRIANGLE); }
				}
			}
		}
	} else if (down) {
		down = 0;
		if (!locked && !fired && now - t_down < 450000 && c->mode != TM_NONE) touch_tap(c, lx, ly);
	}

	if (tq_n > 0) {
		unsigned b = tq[0];
		memmove(tq, tq + 1, (size_t)(--tq_n) * sizeof(tq[0]));
		return b;
	}
	return 0;
}
