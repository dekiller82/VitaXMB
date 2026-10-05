#pragma once

/* ------------------------------------------------------------------ */
/* Data                                                                */
/* ------------------------------------------------------------------ */

enum { CAT_SETTINGS, CAT_PHOTO, CAT_MUSIC, CAT_VIDEO, CAT_GAME, CAT_NETWORK, CAT_COUNT };
static const char *cat_names[CAT_COUNT] = { "Settings", "Photo", "Music", "Video", "Game", "Network" };

enum { KIND_INFO, KIND_APP, KIND_EXIT, KIND_FOLDER, KIND_VALUE, KIND_URI, KIND_PAGE, KIND_TRACK };
/* values for KIND_VALUE rows */
enum { SET_THEME, SET_CLOCK, SET_SOUND, SET_STARTUP, SET_CONFIRM, SET_LAUNCH, SET_ART, SET_EXTRA, SET_CUSTOM, SET_AUTOUPDATE, SET_SHOW };   /* SET_SHOW + category: show / hide that category */
/* values for KIND_PAGE rows */
enum { PAGE_NONE, PAGE_SYSINFO, PAGE_GAMEINFO, PAGE_PLAYER, PAGE_UPDATE };

/* Menus: the first CAT_COUNT are the category roots, the rest are nested lists. */
enum { M_MEMCARD = CAT_COUNT, M_SAVES, M_FOLDER, M_VITAXMB, M_SYSSET, M_THEMESET, M_NETSET, M_VIDEOS, M_TRACKS, M_COUNT };
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
	char path[112];            /* files: where it lives (videos, ...) */
} Item;

typedef struct {
	Item items[MAX_ITEMS];
	int count;
	int sel;
	float pos;              /* animated selection position */
} Menu;

static Menu menus[M_COUNT];

/* Theme: "monthly" (0) follows the PSP behaviour, 1..12 forces a month. */
static int theme = 0;
static int launch_mode = 0;   /* see launch_request() */
static int art_decrypt = 0;   /* opt-in: decrypt missing LiveArea art via VitaShell modules */
static int clock24 = 1;       /* 24-hour clock like the PSP capture */
static int sound_on = 1;      /* UI sound effects */
static int auto_update = 1;   /* look for a newer VitaXMB at start-up */
static int startup_anim = 1;  /* fade-in / slide-in at launch */
static int confirm_dialogs = 1; /* ask before leaving the XMB */
static int cat_hidden[CAT_COUNT];  /* categories the user hid (Settings and Game can't be hidden) */
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
