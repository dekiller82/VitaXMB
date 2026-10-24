#pragma once

/* ------------------------------------------------------------------------------------------------------------------ */
/* The video player screen (BETA): the picture, subtitles and the PSP's control panel.                                  */
/*                                                                                                                      */
/* Layout: a manual screenshot of the PSP video player read with the firmware's msvideo_main_plugin.rco, in Vita pixels: */
/* the panel of icons in three left-aligned rows (y 120, 184, 246) with the name of the selected one under it; the      */
/* "Display" status (a strip across the top with the film icon, the title and the clock, and along the bottom the       */
/* state icon (52, 513), the repeat icon, the time line 285 .. 950 (white, blue where played), a chapter box and the     */
/* time box at y 456 .. 491). Behaviour: the PSP manual's "Control panel" page (Display toggles the status, the search  */
/* icons cycle three speeds with Cross, Previous returns to the beginning first, Clear resets repeat / A-B / shuffle,   */
/* the screen modes are Normal, Zoom, Full Screen, Original and 4:3). The pictures are the PSP's own icons traced to    */
/* the Vita's resolution (tools/make_native_video.py); the strip is the firmware's tex_option_bg (white, 60 %), the      */
/* boxes its moji_bg (dark navy, 51 %). The engines are media/video.h and media/avp.h.                                  */
/* ------------------------------------------------------------------------------------------------------------------ */

enum { VR_OFF, VR_ONE, VR_ALL };                 /* repeat: none, this video (title), all videos (folder) */
enum { VU_PLAY, VU_PANEL, VU_LIST, VU_JUMP, VU_HELP };         /* what has the buttons */
/* the panel's items in the order of its three rows; an item the video cannot use is not shown */
enum { VI_AUDIO, VI_SUB, VI_GOTO, VI_SCREEN, VI_DISPLAY, VI_HELP,
       VI_PREV, VI_NEXT, VI_REV, VI_FWD, VI_SLOW, VI_STEP, VI_PLAY, VI_PLUS, VI_MINUS, VI_PAUSE, VI_STOP,
       VI_AB, VI_REPEAT, VI_SHUF, VI_CLEAR, VI_COUNT };

static const unsigned char vui_row_of[VI_COUNT] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2 };
static const short vui_row_y[3] = { 120, 184, 246 };
static const short vui_row_x0[3] = { 56, 92, 0 };           /* where a row starts; the icons are 64 apart */
static const short vui_row3_x[4] = { 248, 330, 424, 520 };  /* A-B, repeat, shuffle, clear */

static int vui_idx = -1;                         /* the playing row of menus[M_VIDEOS] */
static int vui_mode = VM_NORMAL;                 /* screen mode */
static int vui_ui = VU_PLAY;
static int vui_sel = VI_PLAY;                    /* selected panel item */
static int vui_repeat = VR_OFF;
static int vui_shuffle;
static int vui_status;                           /* "Display": the status (top strip, bottom row) stays on */
static int vui_info;                             /* the information box (Select) */
static int vui_scan;                             /* fast reverse (-1..-3) / forward (1..3), set with Cross on the search icons */
static uint64_t vui_t_osd;                       /* the bottom row was last asked for (state changes, seeks) */
static float vui_panel_a, vui_osd_a, vui_top_a;  /* fades of the panel, the bottom row and the top strip */
static int vui_list_kind;                        /* VI_AUDIO or VI_SUB while a list is open */
static int vui_list_sel;
static int vui_jump[3], vui_jump_pos;            /* time jump: h m s, the group being set */
static double vui_ab_a = -1, vui_ab_b = -1;
static int vui_scrub_dir;                        /* -1 / +1 while Left / Right is held without the panel */
static uint64_t vui_scrub_t0, vui_scrub_tick, vui_scan_tick;
static char vui_toast[64];                       /* a short message */
static uint64_t vui_toast_t;
static double vui_resume_to = -1;                /* a saved position to jump to once the video runs */

static const char *vui_mode_name[VM_COUNT] = { "Normal", "Zoom", "Full Screen", "Original", "4 : 3" };

static void vui_say(const char *s)
{
	snprintf(vui_toast, sizeof(vui_toast), "%s", s);
	vui_toast_t = sceKernelGetProcessTimeWide();
}

