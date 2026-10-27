#pragma once

/* ---- PSM games ----
 * A PlayStation Mobile game lives in ux0:psm/<title id> and has a LiveArea bubble whose files are in ur0:appmeta/<title id> (there is no folder for it
 * in ux0:app, so the scan of ux0:app never saw it). It starts like any bubble, through psgm:play?titleid=<id>; its name is in LiveArea's database. */

static void scan_psm_games(void)
{
	SceUID d = sceIoDopen("ux0:psm");
	if (d < 0) return;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0) {
		if (SCE_S_ISDIR(e.d_stat.st_mode) && e.d_name[0] != '.' && strlen(e.d_name) < 16) {
			char p[64], title[64] = "";
			snprintf(p, sizeof(p), "ux0:app/%s", e.d_name);
			int listed = psp_exists(p);                                     /* an app of ux0:app is in the list already */
			snprintf(p, sizeof(p), "ur0:appmeta/%s", e.d_name);
			int known = !listed && (appdb_title(e.d_name, title, sizeof(title)) || psp_exists(p));      /* a title without a bubble cannot be started */
			if (known) {
				if (!title[0]) snprintf(title, sizeof(title), "%s", e.d_name);
				clean_title(title);
				Item *it = add_item(M_MEMCARD, KIND_APP, title, e.d_name, e.d_name, tex_game_s);
				if (it) {
					snprintf(it->icon_path, sizeof(it->icon_path), "ur0:appmeta/%s/icon0.png", e.d_name);
					snprintf(it->pic_path, sizeof(it->pic_path), "ur0:appmeta/%s/pic0.png", e.d_name);
					snprintf(it->meta_dir, sizeof(it->meta_dir), "ur0:appmeta/%s/livearea/contents/", e.d_name);
					it->org = ORG_PSM;
				}
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}
