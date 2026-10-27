#pragma once

/* ------------------------------------------------------------------ */
/* Data                                                                */
/* ------------------------------------------------------------------ */

#define N_CUSTOM_COLS 22                       /* the columns the user can make (one bit each in the hidden mask of config.bin: CAT_COUNT must stay at 30 or below) */
enum { CAT_SETTINGS, CAT_PHOTO, CAT_MUSIC, CAT_VIDEO, CAT_GAME, CAT_NETWORK, CAT_EMU, CAT_UTIL, CAT_C1, CAT_C2, CAT_C3, CAT_C4, CAT_C5, CAT_C6, CAT_C7, CAT_C8, CAT_C9, CAT_C10, CAT_C11, CAT_C12, CAT_C13, CAT_C14, CAT_C15, CAT_C16, CAT_C17, CAT_C18, CAT_C19, CAT_C20, CAT_C21, CAT_C22, CAT_COUNT };      /* Emulators and Utilities appear once something is in them; Column 1-22 are the user's own */
static const char *cat_names[CAT_COUNT] = { "Settings", "Photo", "Music", "Video", "Game", "Network", "Emulators", "Utilities", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column", "Column" };

enum { KIND_INFO, KIND_APP, KIND_EXIT, KIND_FOLDER, KIND_VALUE, KIND_URI, KIND_PAGE, KIND_TRACK, KIND_VIDEO };
/* values for KIND_VALUE rows */
enum { SET_THEME, SET_CLOCK, SET_SOUND, SET_STARTUP, SET_CONFIRM, SET_LAUNCH, SET_ART, SET_EXTRA, SET_CUSTOM, SET_AUTOUPDATE, SET_BATTPCT, SET_NOLA, SET_BOOTLOGO, SET_LANG, SET_ORGANIZE, SET_SORT, SET_COLUMNS, SET_RETRO, SET_SHOW };   /* SET_SHOW + category: show / hide that category */
/* values for KIND_PAGE rows */
enum { PAGE_NONE, PAGE_SYSINFO, PAGE_GAMEINFO, PAGE_PLAYER, PAGE_UPDATE, PAGE_VIDEO, PAGE_TROPHY };

/* Menus: the first CAT_COUNT are the category roots, the rest are nested lists. */
enum { M_MEMCARD = CAT_COUNT, M_SAVES, M_FOLDER, M_VCOL, M_VITAXMB, M_SYSSET, M_THEMESET, M_NETSET, M_VIDEOS, M_TRACKS, M_TROPHY, M_COUNT };
enum { LAY_COLUMN, LAY_GAME, LAY_SUB };
#define MAX_DEPTH 4

typedef struct {
	char title[64];
	char sub[48];
	char id[16];
	char icon_path[112];    /* optional icon file shown for this row */
	char icon_path2[112];   /* fallback icon file */
	char pic_path[112];     /* fallback full-screen background (pic0.png) */
	char meta_dir[64];      /* ur0:appmeta/<id>/livearea/contents/ for installed apps */
	char gate_path[112];    /* LiveArea gate (rectangle) image */
	char bg_path[112];      /* LiveArea background image */
	int meta_resolved;
	int dec_queued;         /* artwork decryption was requested for this app */
	int icon_rect;          /* icon texture is the rectangular gate image */
	int kind;
	int submenu;            /* KIND_FOLDER: menu to open */
	vita2d_texture *icon;   /* loaded from icon_path, owned */
	vita2d_texture *stock;  /* shared XMB icon, not owned */
	int icon_tried;
	volatile int load_state;   /* 0 idle, 1 wanted, 2 loading (worker), 3 decoded, 4 done */
	uint8_t *pending_pix;      /* decoded RGBA waiting for upload on the render thread */
	int pending_w, pending_h, pending_rect;
	int value_id;              /* KIND_VALUE: SET_*, KIND_PAGE: PAGE_* */
	float glow;                /* 0..1, eases to 1 on the selected row (snappier than the scroll animation) */
	char uri[96];              /* KIND_URI: what to launch */
	char path[256];            /* files: where it lives (videos, ...) */
	int tr;                    /* 1: the title, 2: the subtitle is one of the UI's own texts, shown through T() */
	int org;                   /* where a game comes from: 1 PSP and 2 PS1 (Adrenaline's folders, by the PARAM.SFO CATEGORY), 3 PSM, ORG_RETRO0 + n a retro game of system n */
	int rix;                   /* a retro game: its record in retro_rec[] plus one (0: not a retro game) */
} Item;
#define ORG_PSP 1
#define ORG_PS1 2
#define ORG_PSM 3
#define ORG_RETRO0 16

static int items_tr;           /* add_item gives new items this tr */
static const char *T(const char *en);
static const char *item_title(const Item *it) { return (it->tr & 1) ? T(it->title) : it->title; }
static const char *item_sub(const Item *it) { return (it->tr & 2) ? T(it->sub) : it->sub; }

typedef struct {
	Item *items;            /* heap array of cap entries (menus_init) */
	int cap;
	int count;
	int sel;
	float pos;              /* animated selection position */
} Menu;

static Menu menus[M_COUNT];

/* Lists that follow the size of the library get GAME_MAX rows, every other menu MAX_ITEMS. The arrays are
 * allocated once and never move, because the icon thread keeps pointers into them. */
static void menus_init(void)
{
	for (int m = 0; m < M_COUNT; m++) {
		int big = m == M_MEMCARD || m == M_FOLDER || m == M_VCOL;
		int mid = m == M_TROPHY;
		menus[m].cap = big ? GAME_MAX : (m == M_SAVES ? SAVES_MAX : (mid ? 512 : MAX_ITEMS));
		menus[m].items = (Item *)calloc(menus[m].cap, sizeof(Item));
		if (!menus[m].items) menus[m].cap = 0;
	}
}

/* Theme: "monthly" (0) follows the PSP behaviour, 1..12 forces a month. */
static int theme = 0;
static int launch_mode = 0;   /* see launch_request() */
static int art_decrypt = 0;   /* opt-in: decrypt missing LiveArea art via VitaShell modules */
static int clock24 = 1;       /* 24-hour clock like the PSP capture */
static int sound_on = 1;      /* UI sound effects */
static int nla_asked = 0;     /* the first-run offer of the NoLiveArea plugin was shown */
static int nla_on = 0;        /* the NoLiveArea plugin is installed (its line is in ur0:tai/config.txt) */
static int batt_pct = 0;      /* show the battery's percentage beside the clock */
static int auto_update = 1;   /* look for a newer VitaXMB at start-up */
static int boot_logo = 1;     /* game boot movie logo: 1 the Vita picture (default), 0 the PSP one */
static int startup_anim = 1;  /* fade-in / slide-in at launch */
static int confirm_dialogs = 1; /* ask before leaving the XMB */
static int cat_hidden[CAT_COUNT];  /* categories that are not on the bar now: the user hid them, or an automatic column is empty (cat_refresh_hidden) */
static int cat_order[CAT_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 };   /* the columns left to right (the user can rearrange them) */
static int col_custom[CAT_COUNT];  /* 1: a column the user made (Column 1-22 only) */
static char col_title[CAT_COUNT][32];   /* a name the user gave (empty: the default) */
static int col_icon[CAT_COUNT] = { [0 ... CAT_COUNT - 1] = -1 };   /* index in col_icons[] the user picked (-1: the column's own picture) */
static int col_used[CAT_COUNT];    /* apps and folders placed in each column (an automatic column with none is off the bar) */
/* what a column is called: the user's name, else its own */
static const char *col_name(int c) { return col_title[c][0] ? col_title[c] : T(cat_names[c]); }
static int cat_pref[CAT_COUNT];    /* the user's choice: 1 hidden (Settings and Game can't be hidden) */
static int cat_now = CAT_GAME;     /* the category shown, kept up to date by main: an emptied automatic column stays until it is left */
static int sort_asked;             /* the offer to sort the apps into columns was shown */
static int retro_emu;              /* the launcher of retro games: 0 RetroArch, 1 Emu4Vita+ */
static int extra_storage = 0;   /* EXPERIMENTAL: also look on a second card (uma0:, imc0:, xmc0:, grw0:) */

/* Base colours per month, as {top, bottom} RGB. */
static const unsigned char month_cols[12][6] = {
	{ 0xc8,0x9a,0x2a,  0x5a,0x3a,0x08 }, /* Jan */
	{ 0x9a,0x7a,0xc8,  0x3a,0x2a,0x68 }, /* Feb */
	{ 0x6a,0xc0,0x7a,  0x1a,0x58,0x34 }, /* Mar */
	{ 0xe8,0x8a,0xb0,  0x7a,0x2a,0x50 }, /* Apr */
	{ 0x4a,0xc0,0x4a,  0x0a,0x4a,0x18 }, /* May */
	{ 0x4a,0xd0,0xc0,  0x0a,0x54,0x6a }, /* Jun */
	{ 0x3a,0x90,0xe8,  0x08,0x2a,0x7a }, /* Jul */
	{ 0x30,0x70,0xd8,  0x08,0x20,0x6a }, /* Aug */
	{ 0xe0,0xa0,0x30,  0x6a,0x30,0x08 }, /* Sep */
	{ 0xe0,0x70,0x30,  0x6a,0x20,0x08 }, /* Oct */
	{ 0x90,0x60,0x40,  0x38,0x20,0x14 }, /* Nov */
	{ 0xd8,0x40,0x50,  0x62,0x10,0x1c }, /* Dec */
};