static void fmt_hms(double t, char *out, size_t n)
{
	if (t < 0 || t != t) t = 0;
	unsigned s = (unsigned)t;
	snprintf(out, n, "%02u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
}

/* ---- pictures ---- */
static vita2d_texture *vpng(const char *name)
{
	static char names[96][32];
	static vita2d_texture *tex[96];
	for (int i = 0; i < 96; i++) {
		if (!names[i][0]) {
			snprintf(names[i], sizeof(names[i]), "%s", name);
			char path[96];
			snprintf(path, sizeof(path), "app0:assets/psp/video/%s.png", name);
			tex[i] = vita2d_load_PNG_file(path);
			if (!tex[i]) trace("video picture missing: %s\n", name);
			return tex[i];
		}
		if (!strcmp(names[i], name)) return tex[i];
	}
	return NULL;
}

static void vdraw(const char *name, float cx, float cy, int a)
{
	vita2d_texture *t = vpng(name);
	if (!t || a <= 0) return;
	float x = floorf(cx - vita2d_texture_get_width(t) / 2.0f + 0.5f), y = floorf(cy - vita2d_texture_get_height(t) / 2.0f + 0.5f);
	vita2d_draw_texture_tint(t, x, y, RGBA8(255, 255, 255, a));
}

/* an icon with its shadow (two PSP pixels down and right) and, when selected, its glow */
static void vicon(const char *name, float cx, float cy, int a, int focus)
{
	char n[48];
	snprintf(n, sizeof(n), "%s_shadow", name);
	vdraw(n, cx + 4, cy + 4, a);
	if (focus) { snprintf(n, sizeof(n), "%s_focus", name); vdraw(n, cx, cy, a); }
	vdraw(name, cx, cy, a);
}

/* All text is drawn through the text engine, which uses the PSP's own font (ltn0.pgf) and its soft shadow, like the XMB. The firmware gives the
 * player's texts size 7.61, the same as the status clock, which the app draws at 28; the help and list texts are 7.02 (26). */
#define VUI_TXT 28
#define VUI_TXT_SMALL 26

static void vui_text_shadowed(float cx, float cy, unsigned size, const char *s, int a)
{
	ptext_vc(cx - ptext_width(size, s) / 2.0f, cy, WHITE(a), size, s);
}

static void vui_text_left(float x, float cy, unsigned size, const char *s, int a)
{
	ptext_vc(x, cy, WHITE(a), size, s);
}

static void vui_text_right(float xr, float cy, unsigned size, const char *s, int a)
{
	ptext_vc(xr - ptext_width(size, s), cy, WHITE(a), size, s);
}

/* the firmware's text box (moji_bg: dark navy, 51 % opaque) */
static void vui_tbox(float x, float y, float w, float h, int a)
{
	vita2d_draw_rectangle(x, y, w, h, RGBA8(0, 15, 36, a * 130 / 255));
}

/* ---- remembering where a video was left (like the PSP's resume) ---- */
static uint32_t vui_hash(const char *p)
{
	uint32_t h = 2166136261u;
	for (; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
	return h;
}

typedef struct { uint32_t hash; uint32_t sec; } VResume;
static VResume vui_rs[64];
static int vui_rs_n, vui_rs_loaded;

static void vui_rs_load(void)
{
	if (vui_rs_loaded) return;
	vui_rs_loaded = 1;
	FILE *f = fopen(CONFIG_DIR "/resume.dat", "rb");
	if (!f) return;
	vui_rs_n = (int)fread(vui_rs, sizeof(VResume), 64, f);
	fclose(f);
}

static void vui_rs_set(const char *path, double pos, double dur)
{
	vui_rs_load();
	uint32_t h = vui_hash(path);
	int i = 0;
	while (i < vui_rs_n && vui_rs[i].hash != h) i++;
	int keep = pos > 15.0 && (dur <= 0 || pos < dur - 15.0);
	if (i < vui_rs_n) {                                  /* drop the old entry, then add to the front if it is worth keeping */
		memmove(&vui_rs[i], &vui_rs[i + 1], (size_t)(vui_rs_n - i - 1) * sizeof(VResume));
		vui_rs_n--;
	}
	if (keep) {
		if (vui_rs_n >= 64) vui_rs_n = 63;
		memmove(&vui_rs[1], &vui_rs[0], (size_t)vui_rs_n * sizeof(VResume));
		vui_rs[0].hash = h; vui_rs[0].sec = (uint32_t)pos;
		vui_rs_n++;
	}
	FILE *f = fopen(CONFIG_DIR "/resume.dat", "wb");
	if (f) { fwrite(vui_rs, sizeof(VResume), (size_t)vui_rs_n, f); fclose(f); }
}

static double vui_rs_get(const char *path)
{
	vui_rs_load();
	uint32_t h = vui_hash(path);
	for (int i = 0; i < vui_rs_n; i++) if (vui_rs[i].hash == h) return (double)vui_rs[i].sec;
	return -1;
}

static void video_save_position(void)
{
	if (vui_idx < 0 || vid_status == VS_ERROR || vid_status == VS_IDLE || vid_status == VS_OPENING) return;
	vui_rs_set(vid_path, vid_status == VS_ENDED ? 0.0 : vid_position(), vid_duration);
}

/* ---- starting and stepping through videos ---- */
static int video_playable(int idx)
{
	return idx >= 0 && idx < menus[M_VIDEOS].count && menus[M_VIDEOS].items[idx].kind == KIND_VIDEO && menus[M_VIDEOS].items[idx].path[0];
}

static void video_play_index(int idx)
{
	if (!video_playable(idx)) return;
	if (mus_ready) music_stop();
	vui_idx = idx;
	menus[M_VIDEOS].sel = idx;
	vui_ui = VU_PLAY; vui_sel = VI_PLAY; vui_info = 0; vui_scrub_dir = 0; vui_scan = 0;
	vui_ab_a = vui_ab_b = -1;
	vid_chan = 0;
	vui_resume_to = vui_rs_get(menus[M_VIDEOS].items[idx].path);
	vid_open(menus[M_VIDEOS].items[idx].path);
	vui_t_osd = sceKernelGetProcessTimeWide();
}

static int video_count(void)
{
	int n = 0;
	for (int i = 0; i < menus[M_VIDEOS].count; i++) if (video_playable(i)) n++;
	return n;
}

/* the next / previous video of the folder (shuffle: a random one) */
static void video_step(int dir)
{
	int n = menus[M_VIDEOS].count;
	if (n <= 0 || vui_idx < 0) return;
	video_save_position();
	int i = vui_idx;
	if (vui_shuffle && video_count() > 1) {
		for (int k = 0; k < 8; k++) { int j = (int)(sceKernelGetProcessTimeWide() % (unsigned)n); if (j != vui_idx && video_playable(j)) { i = j; break; } }
	} else {
		for (int k = 0; k < n; k++) { i = (i + dir + n) % n; if (video_playable(i)) break; }
	}
	if (i != vui_idx) video_play_index(i);
}

static int vui_chapter_at(double p)
{
	int ch = 0;
	for (int i = 0; i < vid_n_chapters; i++) if (p >= vid_chapters[i].start) ch = i;
	return ch;
}

/* Previous: the chapter's start, then the one before; without chapters the beginning, then the previous video */
static void vui_prev(void)
{
	double p = vid_position();
	if (vid_n_chapters > 1) {
		int c = vui_chapter_at(p);
		if (p - vid_chapters[c].start > 3.0) vid_seek_abs(vid_chapters[c].start);
		else if (c > 0) vid_seek_abs(vid_chapters[c - 1].start);
		else vid_seek_abs(0);
	} else if (p > 3.0) vid_seek_abs(0);
	else video_step(-1);
}

static void vui_next(void)
{
	if (vid_n_chapters > 1) {
		int c = vui_chapter_at(vid_position());
		if (c + 1 < vid_n_chapters) { vid_seek_abs(vid_chapters[c + 1].start); return; }
	}
	video_step(1);
}

/* ---- the panel ---- */
static int vui_has(int it)
{
	switch (it) {
	case VI_SUB: return vid_n_s > 0;
	case VI_SLOW: case VI_STEP: case VI_PLUS: case VI_MINUS: return vid_backend == VB_FF;       /* the hardware player cannot do these */
	}
	return 1;
}

/* centre of an item on the screen */
static void vui_slot(int it, float *x, float *y)
{
	int row = vui_row_of[it], k = 0;
	for (int i = 0; i < it; i++) if (vui_row_of[i] == row && vui_has(i)) k++;
	*y = vui_row_y[row];
	*x = row == 2 ? vui_row3_x[k < 4 ? k : 3] : vui_row_x0[row] + 64.0f * k;
}

static const char *vui_item_label(int it)
{
	switch (it) {
	case VI_AUDIO: return T("Audio Options");
	case VI_SUB: return T("Subtitle Options");
	case VI_GOTO: return T("Go To");
	case VI_SCREEN: return T("Screen Mode");
	case VI_DISPLAY: return T("Display");
	case VI_HELP: return T("Help");
	case VI_PREV: return T("Previous");
	case VI_NEXT: return T("Next");
	case VI_REV: return T("Fast Reverse");
	case VI_FWD: return T("Fast Forward");
	case VI_SLOW: return T("Slow Motion");
	case VI_STEP: return T("Frame Advance");
	case VI_PLAY: return T("Play");
	case VI_PLUS: return T("Playback Speed (+)");
	case VI_MINUS: return T("Playback Speed (-)");
	case VI_PAUSE: return T("Pause");
	case VI_STOP: return T("Stop");
	case VI_AB: return T("A-B Repeat");
	case VI_REPEAT: return T("Repeat");
	case VI_SHUF: return T("Shuffle");
	case VI_CLEAR: return T("Clear");
	}
	return "";
}

static const char *vui_item_icon(int it)
{
	switch (it) {
	case VI_AUDIO: return "cp_sound";
	case VI_SUB: return "cp_subtitle";
	case VI_GOTO: return "jump";
	case VI_SCREEN: return "change_screen_mode";
	case VI_DISPLAY: return "cp_display";
	case VI_HELP: return "cp_help";
	case VI_PREV: return "prev";
	case VI_NEXT: return "next";
	case VI_REV: return "cp_search_b";
	case VI_FWD: return "cp_search_f";
	case VI_SLOW: return "slow";
	case VI_STEP: return "step";
	case VI_PLAY: return "play";
	case VI_PLUS: return "play_plus";
	case VI_MINUS: return "play_minus";
	case VI_PAUSE: return "pause";
	case VI_STOP: return "stop";
	case VI_AB: return "cp_abrepeat";
	case VI_REPEAT: return "repeat";
	case VI_SHUF: return "shuffle";
	case VI_CLEAR: return "cp_clear";
	}
	return "play";
}

static int vui_list_rows(void) { return vui_list_kind == VI_AUDIO ? vid_n_a + 3 : vid_n_s + 1; }

static void vui_open_list(int kind)
{
	vui_list_kind = kind;
	vui_list_sel = kind == VI_AUDIO ? (vid_n_a ? vid_a_sel : vid_n_a + vid_chan) : vid_s_sel + 1;       /* subtitles: row 0 is "Off" */
	vui_ui = VU_LIST;
}

/* speed steps for Playback Speed (+) / (-): each press moves on, back to x1.0 at the end */
static void vui_speed_step(int plus)
{
	static const double up[3] = { 1.25, 1.5, 2.0 }, down[2] = { 0.75, 0.5 };
	double s = vid_speed, n = 1.0;
	if (plus) { if (s < 1.0) n = 1.0; else for (int i = 0; i < 3; i++) if (up[i] > s + 0.01) { n = up[i]; break; } }
	else { if (s > 1.0) n = 1.0; else for (int i = 0; i < 2; i++) if (down[i] < s - 0.01) { n = down[i]; break; } }
	vid_set_speed(n);
	char t[32];
	snprintf(t, sizeof(t), "x %.2f", vid_speed);
	vui_say(t);
}

/* Fast Reverse / Fast Forward: Cross on the icon moves on through three speeds, then back to normal */
static void vui_scan_step(int dir)
{
	int cur = vui_scan * dir > 0 ? abs(vui_scan) : 0;
	cur++;
	vui_scan = cur > 3 ? 0 : dir * cur;
	vui_scan_tick = sceKernelGetProcessTimeWide();
}

static void vui_activate(int it)
{
	uint64_t now = sceKernelGetProcessTimeWide();
	if (it != VI_REV && it != VI_FWD) vui_scan = 0;
	switch (it) {
	case VI_PREV: vui_prev(); break;
	case VI_NEXT: vui_next(); break;
	case VI_REV: vui_scan_step(-1); vui_t_osd = now; break;
	case VI_FWD: vui_scan_step(1); vui_t_osd = now; break;
	case VI_PLAY: if (vid_speed != 1.0) vid_set_speed(1.0); vid_set_pause(0); vui_t_osd = now; break;
	case VI_PAUSE: vid_set_pause(!vid_paused); vui_t_osd = now; break;       /* pause, and again to resume */
	case VI_STOP: break;                                           /* handled by the caller: leaves the player */
	case VI_SLOW:
		vid_set_speed(vid_speed != 1.0 ? 1.0 : 0.5);
		vui_say(vid_speed != 1.0 ? T("Slow Motion") : T("Normal speed"));
		break;
	case VI_STEP: vid_step_frame(); vui_t_osd = now; break;
	case VI_PLUS: vui_speed_step(1); vui_t_osd = now; break;
	case VI_MINUS: vui_speed_step(0); vui_t_osd = now; break;
	case VI_GOTO: {
		double p = vid_position();
		vui_jump[0] = (int)p / 3600; vui_jump[1] = ((int)p / 60) % 60; vui_jump[2] = (int)p % 60; vui_jump_pos = 2;
		vui_ui = VU_JUMP; break; }
	case VI_REPEAT: {
		static const char *r[3] = { "Repeat Off", "Title Repeat", "Folder Repeat" };
		vui_repeat = (vui_repeat + 1) % 3; vui_say(T(r[vui_repeat])); break; }
	case VI_SHUF: vui_shuffle = !vui_shuffle; vui_say(vui_shuffle ? T("Shuffle On") : T("Shuffle Off")); break;
	case VI_AB:
		if (vui_ab_a < 0) { vui_ab_a = vid_position(); vui_say(T("A set")); }
		else if (vui_ab_b < 0) { double p = vid_position(); if (p > vui_ab_a + 0.5) { vui_ab_b = p; vui_say(T("B set: repeating")); } }
		else { vui_ab_a = vui_ab_b = -1; vui_say(T("A-B Repeat off")); }
		break;
	case VI_CLEAR:
		vui_ab_a = vui_ab_b = -1; vui_repeat = VR_OFF; vui_shuffle = 0;
		vui_say(T("Repeat, A-B Repeat and Shuffle cleared"));
		break;
	case VI_AUDIO: vui_open_list(VI_AUDIO); break;
	case VI_SUB: vui_open_list(VI_SUB); break;
	case VI_SCREEN: vui_mode = (vui_mode + 1) % VM_COUNT; vui_say(T(vui_mode_name[vui_mode])); break;
	case VI_DISPLAY: vui_status = !vui_status; break;
	case VI_HELP: vui_ui = VU_HELP; break;
	}
}

/* the item nearest in x in row r (moving up or down) */
static int vui_row_nearest(int r, float x)
{
	int best = -1;
	float bd = 1e9f;
	for (int i = 0; i < VI_COUNT; i++) {
		if (vui_row_of[i] != r || !vui_has(i)) continue;
		float ix, iy;
		vui_slot(i, &ix, &iy);
		float d = fabsf(ix - x);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

/* what the buttons do. pressed: new presses (the d-pad repeats), held: the buttons now down. Returns 1 to leave the player. */
static int video_input(unsigned pressed, unsigned held)
{
	uint64_t now = sceKernelGetProcessTimeWide();
	if (vid_status == VS_ERROR) return (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) ? 1 : 0;

	if (vid_warn) {                                                  /* a big video: wait for the choice */
		if (pressed & SCE_CTRL_CROSS) { vid_warn = 0; vid_set_pause(0); vui_t_osd = now; }
		if (pressed & SCE_CTRL_CIRCLE) { vid_warn = 0; vid_close(); return 1; }
		return 0;
	}
	if (vui_ui == VU_HELP) { if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) vui_ui = VU_PLAY; return 0; }
	if (vui_ui == VU_LIST) {
		int rows = vui_list_rows();
		if ((pressed & SCE_CTRL_UP) && vui_list_sel > 0) vui_list_sel--;
		if ((pressed & SCE_CTRL_DOWN) && vui_list_sel < rows - 1) vui_list_sel++;
		if (pressed & SCE_CTRL_CROSS) {
			if (vui_list_kind == VI_AUDIO) {
				if (vui_list_sel < vid_n_a) vid_select_audio(vui_list_sel);
				else vid_chan = vui_list_sel - vid_n_a;                 /* both / left / right */
			} else vid_select_sub(vui_list_sel - 1);
			vui_ui = VU_PANEL;
		}
		if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) vui_ui = VU_PANEL;
		return 0;
	}
	if (vui_ui == VU_JUMP) {
		static const int mx[3] = { 99, 59, 59 };
		if ((pressed & SCE_CTRL_LEFT) && vui_jump_pos > 0) vui_jump_pos--;
		if ((pressed & SCE_CTRL_RIGHT) && vui_jump_pos < 2) vui_jump_pos++;
		if (pressed & SCE_CTRL_UP) vui_jump[vui_jump_pos] = (vui_jump[vui_jump_pos] + 1) % (mx[vui_jump_pos] + 1);
		if (pressed & SCE_CTRL_DOWN) vui_jump[vui_jump_pos] = (vui_jump[vui_jump_pos] + mx[vui_jump_pos]) % (mx[vui_jump_pos] + 1);
		if (pressed & SCE_CTRL_CROSS) { vid_seek_abs(vui_jump[0] * 3600.0 + vui_jump[1] * 60.0 + vui_jump[2]); vui_ui = VU_PANEL; vui_t_osd = now; }
		if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) vui_ui = VU_PANEL;
		return 0;
	}

	/* a fast reverse / forward chosen in the panel keeps scanning, a few seconds at a time, until it is moved on or cancelled */
	if (vui_scan && now - vui_scan_tick >= 250000ULL) {
		static const double step[3] = { 4.0, 12.0, 40.0 };
		vid_seek_rel((vui_scan > 0 ? 1.0 : -1.0) * step[abs(vui_scan) - 1]);
		vui_scan_tick = now;
		vui_t_osd = now;
	}

	/* without the panel, holding Left / Right searches and a tap jumps 10 seconds */
	int dir = 0;
	if (vui_ui == VU_PLAY && !vui_scan) dir = (held & SCE_CTRL_LEFT) ? -1 : ((held & SCE_CTRL_RIGHT) ? 1 : 0);
	if (dir && !vui_scrub_dir) { vui_scrub_dir = dir; vui_scrub_t0 = now; vui_scrub_tick = now; }
	if (vui_scrub_dir && dir != vui_scrub_dir) {
		if (now - vui_scrub_t0 < 350000ULL) vid_seek_rel(vui_scrub_dir * 10.0);
		vui_scrub_dir = dir;
		if (dir) { vui_scrub_t0 = now; vui_scrub_tick = now; }
		vui_t_osd = now;
	}
	if (vui_scrub_dir && now - vui_scrub_t0 >= 350000ULL && now - vui_scrub_tick >= 250000ULL) {
		double held_s = (double)(now - vui_scrub_t0) / 1.0e6;
		vid_seek_rel(vui_scrub_dir * (held_s < 1.5 ? 4.0 : (held_s < 3.0 ? 10.0 : 25.0)));
		vui_scrub_tick = now;
		vui_t_osd = now;
	}

	if (pressed & SCE_CTRL_RTRIGGER) vui_next();
	if (pressed & SCE_CTRL_LTRIGGER) vui_prev();
	if (pressed & SCE_CTRL_SQUARE) { vui_mode = (vui_mode + 1) % VM_COUNT; vui_say(T(vui_mode_name[vui_mode])); }
	if (pressed & SCE_CTRL_START) vui_ui = VU_HELP;
	if (pressed & SCE_CTRL_SELECT) vui_info = !vui_info;

	if (vui_ui == VU_PANEL) {
		if (!vui_has(vui_sel)) vui_sel = VI_PLAY;
		int r = vui_row_of[vui_sel];
		float x, y;
		vui_slot(vui_sel, &x, &y);
		if (pressed & SCE_CTRL_LEFT) { for (int i = vui_sel - 1; i >= 0 && vui_row_of[i] == r; i--) if (vui_has(i)) { vui_sel = i; break; } }
		if (pressed & SCE_CTRL_RIGHT) { for (int i = vui_sel + 1; i < VI_COUNT && vui_row_of[i] == r; i++) if (vui_has(i)) { vui_sel = i; break; } }
		if ((pressed & SCE_CTRL_UP) && r > 0) vui_sel = vui_row_nearest(r - 1, x);
		if ((pressed & SCE_CTRL_DOWN) && r < 2) vui_sel = vui_row_nearest(r + 1, x);
		if (pressed & SCE_CTRL_CROSS) {
			if (vui_sel == VI_STOP) { video_save_position(); vid_close(); return 1; }
			vui_activate(vui_sel);
		}
		if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) vui_ui = VU_PLAY;
		return 0;
	}
	/* no panel */
	if (pressed & SCE_CTRL_CROSS) { vui_scan = 0; vid_toggle_pause(); vui_t_osd = now; }
	if (pressed & SCE_CTRL_TRIANGLE) { vui_ui = VU_PANEL; vui_sel = VI_PLAY; }
	if (pressed & SCE_CTRL_CIRCLE) { video_save_position(); vid_close(); return 1; }
	return 0;
}

