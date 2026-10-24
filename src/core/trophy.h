#pragma once

/*
 * trophy.h - trophy progress without going through LiveArea.
 *
 * The system keeps every game's trophies in a plain SQLite database, ur0:user/00/trophy/db/trophy_local.db: tbl_trophy_title has one row per
 * trophy set (npcommid, progress in percent, the counts per grade) and tbl_trophy_flag one row per trophy (name, description, grade,
 * unlocked, hidden, time). A game's set is named by the folder ux0:app/<titleid>/sce_sys/trophy/NPWRxxxxx_00. The database is copied first
 * (the trophy service keeps it open), then read with the system's SceSqlite, as appdb.h does for LiveArea's own database.
 * The grade of a trophy is 1 platinum, 2 gold, 3 silver, 4 bronze.
 */

#define TROPHY_DB   "ur0:user/00/trophy/db/trophy_local.db"
#define TROPHY_COPY CONFIG_DIR "/trophy.db.copy"
#define TROPHY_CONF "ur0:user/00/trophy/conf"

typedef struct {
	char id[16];                    /* npcommid, NPWRxxxxx_00 */
	char title[64];
	int progress, unlocked, total;
	int have[4], all[4];            /* unlocked / total per grade: platinum, gold, silver, bronze */
} TrophyTitle;

typedef struct {
	int id, grade, unlocked, hidden;
	char title[64];
	char desc[160];
	char date[12];                  /* YYYY-MM-DD of the unlock, empty while locked */
} TrophyRow;

static TrophyTitle trophy_cur;      /* the set the page shows */
static TrophyRow *trophy_rows;
static int trophy_n_rows;

static sqlite3 *trophy_open(void)
{
	if (!sql_init()) return NULL;
	SceUID in = sceIoOpen(TROPHY_DB, SCE_O_RDONLY, 0);
	if (in < 0) return NULL;
	SceUID out = sceIoOpen(TROPHY_COPY, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (out < 0) { sceIoClose(in); return NULL; }
	static uint8_t buf[32 * 1024];
	int n, ok = 1;
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0)
		if (sceIoWrite(out, buf, n) != n) { ok = 0; break; }
	sceIoClose(in);
	sceIoClose(out);
	sqlite3 *db = NULL;
	if (!ok || n < 0 || sqlite3_open_v2(TROPHY_COPY, &db, APPDB_SQLITE_OPEN_READONLY, NULL) != 0) {
		if (db) sqlite3_close(db);
		return NULL;
	}
	return db;
}

/* The database stores the trademark signs as replacement characters and no-break spaces; neither draws. */
static void trophy_text(char *dst, size_t n, const unsigned char *src)
{
	size_t o = 0;
	for (; src && *src && o + 1 < n; src++) {
		if (src[0] == 0xEF && src[1] == 0xBF && src[2] == 0xBD) { src += 2; continue; }
		if (src[0] == 0xC2 && src[1] == 0xA0) { src += 1; dst[o++] = ' '; continue; }
		if (*src < 0x20) { dst[o++] = ' '; continue; }
		dst[o++] = (char)*src;
	}
	dst[o] = 0;
}

static int trophy_cmp(const void *a, const void *b)
{
	return strcasecmp(((const TrophyTitle *)a)->title, ((const TrophyTitle *)b)->title);
}

static void trophy_row_title(const TrophyTitle *t, Item *it)
{
	snprintf(it->title, sizeof(it->title), "%s", t->title);
	snprintf(it->id, sizeof(it->id), "%s", t->id);
	snprintf(it->sub, sizeof(it->sub), "%d%%   %d / %d", t->progress, t->unlocked, t->total);
	it->tr = 0;
}

/* Fills the Trophy Collection list (M_TROPHY) with every game that has trophies, by name. */
static void trophy_fill_menu(void)
{
	clear_menu(M_TROPHY);
	sqlite3 *db = trophy_open();
	sqlite3_stmt *st = NULL;
	static TrophyTitle *tmp;
	if (!tmp) tmp = (TrophyTitle *)malloc(512 * sizeof(TrophyTitle));
	int n = 0;
	if (db && tmp && sqlite3_prepare_v2(db, "SELECT npcommid, title, progress, unlocked_trophy_num, trophy_num, "
	        "unlocked_platinum_num, unlocked_gold_num, unlocked_silver_num, unlocked_bronze_num, "
	        "platinum_num, gold_num, silver_num, bronze_num FROM tbl_trophy_title WHERE trophy_num > 0 AND title IS NOT NULL", -1, &st, NULL) == 0 && st) {
		while (n < 512 && sqlite3_step(st) == APPDB_SQLITE_ROW) {
			TrophyTitle *t = &tmp[n];
			memset(t, 0, sizeof(*t));
			trophy_text(t->id, sizeof(t->id), sqlite3_column_text(st, 0));
			trophy_text(t->title, sizeof(t->title), sqlite3_column_text(st, 1));
			t->progress = sqlite3_column_int(st, 2);
			t->unlocked = sqlite3_column_int(st, 3);
			t->total = sqlite3_column_int(st, 4);
			for (int g = 0; g < 4; g++) { t->have[g] = sqlite3_column_int(st, 5 + g); t->all[g] = sqlite3_column_int(st, 9 + g); }
			if (t->id[0] && t->title[0]) n++;
		}
		sqlite3_finalize(st);
	}
	if (db) sqlite3_close(db);
	qsort(tmp, n, sizeof(TrophyTitle), trophy_cmp);
	for (int i = 0; i < n; i++) {
		Item *it = add_item(M_TROPHY, KIND_PAGE, tmp[i].title, "", tmp[i].id, tex_trophy);
		if (!it) break;
		trophy_row_title(&tmp[i], it);
		it->value_id = PAGE_TROPHY;
	}
	if (menus[M_TROPHY].count == 0) {
		int keep = items_tr; items_tr = 3;
		add_item(M_TROPHY, KIND_INFO, "No trophies", "Play a game with trophies first", NULL, tex_trophy);
		items_tr = keep;
	}
	Menu *mn = &menus[M_TROPHY];
	if (mn->sel >= mn->count) mn->sel = 0;
	mn->pos = (float)mn->sel;
}

