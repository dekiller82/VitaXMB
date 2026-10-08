#pragma once

/*
 * ptpatch.h - the code patches a CXMB theme (.ctf) carries for the PSP's own modules.
 *
 * CXMB (ctf.c, main.c) stores a patch for three modules, /vsh/module/paf.prx (scePaf_Module), vshmain.prx (vsh_module)
 * and common_gui.prx (sceVshCommonGui_Module), as a list of records {u32 offset; u32 length; length bytes}, where
 * the offset is relative to the module's text segment (for the stock modules it is the virtual address that the
 * disassembly shows). In the file table the "size" of such an entry is NOT a byte length: it is the number of records
 * (makeDiff() returns the number of differing runs, parseDiff() loops `while (i < size)`).
 *
 * The PSP applies the bytes over its code, so what a theme "says" is the changed instruction bytes. Almost all of them
 * are the immediate of a `lui $at, <hi half of a float>` (a layout constant written into the stock code), sometimes
 * with the `ori` that follows it for the low half. pp_hi / pp_hilo give back the float a site loads after the patch;
 * the stock immediate is passed in, so a site that the theme leaves alone costs nothing.
 *
 * Every site that VitaXMB reads is named where it is used (psptheme.h, pt_apply_patches); the stock instruction at
 * each came from the 6.61 modules (the 6.60 ones carry the same code at the same offsets).
 */

enum { PM_PAF, PM_VSH, PM_CGUI, PM_COUNT };

typedef struct { uint32_t off, len, data; } PpRec;
static uint8_t *pp_buf[PM_COUNT];
static PpRec *pp_rec[PM_COUNT];
static int pp_n[PM_COUNT];

#define PP_MAX_USED 128
static struct { int mod; uint32_t at; } pp_used[PP_MAX_USED];     /* sites a reader has claimed (pp_use) */
static int pp_n_used;

static void pp_clear(void)
{
	pp_n_used = 0;
	for (int m = 0; m < PM_COUNT; m++) {
		free(pp_buf[m]); free(pp_rec[m]);
		pp_buf[m] = NULL; pp_rec[m] = NULL; pp_n[m] = 0;
	}
}

/* Reads the records of one module from the theme file; count is the table entry's size (see above). */
static void pp_load(FILE *f, long start, uint32_t count, int mod)
{
	if (count == 0 || count > 8192) return;
	const size_t cap = 256 * 1024;
	uint8_t *b = malloc(cap);
	PpRec *r = malloc(count * sizeof(PpRec));
	if (!b || !r) { free(b); free(r); return; }
	size_t got = 0;
	if (fseek(f, start, SEEK_SET) == 0) got = fread(b, 1, cap, f);
	uint32_t p = 0, n = 0;
	while (n < count && p + 8 <= got) {
		uint32_t off = pt_rd32(b + p), len = pt_rd32(b + p + 4);
		if (len > got || p + 8 + len > got) break;
		r[n].off = off; r[n].len = len; r[n].data = p + 8;
		n++;
		p += 8 + len;
	}
	uint8_t *small = realloc(b, p ? p : 1);
	pp_buf[mod] = small ? small : b;
	pp_rec[mod] = r;
	pp_n[mod] = (int)n;
}

/* The byte at `at` after the theme's patches (a later record wins), or stock when no record covers it. */
static uint8_t pp_at(int mod, uint32_t at, uint8_t stock)
{
	for (int i = pp_n[mod] - 1; i >= 0; i--) {
		const PpRec *r = &pp_rec[mod][i];
		if (at >= r->off && at - r->off < r->len) return pp_buf[mod][r->data + (at - r->off)];
	}
	return stock;
}

/* Marks a site the theme reader interprets (the instruction word at `at`, 4 bytes), for the coverage report below. */
static void pp_use(int mod, uint32_t at)
{
	if (pp_n_used < PP_MAX_USED) { pp_used[pp_n_used].mod = mod; pp_used[pp_n_used].at = at & ~3u; pp_n_used++; }
}

/* Debug builds: lists the patch records no reader has claimed, so a theme's report shows what is still ignored. */
static void pp_report(void)
{
	static const char *names[PM_COUNT] = { "paf", "vshmain", "common_gui" };
	for (int m = 0; m < PM_COUNT; m++)
		for (int i = 0; i < pp_n[m]; i++) {
			const PpRec *r = &pp_rec[m][i];
			int used = 0;
			for (int k = 0; k < pp_n_used && !used; k++)
				used = pp_used[k].mod == m && pp_used[k].at + 4 > r->off && r->off + r->len > pp_used[k].at;
			if (!used) {
				char hex[40] = "";
				for (uint32_t b = 0; b < r->len && b < 8; b++) snprintf(hex + b * 2, sizeof(hex) - b * 2, "%02x", pp_buf[m][r->data + b]);
				trace("theme patch ignored: %s %07x +%u: %s\n", names[m], (unsigned)r->off, (unsigned)r->len, hex);
			}
		}
}

/* The 16-bit immediate of the instruction at `at` (its low two bytes), after the patches. */
static uint16_t pp_imm(int mod, uint32_t at, uint16_t stock)
{
	return (uint16_t)(pp_at(mod, at, (uint8_t)(stock & 0xFF)) | (pp_at(mod, at + 1, (uint8_t)(stock >> 8)) << 8));
}

static float pp_bits(uint32_t bits)
{
	float v;
	memcpy(&v, &bits, 4);
	return v;
}

/* `lui $at, hi` followed by nothing that matters: the float whose high half is the immediate (a whole number of the
 * 0x4XXX0000 kind: 200.0 is 0x4348). */
static float pp_hi(int mod, uint32_t at, uint16_t stock_hi)
{
	pp_use(mod, at);
	return pp_bits((uint32_t)pp_imm(mod, at, stock_hi) << 16);
}

/* `lui $at, hi` at `at` and `ori $at, $at, lo` right after it: a full float. */
static float pp_hilo(int mod, uint32_t at, uint16_t stock_hi, uint16_t stock_lo)
{
	pp_use(mod, at);
	pp_use(mod, at + 4);
	return pp_bits(((uint32_t)pp_imm(mod, at, stock_hi) << 16) | pp_imm(mod, at + 4, stock_lo));
}