/* the video's rectangle on the 960 x 544 screen for the screen mode */
static void vui_dest(int mode, float *x, float *y, float *w, float *h)
{
	float vw = (float)vid_w, vh = (float)vid_h;
	if (vw <= 0 || vh <= 0) { *x = 0; *y = 0; *w = 960; *h = 544; return; }
	float s;
	switch (mode) {
	case VM_ORIGINAL: s = vw <= 480.0f ? 2.0f : 1.0f; break;
	case VM_ZOOM:     s = 960.0f / vw > 544.0f / vh ? 960.0f / vw : 544.0f / vh; break;
	case VM_FULL:     *x = 0; *y = 0; *w = 960; *h = 544; return;
	case VM_43:       *h = 544.0f; *w = 544.0f * 4.0f / 3.0f; *x = floorf((960.0f - *w) / 2.0f + 0.5f); *y = 0; return;     /* the picture squeezed into a 4:3 area */
	default:          s = 960.0f / vw < 544.0f / vh ? 960.0f / vw : 544.0f / vh; break;
	}
	*w = vw * s; *h = vh * s;
	*x = floorf((960.0f - *w) / 2.0f + 0.5f); *y = floorf((544.0f - *h) / 2.0f + 0.5f);
}

/* a dark box for lists and messages */
static void vui_box(float x, float y, float w, float h, int a)
{
	vui_tbox(x, y, w, h, a);
	vita2d_draw_rectangle(x, y, w, 2, RGBA8(255, 255, 255, a * 55 / 100));
	vita2d_draw_rectangle(x, y + h - 2, w, 2, RGBA8(255, 255, 255, a * 55 / 100));
}

