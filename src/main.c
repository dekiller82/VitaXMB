/*
 * VitaXMB - a PSP-style XMB launcher for the PS Vita.
 * Scans ux0:app for installed games/homebrew and launches them.
 *
 * The program is one translation unit built from the modules below, included in dependency order:
 *   core/    data, config, scanning, items, folders, settings, menus, icons
 *   media/   sound effects, music player, video and music scanning
 *   render/  textures, text, background, the wave and the sky
 *   theme/   PSP themes (.ctf/.ptf), the RCO engine, the boot animation
 *   ui/      layout, lists, dialogs, panels, pickers, keyboard
 */
#include "core/common.h"
#include "render/xform.h"
#include "theme/paf.h"
#include "core/data.h"
#include "core/assets.h"
#include "media/sound.h"
#include "core/config.h"
#include "core/sfo.h"
#include "render/texture.h"
#include "render/sky_tex.h"
#include "render/wave_stock.h"
#include "render/wave.h"
#include "theme/psptheme.h"
#include "theme/rco.h"
#include "theme/resources.h"
#include "theme/bgscene.h"
#include "theme/themesound.h"
#include "theme/menuscale.h"
#include "theme/boot.h"
#include "core/pngload.h"
#include "core/items.h"
#include "core/vitashell.h"
#include "core/appinfo.h"
#include "core/artwork.h"
#include "core/folders.h"
#include "core/scan.h"
#include "core/settings.h"
#include "core/updater.h"
#include "media/media_scan.h"
#include "core/menus.h"
#include "core/icons.h"
#include "render/draw.h"
#include "render/text.h"
#include "render/background.h"
#include "ui/layout.h"
#include "ui/sublist.h"
#include "ui/glyphs.h"
#include "ui/info.h"
#include "ui/updatepage.h"
#include "ui/dialog.h"
#include "ui/options.h"
#include "ui/chooser.h"
#include "ui/picker.h"
#include "media/music.h"
#include "core/remote.h"
#include "ui/ime.h"

