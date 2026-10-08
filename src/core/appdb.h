#pragma once

/*
 * appdb.h - the titles LiveArea shows, from the system's app database (ur0:shell/db/app.db, table tbl_appinfo_icon).
 *
 * The bubbles that Adrenaline's tools make for PSP and PS1 games carry a placeholder in param.sfo: TITLE is one letter ("L", "R", "T"),
 * and the name that LiveArea shows lives only in this database. The database is copied first (SceShell keeps it open), then read with the
 * system's SceSqlite module, whose C API is the usual sqlite3 one.
 */

#include <psp2/sysmodule.h>
#include <psp2/sqlite.h>

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
int sqlite3_open_v2(const char *file, sqlite3 **db, int flags, const char *vfs);
int sqlite3_close(sqlite3 *db);
int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int nbytes, sqlite3_stmt **stmt, const char **tail);
int sqlite3_bind_text(sqlite3_stmt *stmt, int idx, const char *text, int nbytes, void (*destructor)(void *));
int sqlite3_step(sqlite3_stmt *stmt);
const unsigned char *sqlite3_column_text(sqlite3_stmt *stmt, int col);
int sqlite3_finalize(sqlite3_stmt *stmt);
#define APPDB_SQLITE_ROW 100
#define APPDB_SQLITE_OPEN_READONLY 1
#define APPDB_TRANSIENT ((void (*)(void *))-1)

#define APPDB_SRC  "ur0:shell/db/app.db"
#define APPDB_COPY CONFIG_DIR "/app.db.copy"

static void *appdb_malloc(int n) { return malloc((size_t)n); }
static void *appdb_realloc(void *p, int n) { return realloc(p, (size_t)n); }
static void appdb_free(void *p) { free(p); }

static int appdb_state;                 /* 0 not tried, 1 database open, -1 not available */
static sqlite3 *appdb;

static int appdb_copy(void)
{
	SceUID in = sceIoOpen(APPDB_SRC, SCE_O_RDONLY, 0);
	if (in < 0) return 0;
	SceUID out = sceIoOpen(APPDB_COPY, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (out < 0) { sceIoClose(in); return 0; }
	static uint8_t buf[32 * 1024];
	int n, ok = 1;
	while ((n = sceIoRead(in, buf, sizeof(buf))) > 0)
		if (sceIoWrite(out, buf, n) != n) { ok = 0; break; }
	sceIoClose(in);
	sceIoClose(out);
	return ok && n >= 0;
}

/* Opens the database (a copy of it, else the original) once per call of appdb_begin(). */
static void appdb_begin(void)
{
	if (appdb) { sqlite3_close(appdb); appdb = NULL; }
	appdb_state = 0;
}

static int appdb_open(void)
{
	if (appdb_state) return appdb_state > 0;
	appdb_state = -1;
	if (sceSysmoduleLoadModule(SCE_SYSMODULE_SQLITE) < 0) { trace("appdb: SceSqlite did not load\n"); return 0; }
	/* the module has no allocator of its own: without one every open fails with SQLITE_NOMEM (7) */
	static SceSqliteMallocMethods mm = { appdb_malloc, appdb_realloc, appdb_free };
	int cr = sceSqliteConfigMallocMethods(&mm);
	trace("appdb: allocator set -> %08x\n", cr);
	const char *path = appdb_copy() ? APPDB_COPY : APPDB_SRC;
	int r = sqlite3_open_v2(path, &appdb, APPDB_SQLITE_OPEN_READONLY, NULL);
	trace("appdb: open %s -> %d\n", path, r);
	if (r != 0) { if (appdb) { sqlite3_close(appdb); appdb = NULL; } return 0; }
	appdb_state = 1;
	return 1;
}

/* The title LiveArea shows for a title id; 1 and out filled when the database knows it. */
static int appdb_title(const char *titleid, char *out, size_t n)
{
	if (!appdb_open()) return 0;
	sqlite3_stmt *st = NULL;
	if (sqlite3_prepare_v2(appdb, "SELECT title FROM tbl_appinfo_icon WHERE titleId = ?1 LIMIT 1", -1, &st, NULL) != 0 || !st) return 0;
	int found = 0;
	sqlite3_bind_text(st, 1, titleid, -1, APPDB_TRANSIENT);
	if (sqlite3_step(st) == APPDB_SQLITE_ROW) {
		const unsigned char *t = sqlite3_column_text(st, 0);
		if (t && t[0]) { snprintf(out, n, "%s", (const char *)t); found = 1; }
	}
	sqlite3_finalize(st);
	trace("appdb: %s -> %s\n", titleid, found ? out : "(none)");
	return found;
}