/* the clock, in the format of the PSP's video player: 2005/5/1 2:36PM */
static void vui_clock_text(char *out, size_t n)
{
	SceDateTime dt;
	sceRtcGetCurrentClockLocalTime(&dt);
	if (clock24) snprintf(out, n, "%d/%d/%d %d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
	else snprintf(out, n, "%d/%d/%d %d:%02d%s", dt.year, dt.month, dt.day, dt.hour % 12 ? dt.hour % 12 : 12, dt.minute, dt.hour >= 12 ? "PM" : "AM");
}

/* the strip across the top: film icon, title, clock */
static void vui_draw_top(int a)
{
	vita2d_draw_rectangle(0, 0, SCREEN_W, 44, RGBA8(255, 255, 255, a * 154 / 255));
	vdraw("title_icon", 30, 22, a);
	const char *ttl = vui_idx >= 0 && vui_idx < menus[M_VIDEOS].count ? menus[M_VIDEOS].items[vui_idx].title : "";
	ptext_vc_fit(60, 22, WHITE(a), VUI_TXT, ttl, 600.0f);
	char c[40];
	vui_clock_text(c, sizeof(c));
	vui_text_right(950, 22, VUI_TXT, c, a);
	ptext_right_vc(936, 66, RGBA8(255, 255, 255, a * 70 / 100), 20, "BETA");
}

/* the bottom row: state icon, repeat icon, time line, chapter box, time box */
static void vui_draw_bottom(int a)
{
	char cur[16], tot[16], tm[48];
	double pos = vid_position();
	fmt_hms(pos, cur, sizeof(cur));
	fmt_hms(vid_duration, tot, sizeof(tot));

	char st[24];
	int lv = vui_scan ? abs(vui_scan) : (vui_scrub_dir ? 2 : 0);
	int dr = vui_scan ? vui_scan : vui_scrub_dir;
	if (lv) snprintf(st, sizeof(st), "search_%c_%d", dr < 0 ? 'b' : 'f', lv);
	else snprintf(st, sizeof(st), "%s", vid_paused ? "pause" : "play");
	vicon(st, 52, 513, a, 0);
	if (vid_speed != 1.0) {                                            /* the speed, between the state and the repeat icon */
		snprintf(tm, sizeof(tm), "x%.2f", vid_speed);
		vui_text_shadowed(106, 513, 22, tm, a);
	}
	if (vui_repeat == VR_ONE) vicon("title_repeat", 168, 513, a, 0);
	else if (vui_repeat == VR_ALL) vicon("folder_repeat", 168, 513, a, 0);
	if (vui_shuffle) vicon("shuffle", 232, 513, a, 0);
	if (vui_ab_a >= 0) vicon("Ato", 284, 513, a, 0);
	if (vui_ab_b >= 0) vicon("B", 326, 513, a, 0);

	/* the time line: white, blue where it has been played (the firmware's progress bar pictures) */
	const float bx = 285, bw = 665, by = 510, bh = 6;
	float f = vid_duration > 0 ? clampf((float)(pos / vid_duration), 0.0f, 1.0f) : 0.0f;
	vita2d_draw_rectangle(bx, by, bw, bh, RGBA8(255, 255, 255, a * 230 / 255));
	vita2d_draw_rectangle(bx, by, bw * f, bh, RGBA8(36, 64, 232, a));
	vita2d_draw_rectangle(bx, by + bh - 2, bw * f, 2, RGBA8(14, 36, 190, a));
	vita2d_draw_rectangle(floorf(bx + bw * f) - 1, by - 5, 3, bh + 10, RGBA8(36, 64, 232, a));
	if (vid_duration > 0) {                                             /* the A-B marks on the line */
		if (vui_ab_a >= 0) vita2d_draw_rectangle(floorf(bx + bw * clampf((float)(vui_ab_a / vid_duration), 0, 1)), by - 4, 3, bh + 8, RGBA8(255, 170, 40, a));
		if (vui_ab_b >= 0) vita2d_draw_rectangle(floorf(bx + bw * clampf((float)(vui_ab_b / vid_duration), 0, 1)), by - 4, 3, bh + 8, RGBA8(255, 170, 40, a));
	}

	/* the chapter box and the time box (y 456 .. 491) */
	if (vid_n_chapters > 0) {
		snprintf(tm, sizeof(tm), T("Chapter %d"), vui_chapter_at(pos) + 1);
		vui_tbox(435, 456, 177, 35, a);
		vui_text_shadowed(435 + 177 / 2.0f, 473, VUI_TXT, tm, a);
	}
	vui_tbox(625, 456, 325, 35, a);
	if (vui_ui == VU_JUMP) {                                            /* the time becomes the spinner, the group being set lit */
		float x = 625 + 16;
		for (int i = 0; i < 3; i++) {
			char g[8];
			snprintf(g, sizeof(g), "%02d", vui_jump[i]);
			float w = (float)ptext_width(VUI_TXT, g);
			if (i == vui_jump_pos) vdraw("jump_select_focus", x + w / 2.0f, 473, a);
			vui_text_left(x, 473, VUI_TXT, g, a);
			x += w;
			if (i < 2) { vui_text_left(x, 473, VUI_TXT, ":", a); x += (float)ptext_width(VUI_TXT, ":"); }
		}
		snprintf(tm, sizeof(tm), "/%s", tot);
		vui_text_left(x + 4, 473, VUI_TXT, tm, a);
	} else {
		snprintf(tm, sizeof(tm), "%s/%s", cur, tot);
		vui_text_shadowed(625 + 325 / 2.0f, 473, VUI_TXT, tm, a);
	}
}

/* the panel: three rows of icons and the name of the selected one */
static void vui_draw_panel(int a)
{
	for (int i = 0; i < VI_COUNT; i++) {
		if (!vui_has(i)) continue;
		float x, y;
		vui_slot(i, &x, &y);
		vicon(vui_item_icon(i), x, y, a, i == vui_sel && vui_ui == VU_PANEL);
	}
	vui_text_left(258, 314, VUI_TXT, vui_item_label(vui_sel), a);
}

static void vui_draw_list(int a)
{
	int rows = vui_list_rows();
	float w = 480, h = 36.0f * rows + 24, x = 470, y = 96;
	vui_box(x, y, w, h, a);
	for (int i = 0; i < rows; i++) {
		char t[64];
		if (vui_list_kind == VI_AUDIO) {
			if (i < vid_n_a) {
				const VTrack *k = &vid_atracks[i];
				snprintf(t, sizeof(t), "%d  %s  %s  %dch%s", i + 1, k->lang[0] ? k->lang : "und", k->name, k->channels, i == vid_a_sel ? "  *" : "");
			} else {
				const char *o[3] = { T("Output: Left and right"), T("Output: Left only"), T("Output: Right only") };
				snprintf(t, sizeof(t), "%s%s", o[i - vid_n_a], i - vid_n_a == vid_chan ? "  *" : "");
			}
		} else if (i == 0) snprintf(t, sizeof(t), "%s%s", T("Off"), vid_s_sel < 0 ? "  *" : "");
		else {
			const VTrack *k = &vid_stracks[i - 1];
			snprintf(t, sizeof(t), "%d  %s  %s%s", i, k->lang[0] ? k->lang : "und", k->name, i - 1 == vid_s_sel ? "  *" : "");
		}
		float ry = y + 24 + 36.0f * i;
		if (i == vui_list_sel) vita2d_draw_rectangle(x + 8, ry - 17, w - 16, 34, RGBA8(255, 255, 255, a * 28 / 100));
		ptext_vc(x + 24, ry, WHITE(a), VUI_TXT_SMALL, t);
	}
}

static void vui_draw_help(int a)
{
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, a * 78 / 100));
	static const char *rows[][2] = {
		{ "\xE2\x9C\x95", "Play / pause" },                           /* ✕ */
		{ "\xE2\x97\x8B", "Stop and go back" },                       /* ○ */
		{ "\xE2\x96\xB3", "Show the control panel" },                 /* △ */
		{ "\xE2\x96\xA1", "Change the screen mode" },                 /* □ */
		{ "L / R", "Previous / next" },
		{ "\xE2\x86\x90 \xE2\x86\x92", "Tap: 10 seconds; hold: search" },
		{ "START", "This help" },
		{ "SELECT", "Show the information" },
	};
	vui_text_shadowed(480, 70, 34, T("Video player (BETA)"), a);
	for (int i = 0; i < 8; i++) {
		float y = 140 + i * 44;
		ptext_vc(210, y, WHITE(a), 28, rows[i][0]);
		ptext_vc(380, y, WHITE(a * 90 / 100), VUI_TXT_SMALL, T(rows[i][1]));
	}
	vui_text_shadowed(480, 510, 22, T("In the control panel: the d-pad chooses, \xE2\x9C\x95 uses, \xE2\x97\x8B closes"), a * 80 / 100);
}

