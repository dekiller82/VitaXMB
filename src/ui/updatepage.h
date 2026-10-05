#pragma once

/* ---- the Check for Updates page, and the small notice at start-up ---- */

/* Draws text word-wrapped into a column; returns the y after the last line (at most max_lines lines). */
static float upd_wrap(const char *s, float x, float y, float width, int size, int a, int max_lines, float line_h)
{
	char line[200];
	int lines = 0;
	while (*s && lines < max_lines) {
		size_t n = 0;
		line[0] = 0;
		while (*s && *s != '\n') {
			const char *w = s;
			size_t wl = 0;
			while (w[wl] && w[wl] != ' ' && w[wl] != '\n') wl++;
			char trial[200];
			if (n + wl + 1 >= sizeof(trial)) break;
			memcpy(trial, line, n);
			if (n) trial[n] = ' ';
			memcpy(trial + (n ? n + 1 : 0), w, wl);
			trial[(n ? n + 1 : 0) + wl] = 0;
			if (n && ptext_width(size, trial) > width) break;
			snprintf(line, sizeof(line), "%s", trial);
			n = strlen(line);
			s = w + wl;
			while (*s == ' ') s++;
		}
		if (*s == '\n') s++;
		if (n) ptext_vc(x, y, WHITE(a * 8 / 10), size, line);
		y += line_h;
		lines++;
	}
	return y;
}

/* the release notes wrapped into lines (markdown marks dropped), rebuilt when the release changes */
#define UPD_MAX_LINES 220
#define UPD_WIN_LINES 8
static char upd_lines[UPD_MAX_LINES][112];
static int upd_nlines;
static char upd_lines_tag[40];

static void upd_layout_notes(void)
{
	if (strcmp(upd_lines_tag, upd_tag) == 0 && upd_nlines) return;
	snprintf(upd_lines_tag, sizeof(upd_lines_tag), "%s", upd_tag);
	upd_nlines = 0;
	int blank = 1;
	const char *s = upd_notes;
	while (*s && upd_nlines < UPD_MAX_LINES) {
		char para[600];
		size_t pn = 0;
		while (*s && *s != '\n' && pn + 1 < sizeof(para)) {
			if (*s != '#' && *s != '*' && *s != '`' && *s != '\r') para[pn++] = *s;
			s++;
		}
		if (*s == '\n') s++;
		para[pn] = 0;
		char *p = para;
		while (*p == ' ') p++;
		if (!*p) {                                              /* an empty line: one gap, never two */
			if (!blank && upd_nlines < UPD_MAX_LINES) { upd_lines[upd_nlines++][0] = 0; blank = 1; }
			continue;
		}
		blank = 0;
		char line[112] = "";
		size_t ln = 0;
		while (*p && upd_nlines < UPD_MAX_LINES) {
			char word[80];
			size_t wl = 0;
			while (p[wl] && p[wl] != ' ' && wl + 1 < sizeof(word)) { word[wl] = p[wl]; wl++; }
			word[wl] = 0;
			char trial[200];
			snprintf(trial, sizeof(trial), "%s%s%s", line, ln ? " " : "", word);
			if (ln && ptext_width(22, trial) > 800.0f) {
				snprintf(upd_lines[upd_nlines++], sizeof(upd_lines[0]), "%s", line);
				line[0] = 0; ln = 0;
				continue;                                       /* try the same word on the fresh line */
			}
			snprintf(line, sizeof(line), "%s", trial);
			ln = strlen(line);
			p += wl;
			while (*p == ' ') p++;
		}
		if (ln && upd_nlines < UPD_MAX_LINES) snprintf(upd_lines[upd_nlines++], sizeof(upd_lines[0]), "%s", line);
	}
	while (upd_nlines > 0 && !upd_lines[upd_nlines - 1][0]) upd_nlines--;
}

