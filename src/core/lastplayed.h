#pragma once

/* ------------------------------------------------------------------ */
/* Last played                                                         */
/* ------------------------------------------------------------------ */
/* The game started last sits under Memory Stick in the Game column with the PSP's UMD icon, like the disc in a PSP's drive.
 * Its title id is kept in ux0:data/VitaXMB/lastplayed.txt when a game is launched. */

#define LASTPLAYED_PATH CONFIG_DIR "/lastplayed.txt"
#define LP_FIRST_EXTRA 2          /* the Game column holds Saved Data Utility and Memory Stick; the disc comes after them */

static char lp_id[16];
static int lp_loaded;

static void lastplayed_save(const char *id)
{
	if (!id || !id[0] || !strcmp(id, OWN_TITLEID)) return;
	snprintf(lp_id, sizeof(lp_id), "%s", id);
	lp_loaded = 1;
	FILE *f = fopen(LASTPLAYED_PATH, "wb");
	if (!f) return;
	fputs(lp_id, f);
	fclose(f);
}

/* (Re)builds the disc entry from the installed apps: called after every scan. */
static void lastplayed_refresh(void)
{
	Menu *mn = &menus[CAT_GAME];
	if (!lp_loaded) {
		lp_loaded = 1;
		FILE *f = fopen(LASTPLAYED_PATH, "rb");
		if (f) {
			size_t n = fread(lp_id, 1, sizeof(lp_id) - 1, f);
			lp_id[n] = 0;
			for (size_t i = 0; i < n; i++) if (lp_id[i] == '\r' || lp_id[i] == '\n' || lp_id[i] == ' ') { lp_id[i] = 0; break; }
			fclose(f);
		}
	}
	int had = mn->count > LP_FIRST_EXTRA;
	int was_sel = had && mn->sel == LP_FIRST_EXTRA;
	if (had) mn->count = LP_FIRST_EXTRA;                         /* the entry is rebuilt below (the icon thread never touches it: it has no icon path) */
	if (mn->sel >= mn->count) mn->sel = mn->count - 1;
	if (!lp_id[0] || !all_apps || mn->count >= mn->cap) return;
	for (int i = 0; i < n_all; i++) {
		if (strcmp(all_apps[i].id, lp_id) != 0) continue;
		Item *d = &mn->items[mn->count];
		*d = all_apps[i];
		d->icon = NULL; d->icon_tried = 0; d->load_state = 0; d->pending_pix = NULL;
		d->icon_rect = 0; d->meta_resolved = 0; d->dec_queued = 0; d->glow = 0.0f;
		d->gate_path[0] = d->bg_path[0] = 0;
		d->icon_path[0] = d->icon_path2[0] = 0;                  /* the UMD icon stands in for the game's own */
		d->stock = tex_umd ? tex_umd : tex_game_s;
		snprintf(d->sub, sizeof(d->sub), "Last Played");
		resolve_meta(d);                                         /* the game's background still shows when the disc is selected */
		__sync_synchronize();
		mn->count++;
		if (was_sel) mn->sel = LP_FIRST_EXTRA;
		return;
	}
}