static void vui_draw_info(int a)
{
	char l[96];
	float x = 24, y = 96;
	vui_box(x - 12, y - 22, 560, 214, a);
	const char *ttl = vui_idx >= 0 && vui_idx < menus[M_VIDEOS].count ? menus[M_VIDEOS].items[vui_idx].title : "";
	ptext_vc_fit(x, y, WHITE(a), 24, ttl, 450.0f);
	ptext_right_vc(x + 520, y, RGBA8(255, 255, 255, a * 70 / 100), 20, "BETA");
	snprintf(l, sizeof(l), "%d x %d   %s   %.2f fps", vid_w, vid_h, vid_vcodec, vid_fps);
	ptext_vc(x, y + 34, WHITE(a * 85 / 100), 22, l);
	snprintf(l, sizeof(l), T("Container: %s   %d kbit/s"), vid_container, vid_bitrate);
	ptext_vc(x, y + 64, WHITE(a * 85 / 100), 22, l);
	snprintf(l, sizeof(l), T("Decoder: %s"), vid_backend == VB_AVP ? T("hardware (system player)") : T("software (FFmpeg)"));
	ptext_vc(x, y + 94, WHITE(a * 85 / 100), 22, l);
	if (vid_n_a) {
		snprintf(l, sizeof(l), T("Audio: %s %s (%d tracks)"), vid_atracks[vid_a_sel].name, vid_atracks[vid_a_sel].lang, vid_n_a);
		ptext_vc(x, y + 124, WHITE(a * 85 / 100), 22, l);
	}
	{ char w[80]; snprintf(w, sizeof(w), T(vid_n_s == 1 ? "Subtitles: %d track" : "Subtitles: %d tracks"), vid_n_s); snprintf(l, sizeof(l), "%s%s", w, vid_n_cues ? "" : T(" (none loaded yet)")); }
	ptext_vc(x, y + 154, WHITE(a * 85 / 100), 22, l);
}

