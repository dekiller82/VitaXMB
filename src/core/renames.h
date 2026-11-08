#pragma once

/* ---- names the user gave to apps (Options > Rename): ux0:data/VitaXMB/renames.txt, one "titleid<TAB>name" per line ----
 * Only the title shown on the bar changes; the app, its bubble and its files stay as they are. An empty name takes the rename away. */

#define RENAMES_PATH CONFIG_DIR "/renames.txt"
#define MAX_RENAMES  256

static struct { char id[16]; char name[64]; } renames[MAX_RENAMES];
static int n_renames, renames_loaded;

static void renames_load(void)
{
	renames_loaded = 1;
	n_renames = 0;
	FILE *f = fopen(RENAMES_PATH, "rb");
	if (!f) return;
	char line[128];
	while (n_renames < MAX_RENAMES && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *tab = strchr(line, '\t');
		if (!tab || tab == line || !tab[1]) continue;
		*tab = 0;
		snprintf(renames[n_renames].id, sizeof(renames[0].id), "%s", line);
		snprintf(renames[n_renames].name, sizeof(renames[0].name), "%s", tab + 1);
		n_renames++;
	}
	fclose(f);
}

static void renames_save(void)
{
	FILE *f = fopen(RENAMES_PATH, "wb");
	if (!f) return;
	for (int i = 0; i < n_renames; i++) fprintf(f, "%s\t%s\n", renames[i].id, renames[i].name);
	fclose(f);
}

static int rename_find(const char *id)
{
	for (int i = 0; i < n_renames; i++) if (!strcmp(renames[i].id, id)) return i;
	return -1;
}

/* sets (name non-empty) or removes (name empty) the rename of an app; the caller scans again to bring an original title back */
static void rename_set(const char *id, const char *name)
{
	if (!renames_loaded) renames_load();
	int i = rename_find(id);
	if (!name[0]) {
		if (i >= 0) { renames[i] = renames[--n_renames]; renames_save(); }
		return;
	}
	if (i < 0) {
		if (n_renames >= MAX_RENAMES) return;
		i = n_renames++;
		snprintf(renames[i].id, sizeof(renames[0].id), "%s", id);
	}
	snprintf(renames[i].name, sizeof(renames[0].name), "%s", name);
	renames_save();
}

/* every app of the master list takes its name, if it was renamed */
static void renames_apply(void)
{
	if (!renames_loaded) renames_load();
	if (!n_renames || !all_apps) return;
	for (int i = 0; i < n_all; i++) {
		int r = rename_find(all_apps[i].id);
		if (r >= 0) { snprintf(all_apps[i].title, sizeof(all_apps[i].title), "%s", renames[r].name); all_apps[i].tr &= ~1; }
	}
}