/* Loads one set for the page: its totals into trophy_cur and every trophy into trophy_rows. 1 when the database knew it. */
static int trophy_load(const char *commid)
{
	memset(&trophy_cur, 0, sizeof(trophy_cur));
	trophy_n_rows = 0;
	if (!trophy_rows) trophy_rows = (TrophyRow *)malloc(300 * sizeof(TrophyRow));
	sqlite3 *db = trophy_open();
	if (!db || !trophy_rows) { if (db) sqlite3_close(db); return 0; }
	sqlite3_stmt *st = NULL;
	int found = 0;
	if (sqlite3_prepare_v2(db, "SELECT npcommid, title, progress, unlocked_trophy_num, trophy_num, "
	        "unlocked_platinum_num, unlocked_gold_num, unlocked_silver_num, unlocked_bronze_num, "
	        "platinum_num, gold_num, silver_num, bronze_num FROM tbl_trophy_title WHERE npcommid = ?1 AND trophy_num > 0 LIMIT 1", -1, &st, NULL) == 0 && st) {
		sqlite3_bind_text(st, 1, commid, -1, APPDB_TRANSIENT);
		if (sqlite3_step(st) == APPDB_SQLITE_ROW) {
			TrophyTitle *t = &trophy_cur;
			trophy_text(t->id, sizeof(t->id), sqlite3_column_text(st, 0));
			trophy_text(t->title, sizeof(t->title), sqlite3_column_text(st, 1));
			t->progress = sqlite3_column_int(st, 2);
			t->unlocked = sqlite3_column_int(st, 3);
			t->total = sqlite3_column_int(st, 4);
			for (int g = 0; g < 4; g++) { t->have[g] = sqlite3_column_int(st, 5 + g); t->all[g] = sqlite3_column_int(st, 9 + g); }
			found = 1;
		}
		sqlite3_finalize(st);
	}
	if (found && sqlite3_prepare_v2(db, "SELECT trophyid, grade, unlocked, hidden, title, description, time_unlocked "
	        "FROM tbl_trophy_flag WHERE npcommid = ?1 ORDER BY trophyid", -1, &st, NULL) == 0 && st) {
		sqlite3_bind_text(st, 1, commid, -1, APPDB_TRANSIENT);
		while (trophy_n_rows < 300 && sqlite3_step(st) == APPDB_SQLITE_ROW) {
			TrophyRow *r = &trophy_rows[trophy_n_rows++];
			memset(r, 0, sizeof(*r));
			r->id = sqlite3_column_int(st, 0);
			r->grade = sqlite3_column_int(st, 1);
			r->unlocked = sqlite3_column_int(st, 2) != 0;
			r->hidden = sqlite3_column_int(st, 3) != 0;
			trophy_text(r->title, sizeof(r->title), sqlite3_column_text(st, 4));
			trophy_text(r->desc, sizeof(r->desc), sqlite3_column_text(st, 5));
			const unsigned char *tm = sqlite3_column_text(st, 6);
			if (r->unlocked && tm && strlen((const char *)tm) >= 10 && strncmp((const char *)tm, "0001", 4) != 0) snprintf(r->date, sizeof(r->date), "%.10s", (const char *)tm);
		}
		sqlite3_finalize(st);
	}
	sqlite3_close(db);
	return found;
}

/* The trophy set of an installed game (its folder under sce_sys/trophy), or 0. */
static int trophy_commid_for_app(const char *titleid, char *out, size_t n)
{
	char path[96];
	snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/trophy", titleid);
	SceUID d = sceIoDopen(path);
	if (d < 0) return 0;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	int found = 0;
	while (sceIoDread(d, &e) > 0) {
		if (!strncmp(e.d_name, "NPWR", 4) && SCE_S_ISDIR(e.d_stat.st_mode)) { snprintf(out, n, "%s", e.d_name); found = 1; break; }
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
	return found;
}