static void draw_video(float alpha, int closing)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, a));
	if (closing) return;
	uint64_t now = sceKernelGetProcessTimeWide();

	if (vid_status == VS_ERROR) {
		vui_text_shadowed(480, 250, 30, T("This video can't be played"), a);
		vui_text_shadowed(480, 290, 22, T("Press \xE2\x97\x8B to go back"), a);
		return;
	}
	vid_busy = 1;                                                   /* from here the control thread keeps its hands off the frames */
	__sync_synchronize();
	if (vid_status != VS_PLAYING && vid_status != VS_ENDED) {
		vid_busy = 0;
		vui_text_shadowed(480, 272, 26, T("Loading..."), a);
		vui_text_shadowed(480, 310, 20, vid_stage, a * 70 / 100);
		ptext_right_vc(936, 30, RGBA8(255, 255, 255, a * 70 / 100), 20, "BETA");
		return;
	}
	{	/* no screen dimming or sleep while a video plays */
		static uint64_t tick_t;
		if (now - tick_t > 1000000ULL) {
			tick_t = now;
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
		}
	}
	vid_update();
	if (vui_resume_to > 0 && vid_duration > 0 && vid_position() < 2.0 && vid_tex_cur()) {          /* pick up where the video was left */
		vid_seek_abs(vui_resume_to);
		char r[40], t[16];
		fmt_hms(vui_resume_to, t, sizeof(t));
		snprintf(r, sizeof(r), T("Resumed at %s"), t);
		vui_say(r);
		vui_resume_to = -1;
	} else if (vui_resume_to > 0 && vid_tex_cur() && vid_position() > 3.0) vui_resume_to = -1;
	if (vui_ab_a >= 0 && vui_ab_b > vui_ab_a && vid_position() >= vui_ab_b) vid_seek_abs(vui_ab_a);       /* A-B repeat */

	vita2d_texture *t = vid_tex_cur();
	if (t) {
		float x, y, w, h;
		vui_dest(vui_mode, &x, &y, &w, &h);
		vita2d_draw_texture_tint_scale(t, x, y, w / vid_w, h / vid_h, WHITE(a));
	}

	/* subtitles are read here, still under the guard */
	const VCue *cue = vid_s_sel >= 0 ? vid_cue_at(vid_position()) : NULL;
	char cue_text[200];
	cue_text[0] = 0;
	if (cue) snprintf(cue_text, sizeof(cue_text), "%s", cue->text);
	vid_busy = 0;

	/* the panel while it is open; "Display" keeps the status (top strip and bottom row) on; the bottom row also shows for a few seconds after anything happened */
	int panel = vui_ui == VU_PANEL || vui_ui == VU_LIST || vui_ui == VU_JUMP;
	int bar = vui_status || vid_paused || vid_status == VS_ENDED || vui_scrub_dir || vui_scan || vui_ui == VU_JUMP || vid_speed != 1.0 || now - vui_t_osd < 3500000ULL;
	vui_panel_a += ((panel ? 1.0f : 0.0f) - vui_panel_a) * 0.3f;
	vui_osd_a += ((bar ? 1.0f : 0.0f) - vui_osd_a) * 0.25f;
	vui_top_a += ((vui_status ? 1.0f : 0.0f) - vui_top_a) * 0.3f;
	int pa = (int)(a * vui_panel_a), oa = (int)(a * vui_osd_a), ta = (int)(a * vui_top_a);

	if (cue_text[0]) {                                               /* subtitles stay above the bottom row */
		char line[200];
		snprintf(line, sizeof(line), "%s", cue_text);
		int nl = 1;
		for (char *p = line; *p; p++) if (*p == '\n') nl++;
		float base = oa > 40 ? 430.0f : 500.0f;
		float y = base - (nl - 1) * 36.0f;
		char *s = line;
		while (s) {
			char *e = strchr(s, '\n');
			if (e) *e = 0;
			vui_text_shadowed(480, y, 30, s, a);
			y += 36.0f;
			s = e ? e + 1 : NULL;
		}
	}

	if (ta > 3) vui_draw_top(ta);
	if (pa > 3) vui_draw_panel(pa);
	if (oa > 3) vui_draw_bottom(oa);
	if (vui_ui == VU_LIST) vui_draw_list(a);
	if (vui_info) vui_draw_info(a);
	if (vui_toast[0] && now - vui_toast_t < 1800000ULL) vui_text_shadowed(480, 390, 28, vui_toast, a);
	if (vui_ui == VU_HELP) vui_draw_help(a);
	if (vid_warn) {
		vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, a * 62 / 100));
		vui_box(150, 150, 660, 250, a);
		char l[80];
		snprintf(l, sizeof(l), "%d x %d", vid_w, vid_h);
		vui_text_shadowed(480, 190, 30, vid_warn == 1 ? T("This video is bigger than 720p") : T("This video is large for software decoding"), a);
		vui_text_shadowed(480, 232, 26, l, a * 85 / 100);
		if (vid_warn == 1) {
			vui_text_shadowed(480, 280, 22, T("The Vita can't play pictures this size smoothly."), a * 90 / 100);
			vui_text_shadowed(480, 310, 22, T("Expect stuttering, dropped frames or a failure to start."), a * 90 / 100);
		} else {
			vui_text_shadowed(480, 280, 22, T("The Vita's hardware can't play this file, so it is decoded in"), a * 90 / 100);
			vui_text_shadowed(480, 310, 22, T("software, which is slow. Expect stuttering."), a * 90 / 100);
		}
		vui_text_shadowed(480, 366, 24, T("\xE2\x9C\x95  Play anyway          \xE2\x97\x8B  Back"), a);
	}
}

/* what to do when the video ended */
static int video_after_end(void)
{
	if (vid_status != VS_ENDED) return 0;
	vui_rs_set(vid_path, 0.0, 0.0);                        /* finished: forget the resume point */
	if (vui_repeat == VR_ONE) { vid_seek_abs(0); return 0; }
	int n = menus[M_VIDEOS].count;
	if (vui_idx >= 0 && (vui_repeat == VR_ALL || vui_shuffle || vui_idx + 1 < n) && video_count() > 1) { video_step(1); return 0; }
	vid_close();
	return 1;                                              /* leave the player */
}