int main(void)
{
	vita2d_init();
	wave_set_model(NULL, 0);
	vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
	text_init("app0:assets/font.otf");
	load_icons();
	sound_load_all();
	SceUID athread = sceKernelCreateThread("xmb_audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if (athread >= 0) sceKernelStartThread(athread, 0, NULL);

	menus_init();
	ensure_dirs();
	config_load();
	pt_init();
	build_menus();
	for (int mi = 0; mi < M_COUNT; mi++)
		for (int ji = 0; ji < menus[mi].count; ji++)
			if (menus[mi].items[ji].stock) glow_for(menus[mi].items[ji].stock);
	/* the status pictures are read from their RCO on first use (79 ms on the device): do it before the first frame */
	res_fg("tex_mute"); res_fg("tex_mute_shadow"); res_fg("tex_busy"); res_fg("tex_busy_shadow");
	sound_play(SND_OPENING);

	int cat = CAT_GAME;
	int cur = cat;                      /* menu currently shown */
	int stack[MAX_DEPTH], depth = 0;    /* parents when inside a folder */
	float cat_pos = (float)cat_slot(cat);
	float cat_ms = 200.0f;                         /* duration of the current category move, from the theme */

	/* column transition: new list slides in from slide_dir, old one slides out */
	int prev_menu = -1;
	float in_t = 1.0f, out_t = 1.0f;
	int folder_trans = 0;               /* 1: in/out animation is a folder open/close */
	float slide_dir = 1.0f;

	for (int i = 0; i < M_COUNT; i++) menus[i].pos = (float)menus[i].sel;

	SceCtrlData pad, prev;
	memset(&pad, 0, sizeof(pad));
	memset(&prev, 0, sizeof(prev));
	int hold_frames = 0;
	unsigned int hold_btn = 0;

	float t = 0.0f;
	float folder_t = 0.0f;              /* 1 inside a game folder (wide icons) */
	float sub_t = 0.0f;                 /* 1 inside a Settings-style sub list */
	float startup_t = startup_anim ? 0.0f : 1.0f;
	const Item *launching = NULL;       /* set once Start Game has begun */
	float launch_fade = 0.0f;
	int launch_sent = 0;
	float launch_wait = 0.0f;

	/* overlays */
	int page_open = PAGE_NONE; float page_t = 0.0f;       /* System Information / game Information */
	int page_shown = PAGE_NONE;                           /* last page opened, kept while it fades out */
	int dlg_open = 0; float dlg_t = 0.0f;                 /* confirmation dialog */
	char dlg_l1[96] = "", dlg_l2[96] = "";
	int dlg_sel = 0, dlg_action = ACT_NONE;
	const Item *dlg_item = NULL;
	int opt_open = 0; float opt_t = 0.0f;                 /* Options panel */
	int opt_ids[8], opt_n = 0, opt_sel = 0;
	int opt_folder = -1;                                  /* the folder the options act on */
	const Item *opt_item = NULL;
	int opt_menu = 0;

	/* dwell background (PSP's "hold on a game" effect, using the app's pic0.png) */
	vita2d_texture *pic_tex = NULL;
	char pic_loaded[112] = "";
	float pic_alpha = 0.0f;
	const Item *dwell_item = NULL;
	float dwell_t = 0.0f;
	SceUID bgthread = sceKernelCreateThread("xmb_bg", bg_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (bgthread >= 0) sceKernelStartThread(bgthread, 0, NULL);
	SceUID decthread = sceKernelCreateThread("xmb_dec", dec_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (decthread >= 0) sceKernelStartThread(decthread, 0, NULL);
	int seen_dec_gen = 0;
	SceUID musicthread = sceKernelCreateThread("xmb_music", music_thread, 0x10000110, 0x40000, 0, 0, NULL);
	{ int sr = musicthread >= 0 ? sceKernelStartThread(musicthread, 0, NULL) : -1; trace("music thread create=%08x start=%08x\n", musicthread, sr); }
	SceUID iconthread = sceKernelCreateThread("xmb_icons", icon_thread, 0x10000110, 0x40000, 0, 0, NULL);
	if (iconthread >= 0) sceKernelStartThread(iconthread, 0, NULL);

	{
		FILE *nb = fopen(CONFIG_DIR "/noboot", "rb");      /* an empty file called noboot turns the start-up animation off */
		if (nb || !startup_anim) { if (nb) fclose(nb); }
		else {
			SceDateTime bdt;
			sceRtcGetCurrentClockLocalTime(&bdt);
			boot_month = theme ? theme - 1 : (bdt.month >= 1 && bdt.month <= 12 ? bdt.month - 1 : 0);
			Palette bp = get_palette(boot_month);
#ifdef VITAXMB_DEBUG
			FILE *bsf = fopen(CONFIG_DIR "/bootshot", "rb");      /* debug builds: save a boot frame */
			if (bsf) fclose(bsf);
#else
			FILE *bsf = NULL;
#endif
			boot_play(boot_month, boot_backdrop, bp.wave_top, bsf ? save_screenshot : NULL);
			t = boot_hand_abs ? (float)(sceKernelGetProcessTimeWide() - boot_hand_abs) / 1.0e6f : 0.0f;       /* the wave carries on in real time from where the boot began it */
		}
	}

	upd_read_result();
	for (;;) {
		uint64_t prof_t0 = sceKernelGetProcessTimeWide();
		/* ---------------- input ---------------- */
		prev = pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);

		unsigned int btn = pad.buttons;
		if (pad.lx < 60)  btn |= SCE_CTRL_LEFT;
		if (pad.lx > 195) btn |= SCE_CTRL_RIGHT;
		if (pad.ly < 60)  btn |= SCE_CTRL_UP;
		if (pad.ly > 195) btn |= SCE_CTRL_DOWN;

		unsigned int pb = prev.buttons;
		if (prev.lx < 60)  pb |= SCE_CTRL_LEFT;
		if (prev.lx > 195) pb |= SCE_CTRL_RIGHT;
		if (prev.ly < 60)  pb |= SCE_CTRL_UP;
		if (prev.ly > 195) pb |= SCE_CTRL_DOWN;
		unsigned int pressed = btn & ~pb;

		/* auto-repeat on directions */
		unsigned int dirs = btn & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN);
		if (dirs && dirs == hold_btn) {
			hold_frames++;
			if (hold_frames > 22 && (hold_frames % 5) == 0) pressed |= dirs;
		} else {
			hold_btn = dirs;
			hold_frames = 0;
		}
		pressed |= remote_step();
		if (launching || startup_t < 0.45f || ime_active) pressed = 0;

		int act = ACT_NONE;
		const Item *act_item = NULL;
		Menu *mn = &menus[cur];
		int lay = menu_layout(cur);

		if (ime_active) {
			/* ---- on-screen keyboard (naming a folder) ---- */
			char nm[48] = "";
			int r = ime_poll(nm, sizeof(nm));
			if (r != 0) {
				ime_active = 0;
				if (r > 0 && ime_purpose == IME_NEW) {
					int f = folder_new(nm);
					if (f >= 0) {
						folders_save();
						rebuild_game_lists();
						for (int i = 0; i < menus[M_MEMCARD].count; i++)
							if (menus[M_MEMCARD].items[i].kind == KIND_FOLDER && menus[M_MEMCARD].items[i].value_id == f) {
								menus[M_MEMCARD].sel = i;
								menus[M_MEMCARD].pos = (float)i;
							}
						pk_start(f);
					}
				} else if (r > 0 && ime_purpose == IME_RENAME && ime_target >= 0 && ime_target < n_folders) {
					folder_clean_name(folder_name[ime_target], sizeof(folder_name[0]), nm);
					if (!folder_name[ime_target][0]) folder_default_name(folder_name[ime_target], sizeof(folder_name[0]));
					folders_save();
					rebuild_game_lists();
				}
			}
		} else if (dlg_open) {
			/* ---- confirmation dialog ---- */
			if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { dlg_sel = !dlg_sel; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_CROSS) {
				if (dlg_sel == 0) { act = dlg_action; act_item = dlg_item; }
				else sound_play(SND_CANCEL);
				dlg_open = 0;
			} else if (pressed & SCE_CTRL_CIRCLE) {
				sound_play(SND_CANCEL);
				dlg_open = 0;
			}
		} else if (ch_open) {
			/* ---- Color / Custom Theme chooser ---- */
			if ((pressed & SCE_CTRL_UP)   && ch_sel > 0)        { ch_sel--; sound_play(SND_CURSOR); }
			if ((pressed & SCE_CTRL_DOWN) && ch_sel < ch_n - 1) { ch_sel++; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_LTRIGGER) { ch_sel = ch_sel - 5 < 0 ? 0 : ch_sel - 5; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_RTRIGGER) { ch_sel = ch_sel + 5 > ch_n - 1 ? ch_n - 1 : ch_sel + 5; sound_play(SND_CURSOR); }
			if (ch_kind == 1) theme = ch_sel;                    /* the colour shows live behind the list */
			if (pressed & SCE_CTRL_CROSS) {
				sound_play(SND_CURSOR);
				ch_apply();
				ch_open = 0;
			} else if (pressed & SCE_CTRL_CIRCLE) {
				if (ch_kind == 1) theme = ch_theme_before;
				sound_play(SND_CANCEL);
				ch_open = 0;
			}
		} else if (pk_open) {
			/* ---- folder picker ---- */
			if ((pressed & SCE_CTRL_UP)   && pk_sel > 0)         { pk_sel--; sound_play(SND_CURSOR); }
			if ((pressed & SCE_CTRL_DOWN) && pk_sel < n_all - 1) { pk_sel++; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_LTRIGGER) { pk_sel = pk_sel - 5 < 0 ? 0 : pk_sel - 5; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_RTRIGGER) { pk_sel = pk_sel + 5 > n_all - 1 ? (n_all > 0 ? n_all - 1 : 0) : pk_sel + 5; sound_play(SND_CURSOR); }
			if ((pressed & SCE_CTRL_CROSS) && n_all) { pk_on[pk_sel] = !pk_on[pk_sel]; sound_play(SND_CURSOR); }
			if (pressed & SCE_CTRL_TRIANGLE) {
				int all = pk_count() == n_all;
				for (int i = 0; i < n_all; i++) pk_on[i] = !all;
				sound_play(SND_CURSOR);
			}
			if (pressed & SCE_CTRL_CIRCLE) {
				pk_apply();
				pk_open = 0;
				sound_play(SND_CANCEL);
			}
		} else if (page_open == PAGE_PLAYER) {
			/* ---- music player ---- */
			int total = menus[M_TRACKS].count;
			if (pressed & SCE_CTRL_CROSS) { sound_play(SND_CURSOR); music_toggle(); }
			if (pressed & SCE_CTRL_CIRCLE) { sound_play(SND_CANCEL); page_open = PAGE_NONE; }
			if ((pressed & SCE_CTRL_RTRIGGER) && total) { sound_play(SND_CURSOR); music_start((music_index + 1) % total); }
			if ((pressed & SCE_CTRL_LTRIGGER) && total) { sound_play(SND_CURSOR); music_start((music_index + total - 1) % total); }
			if (pressed & SCE_CTRL_LEFT)  music_seek_rel(-10000);
			if (pressed & SCE_CTRL_RIGHT) music_seek_rel(10000);
		} else if (page_open == PAGE_UPDATE) {
			/* ---- Network Update ---- */
			if (upd_state == UPD_AVAILABLE) {
				if (pressed & SCE_CTRL_UP) upd_scroll = upd_scroll > 0 ? upd_scroll - 1 : 0;
				if (pressed & SCE_CTRL_DOWN) upd_scroll++;                                     /* clamped when drawn */
				if (pressed & SCE_CTRL_LTRIGGER) upd_scroll = upd_scroll > 5 ? upd_scroll - 5 : 0;
				if (pressed & SCE_CTRL_RTRIGGER) upd_scroll += 5;
			}
			if (pressed & SCE_CTRL_CROSS) {
				if (upd_state == UPD_AVAILABLE) { sound_play(SND_CONFIRM); upd_request(UPD_CMD_INSTALL); }
				else if (upd_state == UPD_DONE) { sound_play(SND_CONFIRM); sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL); }
				else if (upd_state == UPD_UPTODATE || upd_state == UPD_FAILED || upd_state == UPD_IDLE) { sound_play(SND_CURSOR); upd_request(UPD_CMD_CHECK); }
			}
			if (pressed & SCE_CTRL_CIRCLE) {
				sound_play(SND_CANCEL);
				if (upd_state == UPD_DOWNLOADING) upd_cancel = 1;
				else if (upd_state != UPD_INSTALLING) page_open = PAGE_NONE;
			}
		} else if (page_open != PAGE_NONE) {
			/* ---- information page ---- */
			if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) { sound_play(SND_CANCEL); page_open = PAGE_NONE; }
		} else if (opt_open) {
			/* ---- Options panel ---- */
			if (pressed & SCE_CTRL_UP)   { if (opt_sel > 0) { opt_sel--; sound_play(SND_CURSOR); } }
			if (pressed & SCE_CTRL_DOWN) { if (opt_sel < opt_n - 1) { opt_sel++; sound_play(SND_CURSOR); } }
			if (pressed & SCE_CTRL_CROSS) {
				int id = opt_ids[opt_sel];
				if (id == OPT_DELFOLDER) sound_play(SND_CONFIRM);
				else if (id != OPT_START) sound_play(SND_CURSOR);
				switch (id) {
				case OPT_START:     act = ACT_START; break;
				case OPT_INFO:      act = ACT_INFO; break;
				case OPT_NEWFOLDER: act = ACT_NEWFOLDER; break;
				case OPT_SELECT:    act = ACT_SELECT; break;
				case OPT_RENAME:    act = ACT_RENAME; break;
				case OPT_DELFOLDER: act = ACT_DELFOLDER_ASK; break;
				case OPT_REMOVE:    act = ACT_REMOVE; break;
				default:            act = ACT_REFRESH; break;
				}
				act_item = opt_item;
				opt_open = 0;
			} else if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) {
				sound_play(SND_CANCEL);
				opt_open = 0;
			}
		} else {
			/* ---- the XMB itself ---- */
			int move_cat = 0, move_item = 0, go_back = 0, cycle = 0;
			if (lay == LAY_SUB) {
				if (pressed & SCE_CTRL_LEFT)  go_back = 1;     /* the arrow points back */
				if (pressed & SCE_CTRL_RIGHT) cycle = 1;
			} else {
				if (pressed & SCE_CTRL_LEFT) {
					if (lay == LAY_GAME && depth > 0) go_back = 1;   /* the arrow at the left edge points back */
					else move_cat = -1;
				}
				if ((pressed & SCE_CTRL_RIGHT) && !(lay == LAY_GAME && depth > 0)) move_cat = 1;   /* inside a game list Right does nothing */
			}
			if (pressed & SCE_CTRL_UP)    move_item = -1;
			if (pressed & SCE_CTRL_DOWN)  move_item = 1;
			if (pressed & SCE_CTRL_LTRIGGER) move_item = -5;
			if (pressed & SCE_CTRL_RTRIGGER) move_item = 5;
			if (pressed & SCE_CTRL_CIRCLE) go_back = 1;

			if (move_cat) {
				int n = cat + move_cat;
				while (n >= 0 && n < CAT_COUNT && cat_hidden[n]) n += move_cat;      /* skip hidden categories */
				if (n >= 0 && n < CAT_COUNT) {
					cat_ms = move_cat > 0 ? pt_ms_right : pt_ms_left;
					cat = n;
					prev_menu = cur;
					cur = cat;
					depth = 0;
					slide_dir = (float)move_cat;
					folder_trans = 0;       /* the column rides along with the category bar */
					in_t = 1.0f; out_t = 1.0f;
					sound_play(SND_CATEGORY);
					mn = &menus[cur];
				}
			}
			if (move_item && mn->count) {
				int n = mn->sel + move_item;
				if (n < 0) n = 0;
				if (n >= mn->count) n = mn->count - 1;
				if (n != mn->sel) sound_play(SND_CURSOR);
				mn->sel = n;
			}

			Item *it = mn->count ? &mn->items[mn->sel] : NULL;
			if (it && ((pressed & SCE_CTRL_CROSS) || cycle)) {
				switch (it->kind) {
				case KIND_APP:
					if (pressed & SCE_CTRL_CROSS) { launching = it; sound_play(SND_STARTGAME); }
					break;
				case KIND_FOLDER:
					if ((pressed & SCE_CTRL_CROSS) && depth < MAX_DEPTH && it->submenu >= 0) {
						sound_play(SND_CURSOR);
						if (it->submenu == M_FOLDER) {          /* a user folder: fill its list first */
							open_folder = it->value_id;
							menus[M_FOLDER].sel = 0;
							fill_folder_menu(open_folder);
						}
						stack[depth++] = cur;
						prev_menu = cur;
						cur = it->submenu;
						slide_dir = 1.0f;
						folder_trans = 1;
						in_t = 0.0f; out_t = 0.0f;
					}
					break;
				case KIND_VALUE:
					sound_play(it->value_id == SET_ART && !art_decrypt ? SND_CONFIRM : SND_CURSOR);
					if (it->value_id == SET_ART && !art_decrypt) {
						act = ACT_ARTDEC_ASK;      /* kernel modules: ask first */
					} else if (it->value_id == SET_THEME || it->value_id == SET_CUSTOM) {
						ch_start(it->value_id == SET_THEME ? 1 : 0);        /* a list with a picture, like the PSP's */
					} else {
						setting_change(it->value_id, 1);
					}
					break;
				case KIND_URI:
					if (pressed & SCE_CTRL_CROSS) { sound_play(SND_CURSOR); act = ACT_URI; act_item = it; }
					break;
				case KIND_PAGE:
					if (pressed & SCE_CTRL_CROSS) {
						sound_play(SND_CURSOR);
						if (it->value_id == PAGE_SYSINFO) { sysinfo_gather(); page_open = PAGE_SYSINFO; }
						if (it->value_id == PAGE_UPDATE) {
							if (upd_state == UPD_IDLE || upd_state == UPD_UPTODATE || upd_state == UPD_FAILED) upd_request(UPD_CMD_CHECK);
							page_open = PAGE_UPDATE;
						}
					}
					break;
				case KIND_EXIT:
					if (pressed & SCE_CTRL_CROSS) { sound_play(confirm_dialogs ? SND_CONFIRM : SND_CURSOR); act = ACT_EXIT_ASK; }
					break;
				case KIND_TRACK:
					if (pressed & SCE_CTRL_CROSS) {
						sound_play(SND_CURSOR);
						if (!(music_index == mn->sel && mus_ready)) music_start(mn->sel);
						page_open = PAGE_PLAYER;
					}
					break;
				default:
					break;
				}
			}
			if (go_back) {
				if (depth > 0) {
					sound_play(SND_CANCEL);
					prev_menu = cur;
					cur = stack[--depth];
					slide_dir = -1.0f;
					folder_trans = 1;
					in_t = 0.0f; out_t = 0.0f;
				}
			}
			if ((pressed & SCE_CTRL_TRIANGLE) && it && item_has_options(cur, it)) {
				sound_play(SND_CONFIRM);
				opt_item = it;
				opt_menu = cur;
				opt_n = 0;
				opt_folder = -1;
				if (cur == M_MEMCARD && it->kind == KIND_FOLDER) {
					opt_folder = it->value_id;
					opt_ids[opt_n++] = OPT_SELECT;
					opt_ids[opt_n++] = OPT_RENAME;
					opt_ids[opt_n++] = OPT_DELFOLDER;
					opt_ids[opt_n++] = OPT_NEWFOLDER;
				} else if (cur == M_MEMCARD) {
					opt_ids[opt_n++] = OPT_START;
					opt_ids[opt_n++] = OPT_INFO;
					opt_ids[opt_n++] = OPT_NEWFOLDER;
					opt_ids[opt_n++] = OPT_REFRESH;
				} else if (cur == M_FOLDER) {
					opt_folder = open_folder;
					if (it->kind == KIND_APP) {
						opt_ids[opt_n++] = OPT_START;
						opt_ids[opt_n++] = OPT_INFO;
						opt_ids[opt_n++] = OPT_REMOVE;
					}
					opt_ids[opt_n++] = OPT_SELECT;
					opt_ids[opt_n++] = OPT_RENAME;
					opt_ids[opt_n++] = OPT_DELFOLDER;
				} else {
					opt_ids[opt_n++] = OPT_INFO;
					opt_ids[opt_n++] = OPT_REFRESH;
				}
				opt_sel = 0;
				opt_open = 1;
				sound_play(SND_OPTION);
			}
		}

		if (rc_uri[0]) {                                     /* debug: try a launch flag / URI pair and log the result */
			char *colon = strchr(rc_uri, ':');
			if (colon) {
				*colon = 0;
				unsigned fl = (unsigned)strtoul(rc_uri, NULL, 16);
				int r;
				if (!strcmp(rc_uri, "N")) {                      /* N:<titleid> -> sceAppMgrLaunchAppByName2 */
					r = sceAppMgrLaunchAppByName2(colon + 1, NULL, NULL);
					trace("TEST byname2 %s -> %08x\n", colon + 1, r);
				} else if (rc_uri[0] == 'B') {                   /* B<flags>:<titleid> -> sceAppMgrLaunchAppByName */
					fl = (unsigned)strtoul(rc_uri + 1, NULL, 16);
					r = sceAppMgrLaunchAppByName((int)fl, colon + 1, "");
					trace("TEST byname flags=%x %s -> %08x\n", fl, colon + 1, r);
				} else {
					r = sceAppMgrLaunchAppByUri((int)fl, colon + 1);
					trace("TEST flags=%x uri=%s -> %08x\n", fl, colon + 1, r);
				}
			}
			rc_uri[0] = 0;
		}

		/* ---- carry out whatever the UI asked for ---- */
		if (act == ACT_EXIT_ASK) {
			if (confirm_dialogs) {
				snprintf(dlg_l1, sizeof(dlg_l1), "Exit VitaXMB and return to the LiveArea?");
				dlg_l2[0] = 0;
				dlg_action = ACT_EXIT_DO; dlg_item = NULL; dlg_sel = 0; dlg_open = 1;
				act = ACT_NONE;
			} else {
				act = ACT_EXIT_DO;
			}
		} else if (act == ACT_DELFOLDER_ASK) {
			if (opt_folder >= 0 && opt_folder < n_folders) {
				snprintf(dlg_l1, sizeof(dlg_l1), "Delete the folder \"%.40s\"?", folder_name[opt_folder]);
				snprintf(dlg_l2, sizeof(dlg_l2), "The games stay installed.");
				dlg_action = ACT_DELFOLDER_DO; dlg_item = NULL; dlg_sel = 1; dlg_open = 1;
			}
			act = ACT_NONE;
		} else if (act == ACT_ARTDEC_ASK) {
			snprintf(dlg_l1, sizeof(dlg_l1), "Decrypt game artwork?");
			snprintf(dlg_l2, sizeof(dlg_l2), "Loads VitaShell's kernel modules. Experimental.");
			dlg_action = ACT_ARTDEC_DO; dlg_item = NULL; dlg_sel = 1; dlg_open = 1;
			act = ACT_NONE;
		}

		switch (act) {
		case ACT_EXIT_DO:
			sceKernelDelayThread(300 * 1000);
			audio_run = 0;
			vs_shutdown();
			sceKernelExitProcess(0);
			break;
		case ACT_ARTDEC_DO:
			setting_change(SET_ART, 1);
			break;
		case ACT_URI:
			if (act_item) launch_uri(act_item->uri);
			break;
		case ACT_START:
			if (act_item) { launching = act_item; sound_play(SND_STARTGAME); }
			break;
		case ACT_INFO:
			if (act_item) { gameinfo_gather(act_item, opt_menu == M_SAVES); page_open = PAGE_GAMEINFO; }
			break;
		case ACT_NEWFOLDER: {
			char nm[40];
			folder_default_name(nm, sizeof(nm));
			ime_begin("New folder", nm, IME_NEW, -1);
			break; }
		case ACT_SELECT:
			if (opt_folder >= 0 && opt_folder < n_folders) pk_start(opt_folder);
			break;
		case ACT_RENAME:
			if (opt_folder >= 0 && opt_folder < n_folders) ime_begin("Rename folder", folder_name[opt_folder], IME_RENAME, opt_folder);
			break;
		case ACT_DELFOLDER_DO:
			if (opt_folder >= 0 && opt_folder < n_folders) {
				int leaving = cur == M_FOLDER;
				folder_delete(opt_folder);
				open_folder = -1;
				folders_save();
				rebuild_game_lists();
				if (leaving && depth > 0) {
					prev_menu = cur;
					cur = stack[--depth];
					slide_dir = -1.0f;
					folder_trans = 1;
					in_t = 0.0f; out_t = 0.0f;
				}
			}
			break;
		case ACT_REMOVE:
			if (act_item) {
				char rid[16];
				snprintf(rid, sizeof(rid), "%s", act_item->id);
				app_set_folder(rid, -1);
				folders_save();
				rebuild_game_lists();
			}
			break;
		case ACT_REFRESH:
			if (opt_menu == M_MEMCARD) { scan_apps(); update_game_counts(); menus[M_MEMCARD].pos = (float)menus[M_MEMCARD].sel; }
			if (opt_menu == M_SAVES)   { scan_saves(); update_game_counts(); menus[M_SAVES].pos = (float)menus[M_SAVES].sel; }
			break;
		default: break;
		}

		lay = menu_layout(cur);
		mn = &menus[cur];
		uint64_t prof_t1 = sceKernelGetProcessTimeWide();
		update_icons(cur, out_t < 1.0f ? prev_menu : -1);
		uint64_t prof_t2 = sceKernelGetProcessTimeWide();

		/* freshly decrypted artwork: forget the "missing" result and look again */
		if (dec_gen != seen_dec_gen) {
			seen_dec_gen = dec_gen;
			Menu *am = &menus[M_MEMCARD];
			for (int i = 0; i < am->count; i++) {
				Item *ai = &am->items[i];
				if (!ai->dec_queued) continue;
				ai->meta_resolved = 0;
				ai->gate_path[0] = ai->bg_path[0] = 0;
				if (ai->icon_rect && ai->icon) { defer_free(ai->icon); ai->icon = NULL; }
				ai->icon_rect = 0;
				ai->icon_tried = 0;
				ai->load_state = 0;
			}
			pic_loaded[0] = 0;
		}

		/* ---------------- dwell background ---------------- */
		{
			const Item *sel_it = menus[cur].count ? &menus[cur].items[menus[cur].sel] : NULL;
			if (sel_it != dwell_item) { dwell_item = sel_it; dwell_t = 0.0f; }
			else dwell_t += 1.0f / 60.0f;

			const char *want = NULL;
			if (dwell_item && dwell_item->kind == KIND_APP && !launching && dwell_t > 0.3f && !opt_open && page_open == PAGE_NONE) {
				Item *wi = (Item *)dwell_item;
				if (wi->meta_resolved == 2)
					want = wi->bg_path[0] ? wi->bg_path : (wi->pic_path[0] ? wi->pic_path : NULL);
			}
			int showing = want && pic_tex && strcmp(want, pic_loaded) == 0;

			/* pick up a finished decode */
			if (bg_state == 3) {
				if (bg_pix) {
					vita2d_texture *nt = texture_from_rgba(bg_pix, bg_w, bg_h);
					free(bg_pix); bg_pix = NULL;
					if (nt) {
						if (pic_tex) defer_free(pic_tex);
						pic_tex = nt;
						snprintf(pic_loaded, sizeof(pic_loaded), "%s", bg_done_path);
					}
				} else {
					/* failed: drop the previous texture so another game's image can't linger */
					if (pic_tex) { defer_free(pic_tex); pic_tex = NULL; }
					snprintf(pic_loaded, sizeof(pic_loaded), "%s", bg_done_path);
				}
				bg_state = 0;
			}
			/* after holding on a title for a moment, fetch its image */
			if (want && dwell_t > 0.5f && bg_state == 0 && strcmp(want, pic_loaded) != 0 && pic_alpha < 0.05f) {
				snprintf(bg_req_path, sizeof(bg_req_path), "%s", want);
				bg_state = 1;
			}
			if (dwell_item && dwell_item->kind == KIND_APP && !showing && want && !pic_tex &&
			    strcmp(want, pic_loaded) == 0 && bg_state == 0 &&
			    dwell_item->pic_path[0] && strcmp(want, dwell_item->pic_path) != 0 && dwell_t > 0.5f) {
				/* LiveArea background failed to decode: fall back to pic0.png */
				snprintf(bg_req_path, sizeof(bg_req_path), "%s", dwell_item->pic_path);
				bg_state = 1;
			}
			float target = (showing && dwell_t > 0.5f) ? 1.0f : 0.0f;
			pic_alpha = lerpf(pic_alpha, target, target > pic_alpha ? 0.07f : 0.18f);
			if (pic_alpha < 0.002f) pic_alpha = 0.0f;
		}

		/* ---------------- music upkeep ---------------- */
		if (music_index >= 0 && mus_ready && mus_eof && mus_w == mus_r) {      /* track finished: next one */
			int total = menus[M_TRACKS].count;
			if (total > 0) music_start((music_index + 1) % total);
		}
		if (page_open != PAGE_NONE) page_shown = page_open;
		if (page_shown == PAGE_PLAYER && page_t > 0.0f) spectrum_update(mus_playing && mus_ready);

		/* ---------------- animation ---------------- */
		const float dt_s = 1.0f / 60.0f;
		if (upd_handover) {                                         /* an update is ready: start the updater app the way a game is started */
			upd_handover = 0;
			sceKernelDelayThread(10000);
			int hr = sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=VXMBUPDTR");
			trace("update: handing over to the updater app (%08x)\n", (unsigned)hr);
			sceKernelDelayThread(1000);
			audio_run = 0;
			sceKernelExitProcess(0);
		}
		{	/* updates: look once a few seconds after start, keep the Settings row's note current */
			static int auto_started;
			static float wait_s;
			if (!auto_started && auto_update) {
				wait_s += dt_s;
				if (wait_s > 4.0f && !launching) { auto_started = 1; upd_request(UPD_CMD_CHECK); }
			}
			for (int i = 0; i < menus[CAT_SETTINGS].count; i++) {
				Item *ui = &menus[CAT_SETTINGS].items[i];
				if (ui->kind == KIND_PAGE && ui->value_id == PAGE_UPDATE) {
					if (upd_found) snprintf(ui->sub, sizeof(ui->sub), "Update available: %s", upd_tag); else ui->sub[0] = 0;
				}
			}
		}
		{
			float want_pos = (float)cat_slot(cat);
			/* the theme's own duration for a category change (200 ms by default): about a quarter of it is the time constant */
			cat_pos = lerpf(cat_pos, want_pos, ease4_step(cat_ms));          /* the engine's own curve for it (paf.h) */
			if (fabsf(cat_pos - want_pos) < 0.004f) cat_pos = want_pos;
		}
		for (int i = 0; i < M_COUNT; i++) {
			/* a list scrolls with the same curve; 200 ms each way unless the theme patches the XList's two durations */
			menus[i].pos = lerpf(menus[i].pos, (float)menus[i].sel, ease4_step(menus[i].sel > menus[i].pos ? xl_ms_down : xl_ms_up));
			if (fabsf(menus[i].pos - menus[i].sel) < 0.004f) menus[i].pos = (float)menus[i].sel;
		}
		for (int mi = 0; mi < M_COUNT; mi++) {
			Menu *gm = &menus[mi];
			for (int ji = 0; ji < gm->count; ji++) {
				Item *gi = &gm->items[ji];
				if (ji != gm->sel && gi->glow == 0.0f) continue;             /* the game lists can have thousands of rows: skip the resting ones */
				float tg = ji == gm->sel ? 1.0f : 0.0f;
				gi->glow += (tg - gi->glow) * ease4_step(200.0f);
				if (fabsf(tg - gi->glow) < 0.01f) gi->glow = tg;
			}
		}
		in_t  = clampf(in_t  + dt_s / 0.30f, 0.0f, 1.0f);
		out_t = clampf(out_t + dt_s / 0.22f, 0.0f, 1.0f);
		t += dt_s;
		startup_t = clampf(startup_t + dt_s / 2.2f, 0.0f, 1.0f);
		{ static unsigned frame; if ((++frame % 300) == 0) trace("frame %u menu=%d sel=%d\n", frame, cur, menus[cur].sel); }
		folder_t = lerpf(folder_t, lay == LAY_GAME ? 1.0f : 0.0f, 0.2f);
		if (fabsf(folder_t - (lay == LAY_GAME)) < 0.002f) folder_t = (float)(lay == LAY_GAME);
		sub_t = lerpf(sub_t, lay == LAY_SUB ? 1.0f : 0.0f, 0.2f);
		if (fabsf(sub_t - (lay == LAY_SUB)) < 0.002f) sub_t = (float)(lay == LAY_SUB);
		page_t = lerpf(page_t, page_open != PAGE_NONE ? 1.0f : 0.0f, page_open != PAGE_NONE ? 0.25f : 0.45f);
		if (page_t < 0.01f) page_t = 0.0f; else if (page_t > 0.99f) page_t = 1.0f;
		dlg_t = lerpf(dlg_t, dlg_open ? 1.0f : 0.0f, 0.3f);
		if (dlg_t < 0.01f) dlg_t = 0.0f; else if (dlg_t > 0.99f) dlg_t = 1.0f;
		ch_t = lerpf(ch_t, ch_open ? 1.0f : 0.0f, 0.3f);
		if (ch_t < 0.01f) ch_t = 0.0f; else if (ch_t > 0.99f) ch_t = 1.0f;
		pk_t = lerpf(pk_t, pk_open ? 1.0f : 0.0f, 0.3f);
		if (pk_t < 0.01f) pk_t = 0.0f; else if (pk_t > 0.99f) pk_t = 1.0f;
		{
			float want = pk_sel - 4.0f;
			float maxtop = n_all - 9.0f;
			if (want > maxtop) want = maxtop;
			if (want < 0.0f) want = 0.0f;
			pk_top = lerpf(pk_top, want, 0.3f);
		}
		opt_t = lerpf(opt_t, opt_open ? 1.0f : 0.0f, 0.3f);
		if (opt_t < 0.01f) opt_t = 0.0f; else if (opt_t > 0.99f) opt_t = 1.0f;
		if (launching) {
			launch_fade = clampf(launch_fade + dt_s / 1.2f, 0.0f, 1.0f);
			if (!launch_sent && !sound_playing(SND_STARTGAME)) {
				audio_run = 0;
				launch_request(launching);
				launch_sent = 1;
			}
			if (launch_sent) {
				launch_wait += dt_s;
				if (launch_wait > 10.0f) sceKernelExitProcess(0);   /* launch failed: give up */
			}
		}

		SceDateTime dt;
		sceRtcGetCurrentClockLocalTime(&dt);
		int month = theme ? theme - 1 : (dt.month >= 1 && dt.month <= 12 ? dt.month - 1 : 0);
		Palette pal = get_palette(month);

		/* ---------------- draw ---------------- */
		flush_free();
		text_frame_begin();
		vita2d_start_drawing();
		vita2d_clear_screen();

		bg_alpha = 1.0f;
		draw_background(t, month);
		if (pic_tex && pic_alpha > 0.0f) {
			/* cover-fit: fill the screen, cropping the overflow evenly */
			float tw = vita2d_texture_get_width(pic_tex), th = vita2d_texture_get_height(pic_tex);
			float sc = fmaxf(SCREEN_W / tw, SCREEN_H / th);
			vita2d_draw_texture_tint_scale(pic_tex, (SCREEN_W - tw * sc) / 2, (SCREEN_H - th * sc) / 2,
			                               sc, sc, RGBA8(255, 255, 255, (int)(255 * pic_alpha)));
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(70 * pic_alpha)));
		}
		if (!pt_blade_mode) draw_status(&dt);          /* with full-height panels the status bar goes on top of them, below */

		xf_on = xf_set_up;                           /* a theme may turn the whole XMB in space (xform.h) */
		/* startup: the bar slides in from the left, the lists rise after it */
		float su_bar = ease_out(clampf((startup_t - 0.10f) / 0.55f, 0.0f, 1.0f));

		/* Category row: the open category slides left for sub lists / game folders, the rest fade */
		float fe = ease_out(folder_t), se = ease_out(sub_t);
		float hide = fmaxf(fe, se);
		if (pt_blade_mode) {
			/* A theme that draws the category bar as full-height panels (one per category, the tabs painted in).
			 * They are placed like the icons would be: at the bar's anchor plus the category's distance times the
			 * theme's spacing, so the neighbours' panels reach in from the screen edges and slide as you move. */
			for (int i = 0; i < CAT_COUNT; i++) {
				if (cat_hidden[i] || !pt_blade[i]) continue;
				float d = cat_slot(i) - cat_pos;
				int w = vita2d_texture_get_width(pt_blade[i]), h = vita2d_texture_get_height(pt_blade[i]);
				float cx = CAT_X + pt_blade_dx + CAT_OFFSET(d);
				if (cx - w >= SCREEN_W || cx + w <= 0) continue;
				vita2d_draw_texture_tint_scale(pt_blade[i], cx - w, 272.0f - h, 2.0f, 2.0f, RGBA8(255, 255, 255, (int)(255 * su_bar)));
			}
			draw_status(&dt);
		}
		if (pt_strip_mode) {
			/* A theme whose category bar is one wide strip per category (all labels painted on it, the open one
			 * lit): the strip of the open category lies along the bottom edge, fading into the next. */
			for (int i = 0; i < CAT_COUNT; i++) {
				if (cat_hidden[i] || !pt_strip[i]) continue;
				float d = fabsf(cat_slot(i) - cat_pos);
				if (d >= 1.0f) continue;
				int w = vita2d_texture_get_width(pt_strip[i]), h = vita2d_texture_get_height(pt_strip[i]);
				vita2d_draw_texture_tint_scale(pt_strip[i], (SCREEN_W - 2.0f * w) / 2.0f, SCREEN_H - 2.0f * h, 2.0f, 2.0f,
				                               RGBA8(255, 255, 255, (int)(255 * (1.0f - d) * su_bar)));
			}
		}
		for (int i = 0; i < CAT_COUNT; i++) {
			if (cat_hidden[i]) continue;
			if (pt_blade_mode || pt_strip_mode) break;
			float d = cat_slot(i) - cat_pos;
			float x = CAT_X + pt_row_dx + CAT_OFFSET(d) - 196.0f * pt_fold_ratio * fe - 118.0f * pt_sub_ratio * se;
			x -= (1.0f - su_bar) * 240.0f;
			float sel = 1.0f - clampf(fabsf(d), 0.0f, 1.0f);
			float size = lerpf(98.0f, 120.0f, sel) * pt_menu_scale;
			int a = (int)(lerpf(165, 255, sel) * su_bar);
			if (i != cat) a = (int)(a * (1.0f - hide));         /* only the open category stays */
			if (a > 2) draw_icon(pt_cat[i] ? pt_cat[i] : cat_tex[i], x, CAT_Y, size, a);
			if (sel > 0.3f)
				ptext_vc(x - ptext_width(22, cat_names[i]) / 2.0f, CAT_Y + 58,
				         WHITE((int)(255 * sel * (1.0f - fe) * su_bar)), 22, cat_names[i]);
		}

		/* Lists. Each column is pinned to its category's x, so it travels with the bar.
		 * Opening/closing a folder adds a short slide on top. */
		{
			const float slide_px = 110.0f;
			int owner_cur = menu_owner(cur);
			float dc = cat_slot(owner_cur) - cat_pos;
			float cur_x = pt_row_dx + CAT_OFFSET(dc);
			float cur_a = (1.0f - clampf(fabsf(dc), 0.0f, 1.0f)) * su_bar;
			cur_x -= (1.0f - su_bar) * 240.0f;

			if (lay == LAY_SUB) {
				int par = depth > 0 ? stack[depth - 1] : menu_owner(cur);
				draw_column(par, cur_x, cur_a, 0.0f, sub_t);                 /* icon-only parent column */
				float cx = (folder_trans ? slide_dir * slide_px * (1.0f - ease_out(in_t)) : 0.0f) - (1.0f - su_bar) * 240.0f;
				draw_sub_list(cur, cx, folder_trans ? ease_out(in_t) : 1.0f);
				if (prev_menu >= 0 && menu_layout(prev_menu) == LAY_SUB && out_t < 1.0f)
					draw_sub_list(prev_menu, -slide_dir * slide_px * ease_out(out_t), 1.0f - ease_out(out_t));
				if (out_t >= 1.0f || prev_menu == par) prev_menu = -1;
			} else {
				if (prev_menu >= 0) {
					int owner_prev = menu_owner(prev_menu);
					float dp = cat_slot(owner_prev) - cat_pos;
					float px = pt_row_dx + CAT_OFFSET(dp);
					float pa = 1.0f - clampf(fabsf(dp), 0.0f, 1.0f);
					if (folder_trans && owner_prev == owner_cur) {
						px += -slide_dir * slide_px * ease_out(out_t);
						pa *= 1.0f - ease_out(out_t);
					}
					if (menu_layout(prev_menu) == LAY_SUB) draw_sub_list(prev_menu, px, pa);
					else draw_column(prev_menu, px, pa, 0.0f, 0.0f);
					if (pa < 0.01f || (folder_trans && out_t >= 1.0f)) prev_menu = -1;
				}
				if (folder_trans) {
					cur_x += slide_dir * slide_px * (1.0f - ease_out(in_t));
					cur_a *= ease_out(in_t);
				}
				draw_column(cur, cur_x, cur_a, pic_alpha, sub_t);
			}
		}

		if (folder_t > 0.05f) {                       /* "back" arrow at the left edge of a game folder */
			int ba = (int)(230 * folder_t);
			for (int i = 0; i < 18; i++)
				vita2d_draw_rectangle(10 + i * 0.7f, 272 - (18 - i) * 0.55f - 0.0f, 1.0f, (18 - i) * 1.1f, WHITE(ba));
		}
		if (sub_t > 0.05f) glyph_arrow_left(205, 270, 20, (int)(235 * sub_t));   /* ◀ beside the sub list */
		xf_on = 0;

		/* "Options" pill, only where the triangle does something */
		{
			const Item *sit = menus[cur].count ? &menus[cur].items[menus[cur].sel] : NULL;
			if (sit && item_has_options(cur, sit) && !dlg_open && !pk_open && !ch_open && !ime_active && page_open == PAGE_NONE && !launching)
				draw_options_pill((int)(255 * (1.0f - opt_t) * su_bar));
		}

		/* full-page information screens replace the XMB (the waves stay) */
		if (page_t > 0.0f) {
			if (page_shown == PAGE_PLAYER) {
				draw_player(page_t, page_open != PAGE_PLAYER);
			} else {
				bg_alpha = page_t;
				draw_background(t, month);
				bg_alpha = 1.0f;
				draw_status(&dt);
				if (page_shown == PAGE_UPDATE) draw_update_page(page_t); else draw_info_page(page_t);
			}
		}

		if (opt_t > 0.0f) {
				int octx = opt_menu == M_VIDEOS ? POPT_VIDEO : (opt_menu == M_TRACKS ? POPT_MUSIC : (opt_menu == M_SAVES ? POPT_ETC : POPT_GAME));
				for (int oi = 0; oi < opt_n; oi++) if (opt_ids[oi] == OPT_DELFOLDER) octx = POPT_FOLDER;          /* a folder's own Options */
				draw_options_panel(opt_t, opt_ids, opt_n, opt_sel, &pal, octx);
			}
		if (pk_t > 0.0f) draw_picker(pk_t);
		if (ch_t > 0.0f) draw_chooser(ch_t, &pal);
		if (dlg_t > 0.0f) draw_dialog(dlg_t, dlg_l1, dlg_l2, dlg_sel);

		if (test_page) {
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(40, 40, 48, 255));
			text_flat = 1;
			draw_text_grid();
			text_flat = 0;
		}

		status_busy = launching != NULL || upd_state == UPD_DOWNLOADING || upd_state == UPD_INSTALLING;
		draw_busy(dt_s);                                                  /* the PSP's spinner, bottom right */
		{ static float notice_s; if (page_open == PAGE_NONE && !launching) draw_update_notice(dt_s, &notice_s); }
		if (launch_fade > 0.0f)
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(255 * launch_fade)));
		if (startup_t < 0.6f && !boot_played)         /* fade in from black */
			vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, (int)(255 * (1.0f - ease_out(startup_t / 0.6f)))));

		vita2d_end_drawing();
		if (ime_active && !ime_fake) vita2d_common_dialog_update();
		uint64_t prof_t3 = sceKernelGetProcessTimeWide();
		vita2d_swap_buffers();
		uint64_t prof_t4 = sceKernelGetProcessTimeWide();
		if (prof_t4 - prof_t0 > 24000)
			trace("slow frame %u us: input=%u icons=%u draw=%u swap=%u\n", (unsigned)(prof_t4 - prof_t0),
			      (unsigned)(prof_t1 - prof_t0), (unsigned)(prof_t2 - prof_t1), (unsigned)(prof_t3 - prof_t2), (unsigned)(prof_t4 - prof_t3));
		if (rc_shot[0]) { sceKernelDelayThread(50000); save_screenshot(rc_shot); rc_shot[0] = 0; }
	}

	return 0;
}