static void draw_update_page(float alpha)
{
	int a = (int)(255 * alpha);
	if (a <= 3) return;
	char buf[200];
	const char *head = "Network Update", *cross = NULL;
	snprintf(buf, sizeof(buf), "VitaXMB %s", APP_VERSION);
	ptext_vc(60, 60, WHITE(a), 34, head);
	ptext_right_vc(900, 60, WHITE(a * 6 / 10), 24, buf);
	vita2d_draw_rectangle(50, 90, 860, 2, WHITE(a * 30 / 100));
	int st = upd_state;
	float y = 150;
	switch (st) {
	case UPD_CHECKING:
		ptext_vc(60, y, WHITE(a), 28, "Checking GitHub...");
		break;
	case UPD_UPTODATE:
		snprintf(buf, sizeof(buf), "VitaXMB %s is up to date.", APP_VERSION);
		ptext_vc(60, y, WHITE(a), 28, buf);
		cross = "Check again";
		break;
	case UPD_AVAILABLE:
		snprintf(buf, sizeof(buf), "Version %s is available (you have %s).", upd_tag, APP_VERSION);
		ptext_vc(60, y, WHITE(a), 28, buf);
		upd_layout_notes();
		{
			int maxs = upd_nlines > UPD_WIN_LINES ? upd_nlines - UPD_WIN_LINES : 0;
			if (upd_scroll > maxs) upd_scroll = maxs;
			if (upd_scroll < 0) upd_scroll = 0;
			for (int i = 0; i < UPD_WIN_LINES && upd_scroll + i < upd_nlines; i++)
				if (upd_lines[upd_scroll + i][0]) ptext_vc(60, y + 50 + i * 30, WHITE(a * 8 / 10), 22, upd_lines[upd_scroll + i]);
			if (maxs > 0) {                                     /* a bar at the right shows where in the notes this is */
				float track = UPD_WIN_LINES * 30.0f, th = track * UPD_WIN_LINES / (float)upd_nlines;
				vita2d_draw_rectangle(900, y + 36, 3, track, WHITE(a * 20 / 100));
				vita2d_draw_rectangle(900, y + 36 + (track - th) * upd_scroll / (float)maxs, 3, th, WHITE(a * 70 / 100));
			}
		}
		cross = "Install";
		break;
	case UPD_DOWNLOADING: {
		uint32_t got = upd_got, total = upd_total;
		snprintf(buf, sizeof(buf), "Downloading %s...", upd_tag);
		ptext_vc(60, y, WHITE(a), 28, buf);
		float frac = total ? (float)got / total : 0.0f;
		vita2d_draw_rectangle(60, y + 40, 760, 10, WHITE(a * 25 / 100));
		vita2d_draw_rectangle(60, y + 40, 760 * (frac > 1.0f ? 1.0f : frac), 10, WHITE(a));
		snprintf(buf, sizeof(buf), "%.1f MB%s", got / 1048576.0f, total ? "" : "");
		if (total) snprintf(buf, sizeof(buf), "%.1f of %.1f MB", got / 1048576.0f, total / 1048576.0f);
		ptext_vc(60, y + 80, WHITE(a * 7 / 10), 22, buf);
		break;
	}
	case UPD_INSTALLING:
		ptext_vc(60, y, WHITE(a), 28, "Installing... please wait");
		ptext_vc(60, y + 40, WHITE(a * 7 / 10), 22, "Do not close the application.");
		break;
	case UPD_DONE:
		snprintf(buf, sizeof(buf), "%s is installed.", upd_tag);
		ptext_vc(60, y, WHITE(a), 28, buf);
		ptext_vc(60, y + 40, WHITE(a * 7 / 10), 22, "The app restarts to finish the update.");
		cross = "Restart now";
		break;
	case UPD_SAVED:
		ptext_vc(60, y, WHITE(a), 28, "The update is downloaded.");
		upd_wrap(upd_err[0] ? upd_err : "It could not be installed from here.", 60, y + 50, 820, 22, a, 3, 28);
		upd_wrap("Install ux0:data/VitaXMB/update.vpk with VitaShell to finish the update.", 60, y + 130, 820, 22, a, 3, 28);
		break;
	case UPD_FAILED:
		ptext_vc(60, y, WHITE(a), 28, "The update did not work.");
		upd_wrap(upd_err, 60, y + 44, 820, 22, a, 3, 28);
		cross = "Try again";
		break;
	default:
		ptext_vc(60, y, WHITE(a), 28, "Looks for a newer VitaXMB on GitHub.");
		cross = "Check now";
		break;
	}
	float bx = 60;
	if (st == UPD_AVAILABLE && upd_nlines > UPD_WIN_LINES) ptext_right_vc(900, 506, WHITE(a * 6 / 10), 20, "Up / Down: scroll");
	if (cross) {
		glyph_cross(bx + 10, 506, 8, a);
		ptext_vc(bx + 30, 506, WHITE(a), 24, cross);
		bx += 40 + ptext_width(24, cross) + 30;
	}
	if (st != UPD_INSTALLING) {
		glyph_ring(bx + 10, 506, 8, a);
		ptext_vc(bx + 30, 506, WHITE(a), 24, st == UPD_DOWNLOADING ? "Cancel" : "Back");
	}
}

/* A line at the top of the XMB when the start-up check found something new. */
static void draw_update_notice(float t_now, float *shown_for)
{
	if (upd_result_msg[0]) {                                    /* the outcome of an update that just ran */
		static float res_s;
		res_s += t_now;
		if (res_s < 8.0f) {
			int ra = res_s < 0.5f ? (int)(470 * res_s) : (res_s > 7.0f ? (int)(235 * (8.0f - res_s)) : 235);
			float rw = ptext_width(22, upd_result_msg);
			vita2d_draw_rectangle(480 - rw / 2 - 16, 6, rw + 32, 34, RGBA8(0, 0, 0, ra * 45 / 100));
			ptext_vc(480 - rw / 2, 24, WHITE(ra), 22, upd_result_msg);
		}
		return;
	}
	if (!upd_found || upd_state == UPD_DOWNLOADING || upd_state == UPD_INSTALLING) return;
	if (*shown_for > 9.0f) return;
	*shown_for += t_now;
	float fade = *shown_for < 0.5f ? *shown_for / 0.5f : (*shown_for > 8.0f ? (9.0f - *shown_for) : 1.0f);
	if (fade <= 0.0f) return;
	char buf[160];
	snprintf(buf, sizeof(buf), "Update available: %s   (Settings > Network Update)", upd_tag);
	int a = (int)(235 * (fade > 1.0f ? 1.0f : fade));
	float w = ptext_width(22, buf);
	vita2d_draw_rectangle(480 - w / 2 - 16, 6, w + 32, 34, RGBA8(0, 0, 0, a * 45 / 100));
	ptext_vc(480 - w / 2, 24, WHITE(a), 22, buf);
}
