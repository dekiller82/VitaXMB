/* NoLiveArea, stage 1: a tracer. Loaded into SceShell (*main) by taiHEN.
 * It only OBSERVES: every event the shell receives from AppMgr and every
 * launch-by-URI the shell makes is written to ux0:data/NoLiveArea/log.txt.
 * Nothing is blocked or changed. Used to learn which event means
 * "an app closed" and "the PS button asked for LiveArea". */
#include <taihen.h>
#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stdarg.h>

#define LOGDIR "ux0:data/NoLiveArea"
#define LOGFILE LOGDIR "/log.txt"

static tai_hook_ref_t ev_ref;
static SceUID ev_hook = -1;

static void put(const char *s) {
  SceUID fd = sceIoOpen(LOGFILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (fd < 0) return;
  int n = 0; while (s[n]) n++;
  sceIoWrite(fd, s, n);
  sceIoClose(fd);
}

static void hex8(char *o, unsigned v) {
  for (int i = 7; i >= 0; i--) { unsigned d = (v >> (i * 4)) & 15; *o++ = d < 10 ? '0' + d : 'a' + d - 10; }
  *o = 0;
}

/* ---- stage 2 state: which apps are active, when did something last happen ---- */
#define VX_URI "psgm:play?titleid=VXMB00001"
#define DISABLE_FILE LOGDIR "/disable"
static volatile unsigned act[16];
static volatile unsigned lastAct, lastLaunch;
static volatile int ready;
static volatile unsigned readyAt;
static volatile int vxSeen;  /* an app was activated since boot (VitaXMB, at boot) */
#define EARLY_FLAG LOGDIR "/earlylaunch"
#define EARLY_PENDING LOGDIR "/early.pending"
static volatile int early_done, early_dead;
static void early_ok(void) { if (!early_dead) sceIoRemove(EARLY_PENDING); }
static volatile unsigned vxId;

static unsigned now_ms(void) { return sceKernelGetProcessTimeLow() / 1000; }
static void act_add(unsigned id) {
  int i; for (i = 0; i < 16; i++) if (act[i] == id) return;
  for (i = 0; i < 16; i++) if (!act[i]) { act[i] = id; return; }
}
static void act_del(unsigned id) { for (int i = 0; i < 16; i++) if (act[i] == id) act[i] = 0; }
static int act_count(void) { int n = 0; for (int i = 0; i < 16; i++) if (act[i]) n++; return n; }
static int vx_active(void) { for (int i = 0; i < 16; i++) if (act[i] && act[i] == vxId) return 1; return 0; }

/* ---- boot cover: hide the shell's root widget (alpha 0) until VitaXMB is active ----
 * Offsets are from the 3.65 SceShell: global object at data+0x694cc, root widget at +0x64,
 * its vtable slot 0x94 sets the alpha, stub 0x8145d398 (nid 4588699E) applies it.
 * Armed only by the file HIDE_FLAG, which is deleted BEFORE use so a crash cannot repeat. */
#define HIDE_FLAG LOGDIR "/hideroot"
static unsigned text_base, data_base;
static SceUID shell_modid = -1;
static int hide_state;  /* 0 unarmed, 1 hiding, 2 done */
static unsigned hide_since;

static int shell_bases(void) {
  SceUID ids[96]; int n = 96;
  if (sceKernelGetModuleList(0xFF, ids, &n) < 0) return 0;
  for (int m = 0; m < n; m++) {
    SceKernelModuleInfo mi; mi.size = sizeof(mi);
    if (sceKernelGetModuleInfo(ids[m], &mi) < 0) continue;
    const char *a = mi.module_name, *b = "SceShell"; int i = 0;
    while (b[i] && a[i] == b[i]) i++;
    if (b[i] || a[i]) continue;
    text_base = (unsigned)mi.segments[0].vaddr; data_base = (unsigned)mi.segments[1].vaddr;
    shell_modid = ids[m];
    return text_base && data_base;
  }
  return 0;
}

/* Observe-only hook on the application launcher's dialog function (shell offset 0x214cc, six
 * arguments: four in registers, two on the stack). It logs its arguments and the caller's return
 * address (shell-relative) and passes straight through, so the real "close the running game
 * first?" prompt can be identified when it is triggered. Armed by DLG_FLAG; DLG_PENDING exists
 * while the hook is being installed, so a crash there disables the hook at the next boot. */
#define DLG_FLAG LOGDIR "/hookdlg"
#define DLG_PENDING LOGDIR "/dlg.pending"
static tai_hook_ref_t dlg_ref; static SceUID dlg_h = -1;
static int dlg_p(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f) {
  char line[200], tmp[12]; int w = 0;
  const char *t = "DLG"; for (int i = 0; t[i]; i++) line[w++] = t[i];
  unsigned v[7] = { a, b, c, d, e, f, (unsigned)__builtin_return_address(0) - text_base };
  for (int k = 0; k < 7; k++) { line[w++] = ' '; hex8(tmp, v[k]); for (int z = 0; z < 8; z++) line[w++] = tmp[z]; }
  line[w++] = '\n'; line[w] = 0; put(line);
  return TAI_CONTINUE(int, dlg_ref, a, b, c, d, e, f);
}
static void dlg_install(void) {
  SceUID f = sceIoOpen(DLG_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f);
  if (!text_base && !shell_bases()) { put("hookdlg: SceShell bases not found\n"); return; }
  SceUID pf = sceIoOpen(DLG_PENDING, SCE_O_WRONLY | SCE_O_CREAT, 0777);
  if (pf >= 0) sceIoClose(pf);
  dlg_h = taiHookFunctionOffset(&dlg_ref, shell_modid, 0, 0x214cc, 1, dlg_p);
  sceIoRemove(DLG_PENDING);
  put(dlg_h >= 0 ? "hookdlg: installed\n" : "hookdlg: install FAILED\n");
}

/* Observe-only hooks on the shell's launch-job machinery (what a bubble tap in LiveArea runs):
 *   job creators 0x29bd9c / 0x29bdfa (shell offsets): (out, target, x, y) -> build a "SpawnProcessJob"
 *   job completion 0x29dd5c: (job, result) -> picks the dialog from the launch result code
 * They log their arguments, a small dump of the target object (an argument the shell itself is
 * passing, so valid), and the caller, then pass straight through. Armed by JOB_FLAG; JOB_PENDING
 * exists while installing, so a crash there disables the hooks at the next boot. */
/* Observe-only hooks on the shell's launch decision path, with RETURN values: StartApp
 * (0x278a00: core, name, param, appId), NeedsConfirm (0x27895e: core, flag, name) and the confirm
 * dialog (0x271f72: core, flag, job, flag2). Each hook only stores args, return value and caller in a
 * memory queue (no file I/O, no memory reads); the watchdog thread writes it out. Armed by
 * HOOKLAUNCH_FLAG; covered by the general probe guard (a crash disables the flag next boot). */
#define HOOKLAUNCH_FLAG LOGDIR "/hooklaunch"
static tai_hook_ref_t hl1_ref, hl2_ref, hl3_ref; static SceUID hl1_h = -1, hl2_h = -1, hl3_h = -1;
static volatile unsigned hlq[64][8]; static volatile unsigned hl_w, hl_r;
static void hl_push(unsigned tag, unsigned a, unsigned b, unsigned c, unsigned d, unsigned ret, unsigned lr) {
  unsigned i = hl_w & 63;
  hlq[i][0] = tag; hlq[i][1] = a; hlq[i][2] = b; hlq[i][3] = c; hlq[i][4] = d; hlq[i][5] = ret; hlq[i][6] = lr;
  hl_w++;
}
static int hl1_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  unsigned lr = (unsigned)__builtin_return_address(0);
  int r = TAI_CONTINUE(int, hl1_ref, a, b, c, d); hl_push(1, a, b, c, d, (unsigned)r, lr); return r;
}
static int hl2_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  unsigned lr = (unsigned)__builtin_return_address(0);
  int r = TAI_CONTINUE(int, hl2_ref, a, b, c, d); hl_push(2, a, b, c, d, (unsigned)r, lr); return r;
}
static int hl3_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  unsigned lr = (unsigned)__builtin_return_address(0);
  int r = TAI_CONTINUE(int, hl3_ref, a, b, c, d); hl_push(3, a, b, c, d, (unsigned)r, lr); return r;
}
static void hl_install(void) {
  SceUID f = sceIoOpen(HOOKLAUNCH_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f);
  if (!text_base && !shell_bases()) { put("hooklaunch: SceShell bases not found\n"); return; }
  hl1_h = taiHookFunctionOffset(&hl1_ref, shell_modid, 0, 0x278a00, 1, hl1_p);
  hl2_h = taiHookFunctionOffset(&hl2_ref, shell_modid, 0, 0x27895e, 1, hl2_p);
  hl3_h = taiHookFunctionOffset(&hl3_ref, shell_modid, 0, 0x271f72, 1, hl3_p);
  put((hl1_h >= 0 && hl2_h >= 0 && hl3_h >= 0) ? "hooklaunch: installed\n" : "hooklaunch: install FAILED (some)\n");
}
static void hl_flush(void) {
  while (hl_r != hl_w) {
    unsigned i = hl_r & 63;
    const char *tag = hlq[i][0] == 1 ? "HL_START" : hlq[i][0] == 2 ? "HL_NEEDS" : "HL_CONFIRM";
    char line[240], tmp[12]; int w = 0;
    for (int k = 0; tag[k]; k++) line[w++] = tag[k];
    unsigned v[6] = { hlq[i][1], hlq[i][2], hlq[i][3], hlq[i][4], hlq[i][5], hlq[i][6] - text_base };
    for (int k = 0; k < 6; k++) { line[w++] = ' '; hex8(tmp, v[k]); for (int z = 0; z < 8; z++) line[w++] = tmp[z]; }
    line[w++] = '\n'; line[w] = 0; put(line);
    hl_r++;
  }
}

/* Native "close the running game?" prompt on a PS press. The shell's own method (static address
 * 0x81271f72, shell offset 0x271f72; this = the shell core object, args (core, 0, 0, 0) target the
 * app that is currently running) checks that an app is running and opens the shell's own Yes/No
 * dialog with its own callback (Yes closes the app, No dismisses). Armed by PSCONFIRM_FLAG; it is
 * only ever called from the top of the shell's event loop (a safe point), never from inside the
 * PS handling. PSCONFIRM_RUN exists from just before the call until 8 s later; if it is found at
 * start, the last call killed the shell and the flag is deleted. */
#define PSCONFIRM_FLAG LOGDIR "/psconfirm"
#define PSCONFIRM_RUN LOGDIR "/psconfirm.run"
static volatile int psconfirm_pending;
static volatile unsigned psconfirm_clear_at, psconfirm_last;
/* v2: the dialog method alone returns 0 at once for the app that is already current. The shell's
 * real launch path is core->StartApp (static 0x81278a00, shell offset 0x278a00):
 *   (core, const char *titleId, param, appId)
 * param must point at a two-word block whose first word is 0 (the job constructor dereferences it),
 * appId -1 means "no pre-allocated launch id". It builds a launch job for the title and, when another
 * app is running, hands it to the confirm dialog (Yes: close the running app and start the job;
 * No: cancel); with nothing running it starts the job directly. */
static unsigned psc_param[2];
static void psconfirm_run(void) {
  SceUID f = sceIoOpen(PSCONFIRM_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f);
  if (psconfirm_last && now_ms() - psconfirm_last < 8000) return;  /* one PS press fires the detector several times */
  psconfirm_last = now_ms();
  if (!text_base && !shell_bases()) return;
  unsigned char *g = *(unsigned char **)(data_base + 0x694cc);
  if (!g) return;
  SceUID m = sceIoOpen(PSCONFIRM_RUN, SCE_O_WRONLY | SCE_O_CREAT, 0777);
  if (m >= 0) sceIoClose(m);
  psconfirm_clear_at = now_ms() + 8000;
  psc_param[0] = 0; psc_param[1] = 0;
  put("psconfirm: asking the shell to start VXMB00001 (its own confirm dialog if a game runs)\n");
  int r = ((int (*)(void *, const char *, void *, int))(text_base + 0x278a00 + 1))(g, "VXMB00001", psc_param, -1);
  char line[40], tmp[12]; int w = 0;
  const char *s = "psconfirm: returned "; for (int i = 0; s[i]; i++) line[w++] = s[i];
  hex8(tmp, (unsigned)r); for (int i = 0; i < 8; i++) line[w++] = tmp[i];
  line[w++] = '\n'; line[w] = 0; put(line);
}
#define PROBE_RUN LOGDIR "/probe.run"
static volatile int probe_marker;
#define JOB_FLAG LOGDIR "/hookjob"
#define JOB_PENDING LOGDIR "/job.pending"
static tai_hook_ref_t jobA_ref, jobB_ref, jobC_ref; static SceUID jobA_h = -1, jobB_h = -1, jobC_h = -1;
static void job_log(const char *tag, unsigned a, unsigned b, unsigned c, unsigned d, unsigned lr) {
  char line[260], tmp[12]; int w = 0;
  for (int i = 0; tag[i]; i++) line[w++] = tag[i];
  unsigned v[5] = { a, b, c, d, lr - text_base };
  for (int k = 0; k < 5; k++) { line[w++] = ' '; hex8(tmp, v[k]); for (int z = 0; z < 8; z++) line[w++] = tmp[z]; }
  line[w++] = '\n'; line[w] = 0; put(line);
}
static void log_words(const char *tag, const unsigned *p, int n);
static void job_dump(const char *tag, unsigned p) {
  if (p < 0x80000000u || (p & 3)) return;           /* only plausible user pointers, word aligned */
  log_words(tag, (const unsigned *)p, 16);
}
static int jobA_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  job_log("JOBA", a, b, c, d, (unsigned)__builtin_return_address(0)); job_dump("JOBA_T", b);
  return TAI_CONTINUE(int, jobA_ref, a, b, c, d);
}
static int jobB_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  job_log("JOBB", a, b, c, d, (unsigned)__builtin_return_address(0)); job_dump("JOBB_T", b);
  return TAI_CONTINUE(int, jobB_ref, a, b, c, d);
}
static int jobC_p(unsigned a, unsigned b, unsigned c, unsigned d) {
  job_log("JOBC", a, b, c, d, (unsigned)__builtin_return_address(0));
  return TAI_CONTINUE(int, jobC_ref, a, b, c, d);
}
static void job_install(void) {
  SceUID f = sceIoOpen(JOB_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f);
  if (!text_base && !shell_bases()) { put("hookjob: SceShell bases not found\n"); return; }
  SceUID pf = sceIoOpen(JOB_PENDING, SCE_O_WRONLY | SCE_O_CREAT, 0777);
  if (pf >= 0) sceIoClose(pf);
  jobA_h = taiHookFunctionOffset(&jobA_ref, shell_modid, 0, 0x29bd9c, 1, jobA_p);
  jobB_h = taiHookFunctionOffset(&jobB_ref, shell_modid, 0, 0x29bdfa, 1, jobB_p);
  jobC_h = taiHookFunctionOffset(&jobC_ref, shell_modid, 0, 0x29dd5c, 1, jobC_p);
  sceIoRemove(JOB_PENDING);
  put((jobA_h >= 0 && jobB_h >= 0 && jobC_h >= 0) ? "hookjob: installed\n" : "hookjob: install FAILED (some)\n");
}

/* Observe-only import hook on the dialog library's open call (SceCommonGuiDialog, nid CCFCD9A6):
 * every dialog the shell opens goes through it with one argument, the parameter block. Logs the
 * caller (shell-relative) and the block's words, then passes through. Armed by CGD_FLAG. */
#define CGD_FLAG LOGDIR "/hookcgd"
static tai_hook_ref_t cgd_ref; static SceUID cgd_h = -1;
/* Bisect step A: NO file I/O and NO memory reads inside the hook. It only stores the pointer value
 * and the caller's address in a small in-memory queue; the watchdog thread writes the queue out. */
static volatile unsigned cgdq[32][2]; static volatile unsigned cgd_w, cgd_r;
static volatile int cgd_done;
static int cgd_p(unsigned P) {
  unsigned i = cgd_w & 31;
  cgdq[i][0] = P; cgdq[i][1] = (unsigned)__builtin_return_address(0);
  cgd_w++;
  return TAI_CONTINUE(int, cgd_ref, P);
}
static void cgd_flush(void) {
  while (cgd_r != cgd_w) {
    unsigned i = cgd_r & 31;
    job_log("CGD", cgdq[i][0], 0, 0, 0, cgdq[i][1]);
    cgd_r++;
  }
}
static void cgd_install(void) {
  SceUID f = sceIoOpen(CGD_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f);
  if (!text_base) shell_bases();
  cgd_h = taiHookFunctionImport(&cgd_ref, "SceShell", 0x4B4AAE97, 0xCCFCD9A6, cgd_p);
  put(cgd_h >= 0 ? "hookcgd: installed\n" : "hookcgd: install FAILED\n");
}

/* Read-only map of the root widget: its own fields (a small range just past the ones already
 * touched safely by set_root_alpha) and its vtable (which lives in the shell's own fixed code
 * segment, not the heap, so reading it carries no risk of an unmapped-page fault). No pointer
 * found in either is ever followed -- this never touches memory whose validity isn't already
 * known. Logged once, gated by its own flag file. */
#define MAPROOT_FLAG LOGDIR "/maproot"
static void hexw(char *o, unsigned v) { hex8(o, v); }
static void log_words(const char *tag, const unsigned *p, int n) {
  for (int i = 0; i < n; i += 4) {
    char line[200], tmp[12]; int w = 0;
    for (int j = 0; tag[j]; j++) line[w++] = tag[j];
    line[w++] = ' '; hex8(tmp, (unsigned)i); for (int k = 0; k < 8; k++) line[w++] = tmp[k];
    for (int k = 0; k < 4 && i + k < n; k++) { line[w++] = ' '; hexw(tmp, p[i + k]); for (int z = 0; z < 8; z++) line[w++] = tmp[z]; }
    line[w++] = '\n'; line[w] = 0; put(line);
  }
}
static void maproot_once(void) {
  SceUID f = sceIoOpen(MAPROOT_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f); sceIoRemove(MAPROOT_FLAG);
  if (!text_base && !shell_bases()) { put("maproot: SceShell bases not found\n"); return; }
  unsigned char *g = *(unsigned char **)(data_base + 0x694cc);
  if (!g) { put("maproot: global object is null\n"); return; }
  unsigned char *root = *(unsigned char **)(g + 0x64);
  if (!root) { put("maproot: root widget is null\n"); return; }
  char line[64], tmp[12]; int w = 0;
  const char *m = "maproot: root at (shell-relative data) "; for (int i = 0; m[i]; i++) line[w++] = m[i];
  hex8(tmp, (unsigned)((unsigned)root)); for (int i = 0; i < 8; i++) line[w++] = tmp[i];
  line[w++] = '\n'; line[w] = 0; put(line);
  log_words("ROOTFIELD", (unsigned *)root, 0x1b0 / 4);
  void **vt = *(void ***)root;
  /* vtable entries logged as shell-relative offsets (vt[i] - text_base), so they map straight
   * onto the static disassembly regardless of where the module actually loaded this boot */
  unsigned rel[64];
  for (int i = 0; i < 64; i++) rel[i] = (unsigned)((unsigned)vt[i] - text_base);
  log_words("ROOTVTBL", rel, 64);
  put("maproot: done\n");
}

/* Read-only module list: name, file path, text/data base + size of every module loaded in the shell
 * process, so a vtable address like 0xe02e1c5d can be mapped to the file that must be decrypted.
 * Uses only sceKernelGetModuleList/Info (already used safely above). One-shot, own flag file. */
#define MODMAP_FLAG LOGDIR "/modmap"
static void modmap_once(void) {
  SceUID f = sceIoOpen(MODMAP_FLAG, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoClose(f); sceIoRemove(MODMAP_FLAG);
  SceUID ids[96]; int n = 96;
  if (sceKernelGetModuleList(0xFF, ids, &n) < 0) { put("modmap: list failed\n"); return; }
  for (int m = 0; m < n; m++) {
    SceKernelModuleInfo mi; mi.size = sizeof(mi);
    if (sceKernelGetModuleInfo(ids[m], &mi) < 0) continue;
    char line[420], tmp[12]; int w = 0;
    const char *tag = "MOD "; for (int i = 0; tag[i]; i++) line[w++] = tag[i];
    for (int i = 0; mi.module_name[i] && i < 27; i++) line[w++] = mi.module_name[i];
    line[w++] = ' ';
    for (int i = 0; mi.path[i] && i < 200; i++) line[w++] = mi.path[i];
    for (int s = 0; s < 2; s++) {
      line[w++] = ' '; hex8(tmp, (unsigned)mi.segments[s].vaddr); for (int k = 0; k < 8; k++) line[w++] = tmp[k];
      line[w++] = '+'; hex8(tmp, (unsigned)mi.segments[s].memsz); for (int k = 0; k < 8; k++) line[w++] = tmp[k];
    }
    line[w++] = '\n'; line[w] = 0; put(line);
  }
  put("modmap: done\n");
}

static void set_root_alpha(float alpha) {
  unsigned char *g = *(unsigned char **)(data_base + 0x694cc);
  if (!g) return;
  unsigned char *root = *(unsigned char **)(g + 0x64);
  if (!root) return;
  root[0x196] &= 0x7f;
  root[0x184] = 0xff;
  void **vt = *(void ***)root;
  ((void (*)(void *, int, float))vt[0x94 / 4])(root, 1, alpha);
  ((void (*)(void *, int, int, int, float))(text_base + (0x8145d398 - 0x81000000)))(root, 0, 2, 0, alpha);
}

static void hide_tick(void) {
  if (hide_state == 2) return;
  if (hide_state == 0) {
    SceUID f = sceIoOpen(HIDE_FLAG, SCE_O_RDONLY, 0);
    if (f < 0) { hide_state = 2; return; }
    sceIoClose(f);
    sceIoRemove(HIDE_FLAG);
    if (!shell_bases()) { put("hide: SceShell bases not found\n"); hide_state = 2; return; }
    hide_state = 1; hide_since = now_ms();
    put("hide: root alpha 0\n");
  }
  if (now_ms() - hide_since > 25000) { set_root_alpha(1.0f); put("hide: timeout, restored\n"); hide_state = 2; return; }
  set_root_alpha(0.0f);
}
static void hide_release(void) {
  if (hide_state != 1) return;
  hide_state = 2;
  set_root_alpha(1.0f);
  put("hide: VitaXMB active, restored\n");
}

/* ---- screen cover: the display is switched off (black) instead of showing LiveArea ----
 * boot: from "shell ready" until VitaXMB activates; game launch: from VitaXMB being sent to the
 * background until the game activates; game exit: from the destroy until VitaXMB is back.
 * Needs the file COVER_FLAG. Every cover ends after at most 25 s. COVER_PENDING exists while the
 * screen is covered; if it is found at start the last run died covered, so the cover is disabled. */
#define COVER_FLAG LOGDIR "/cover"
#define COVER_PENDING LOGDIR "/cover.pending"
static volatile int cover_want, covering, cover_dead;
static volatile unsigned cover_until, release_at, vx_left_at;
static unsigned vxids[8]; static int nvx;
static int is_vx(unsigned id) { for (int i = 0; i < nvx; i++) if (vxids[i] == id) return 1; return 0; }
static void vx_learn(unsigned id) { if (!is_vx(id) && nvx < 8) vxids[nvx++] = id; }
static void cover_request(unsigned ms) { if (cover_dead && ms > 8000) ms = 8000; cover_until = now_ms() + ms; release_at = 0; cover_want = 1; }
static void cover_release(void) { cover_want = 0; if (covering) release_at = now_ms() + 300; }

/* int sceAppMgrReceiveShellEvent(void *ev): the shell's event pump */
static int ev_patched(unsigned *ev) {
  int r = TAI_CONTINUE(int, ev_ref, ev);
  if (r >= 0 && ev) {
    unsigned ty = ev[0];
    if (ty == 0x11 && !ready) { ready = 1; readyAt = now_ms(); lastAct = readyAt; modmap_once(); }
    if (psconfirm_pending && ready) { psconfirm_pending = 0; psconfirm_run(); }
    if (ty == 2 && vx_left_at && now_ms() - vx_left_at < 1500) { vx_left_at = 0; cover_request(20000); }
    if (ready) hide_tick();
    if (ty == 2 || ty == 3 || ty == 4) lastAct = now_ms();
    if (ty == 4 && ev[2] == 0 && ev[8] == 0x424d5856 && ev[9] == 0x30303030) vxId = ev[5];
    char line[160], tmp[12]; int p = 0;
    unsigned t = sceKernelGetProcessTimeLow();
    hex8(tmp, t); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
    line[p++] = ' '; line[p++] = 'E'; line[p++] = 'V';
    for (int i = 0; i < 10; i++) { line[p++] = ' '; hex8(tmp, ev[i]); for (int j = 0; j < 8; j++) line[p++] = tmp[j]; }
    line[p++] = '\n'; line[p] = 0;
    put(line);
  }
  return r;
}

/* generic observers: log name + first four args, then call through */
static void hexn(char *o, unsigned v) { hex8(o, v); }
static void logcall(const char *name, unsigned a, unsigned b, unsigned c, unsigned d) {
  char line[200], tmp[12]; int p = 0;
  hex8(tmp, sceKernelGetProcessTimeLow()); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
  line[p++] = ' '; line[p++] = 'C'; line[p++] = ' ';
  for (int i = 0; name[i] && p < 120; i++) line[p++] = name[i];
  unsigned v[4] = { a, b, c, d };
  for (int k = 0; k < 4; k++) { line[p++] = ' '; hexn(tmp, v[k]); for (int j = 0; j < 8; j++) line[p++] = tmp[j]; }
  line[p++] = '\n'; line[p] = 0;
  put(line);
}
#define OBS(N, LIB, FNID)   static tai_hook_ref_t N##_ref; static SceUID N##_h = -1;   static int N##_p(unsigned a, unsigned b, unsigned c, unsigned d) {     logcall(#N, a, b, c, d); return TAI_CONTINUE(int, N##_ref, a, b, c, d); }
#define OBSA(N, STATE) \
  static tai_hook_ref_t N##_ref; static SceUID N##_h = -1; \
  static int N##_p(unsigned a, unsigned b, unsigned c, unsigned d) { \
    logcall(#N, a, b, c, d); STATE; lastAct = now_ms(); return TAI_CONTINUE(int, N##_ref, a, b, c, d); }
static volatile unsigned pendingAt;
/* PS button in another app: the shell asks for the foreground app (id 0xfffffffd). With the file
 * PSQUIT_FLAG present we then start VitaXMB ourselves so the system shows its own "close the
 * running game?" prompt (Yes closes the game and opens VitaXMB, No returns to the game). This
 * DESTROYS the suspended game via the app-launch path and is kept disabled; do not re-enable
 * without the user's confirmation. */
#define PSQUIT_FLAG LOGDIR "/psquit"

/* ---- proof of concept: draw into the buffer shell.self is already displaying, from inside its
 * own process, on PS press. No new GXM context, no app launch, no hook on shared code -- just a
 * CPU write into memory the shell already owns and is already showing. CONFIRMED UNSAFE: raw
 * sceDisplayGetFrameBuf crashed the whole shell process twice (it only works for whichever
 * process currently owns the display slot, which the shell cannot be assumed to hold here) --
 * replaced by the read-only widget map above (maproot_once) instead of guessing at writes. */
static volatile unsigned psAt, lastPs, psLaunchedAt;
/* fresh app-id lookups: one = a launch (cover the loading screen), a burst of 3+ within a few
 * ms = the shell building LiveArea for a PS press (then, with a game running, start VitaXMB) */
static volatile unsigned fresh_t[8], fresh_n, decideAt;
static void fresh_note(void) { unsigned t = now_ms(); fresh_t[fresh_n++ & 7] = t ? t : 1; if (!decideAt) decideAt = t + 100; }
static int game_active(void) { for (int i = 0; i < 16; i++) if (act[i] && !is_vx(act[i])) return 1; return 0; }
OBSA(Activate, act_add(a); pendingAt = 0; vxSeen = 1; vx_left_at = 0; early_ok(); hide_release(); cover_release())
OBSA(Deactivate, act_del(a); if (is_vx(a)) vx_left_at = now_ms())
/* VitaXMB writes ADR_FLAG just before it starts Adrenaline (PSPEMUCFW). Adrenaline 8.0.2 froze the Vita on
 * its second launch (kernel module loaded) after the display cover switched the screen off and on again, so
 * for 30 s after the flag no cover is started. LiveArea may show briefly instead of a black screen. */
#define ADR_FLAG LOGDIR "/adrenaline"
static volatile unsigned adr_until;
OBSA(Destroy, act_del(a); if (a != vxId && !is_vx(a)) { pendingAt = now_ms() + 400; cover_request(15000); })
OBS(LiveBoard, 0, 0)
OBS(LaunchShell, 0, 0) OBS(LaunchName2, 0, 0) OBS(Quit, 0, 0) OBS(SuspendBg, 0, 0)
OBS(ResumeBg, 0, 0) OBS(LaunchCancel, 0, 0) OBS(LaResult, 0, 0) OBS(OverwriteParam, 0, 0)
OBS(LaunchUri2, 0, 0) OBS(DestroyOther, 0, 0) OBS(Event, 0, 0) OBS(UriLaunch, 0, 0)
#define HK(N, LIB, FNID) do { N##_h = taiHookFunctionImport(&N##_ref, "SceShell", LIB, FNID, N##_p);   put(N##_h >= 0 ? "hook " #N " ok\n" : "hook " #N " FAILED\n"); } while (0)


/* sceAppMgrGetStatusByAppId(appId, out): log the out buffer once per appId so IDs map to title ids */
static tai_hook_ref_t Stat_ref; static SceUID Stat_h = -1;
static unsigned seen[64]; static int nseen;
static int Stat_p(unsigned id, unsigned *out) {
  int r = TAI_CONTINUE(int, Stat_ref, id, out);
  if (r >= 0 && out && out[3] == 0x424d5856 && out[4] == 0x30303030) vx_learn(id);  /* title VXMB0000(1) */
  /* a status lookup of a brand-new app id (N0004 / N0006) with an empty title is the shell starting
   * a launch: the loading screen (what looks like LiveArea) follows until that app activates */
  if (r >= 0 && out && id == 0xfffffffd && out[3] != 0x424d5856 && out[3] != 0x5358504e && ready && now_ms() - lastPs > 3000) {
    lastPs = now_ms();
    put("PS press detected (foreground app status lookup, real game)\n");
    maproot_once();
    psconfirm_pending = 1;  /* run at the next safe point (top of the event loop), not from here */
  }
  if (r >= 0 && out && out[3] == 0 && out[4] == 0 && id >= 0x10000 && (id & 0xffff) >= 4 && (id & 0xffff) <= 0x40 && !(id & 1)) fresh_note();
  if (r >= 0 && out) {
    int k; for (k = 0; k < nseen; k++) if (seen[k] == id) break;
    if (k == nseen && nseen < 64) {
      seen[nseen++] = id;
      char line[400], tmp[12]; int p = 0;
      hex8(tmp, sceKernelGetProcessTimeLow()); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
      const char *h = " STAT "; for (int i = 0; h[i]; i++) line[p++] = h[i];
      hex8(tmp, id); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
      for (int w = 0; w < 36; w++) { line[p++] = ' '; hex8(tmp, out[w]); for (int j = 0; j < 8; j++) line[p++] = tmp[j]; }
      line[p++] = '\n'; line[p] = 0;
      put(line);
    }
  }
  return r;
}

/* An app (not VitaXMB) was destroyed: the shell would now show LiveArea. Start VitaXMB instead.
 * The disable file stops it; at most one launch per 3 s. */

static int watchdog(SceSize args, void *argp) {
  (void)args; (void)argp;
  for (;;) {
    sceKernelDelayThread(100 * 1000);
    if (ready && !cgd_done && now_ms() - readyAt > 1500) { cgd_done = 1; cgd_install(); }
    cgd_flush();
    hl_flush();
    { SceUID af = sceIoOpen(ADR_FLAG, SCE_O_RDONLY, 0);
      if (af >= 0) { sceIoClose(af); sceIoRemove(ADR_FLAG); adr_until = now_ms() + 30000; put("adrenaline launch flagged: no cover for 30 s\n"); } }
    if (psconfirm_clear_at && now_ms() >= psconfirm_clear_at) { psconfirm_clear_at = 0; sceIoRemove(PSCONFIRM_RUN); put("psconfirm: shell survived 8 s, guard cleared\n"); }
    if (probe_marker && now_ms() >= 40000) { probe_marker = 0; sceIoRemove(PROBE_RUN); put("probes survived 40 s, guard cleared\n"); }
    /* Early boot launch (optional, file EARLY_FLAG holds the time in ms, default 8000). Launching at
     * 5 s crashed the system 4 times, AutoBoot's 10.6 s is fine. EARLY_PENDING is written before the
     * launch and removed when an app activates; if it is found at start the last try died, so the
     * feature switches itself off. */
    if (!early_done && !early_dead && ready && !vxSeen) {
      SceUID ef = sceIoOpen(EARLY_FLAG, SCE_O_RDONLY, 0);
      if (ef < 0) early_done = 1;
      else {
        char nb[16]; int n = sceIoRead(ef, nb, 15); sceIoClose(ef);
        unsigned at = 0; for (int i = 0; i < n && nb[i] >= '0' && nb[i] <= '9'; i++) at = at * 10 + (nb[i] - '0');
        if (!at) at = 8000;
        if (now_ms() >= at) {
          early_done = 1;
          SceUID pf = sceIoOpen(EARLY_PENDING, SCE_O_WRONLY | SCE_O_CREAT, 0777);
          if (pf >= 0) sceIoClose(pf);
          put("early launch\n");
          int r = TAI_CONTINUE(int, UriLaunch_ref, 0x20000, VX_URI);
          char line[40], tmp[12]; int p = 0;
          const char *m = "early launch result "; for (int i = 0; m[i]; i++) line[p++] = m[i];
          hex8(tmp, (unsigned)r); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
          line[p++] = '\n'; line[p] = 0; put(line);
        }
      }
    }
    if (cover_want && !covering) {
      cover_want = 0;
      if (adr_until && now_ms() < adr_until) { put("cover skipped (adrenaline)\n"); goto nocover; }
      SceUID cf = sceIoOpen(COVER_FLAG, SCE_O_RDONLY, 0), df = sceIoOpen(DISABLE_FILE, SCE_O_RDONLY, 0);
      if (cf >= 0) sceIoClose(cf);
      if (df >= 0) sceIoClose(df);
      if (cf >= 0 && df < 0) {
        SceUID pf = sceIoOpen(COVER_PENDING, SCE_O_WRONLY | SCE_O_CREAT, 0777);
        if (pf >= 0) sceIoClose(pf);
        scePowerRequestDisplayOff(); covering = 1; put("cover on\n");
      }
    }
    nocover:
    if (covering && ((release_at && now_ms() >= release_at) || now_ms() >= cover_until)) {
      scePowerRequestDisplayOn(); covering = 0; release_at = 0; sceIoRemove(COVER_PENDING); put("cover off\n");
    }
    if (decideAt && now_ms() >= decideAt) {
      unsigned t = now_ms(); int c = 0;
      for (int i = 0; i < 8; i++) { if (fresh_t[i] && t - fresh_t[i] < 400) c++; fresh_t[i] = 0; }
      decideAt = 0;
      /* 3+ fresh-id lookups within ~40 ms is the shell building LiveArea for a PS press (a launch makes
       * exactly one). With a game running, ask the shell's own "close the game?" dialog instead. */
      if (c >= 3) {
        if (game_active()) { put("PS burst with a game running\n"); psconfirm_pending = 1; }
        else put("PS burst, no game running\n");
      }
      else cover_request(25000);
    }
    if (psAt && now_ms() >= psAt) {
      psAt = 0;
      SceUID qf = sceIoOpen(PSQUIT_FLAG, SCE_O_RDONLY, 0), df2 = sceIoOpen(DISABLE_FILE, SCE_O_RDONLY, 0);
      if (qf >= 0) sceIoClose(qf);
      if (df2 >= 0) sceIoClose(df2);
      if (qf >= 0 && df2 < 0) {
        put("PS press in an app: launching VitaXMB (system asks to close the game)\n");
        psLaunchedAt = now_ms();
        int r = TAI_CONTINUE(int, UriLaunch_ref, 0x20000, VX_URI);
        char line[40], tmp[12]; int p = 0;
        const char *m = "ps launch result "; for (int i = 0; m[i]; i++) line[p++] = m[i];
        hex8(tmp, (unsigned)r); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
        line[p++] = '\n'; line[p] = 0; put(line);
      }
    }
    unsigned pa = pendingAt;
    if (!pa || now_ms() < pa) continue;
    pendingAt = 0;
    if (now_ms() - lastLaunch < 3000) continue;
    if (psLaunchedAt && now_ms() - psLaunchedAt < 30000) { put("exit after PS prompt: system opens VitaXMB itself\n"); continue; }
    SceUID d = sceIoOpen(DISABLE_FILE, SCE_O_RDONLY, 0);
    if (d >= 0) { sceIoClose(d); continue; }
    lastLaunch = now_ms();
    put("app destroyed: launching VitaXMB\n");
    int r = TAI_CONTINUE(int, UriLaunch_ref, 0x20000, VX_URI);
    char line[40], tmp[12]; int p = 0;
    const char *m = "launch result "; for (int i = 0; m[i]; i++) line[p++] = m[i];
    hex8(tmp, (unsigned)r); for (int i = 0; i < 8; i++) line[p++] = tmp[i];
    line[p++] = '\n'; line[p] = 0; put(line);
  }
  return 0;
}

int module_start(SceSize args, const void *argp) {
  (void)args; (void)argp;
  sceIoMkdir(LOGDIR, 0777);
  put("-- NoLiveArea tracer started\n");
  { SceUID cp = sceIoOpen(PSCONFIRM_RUN, SCE_O_RDONLY, 0);
    if (cp >= 0) { sceIoClose(cp); sceIoRemove(PSCONFIRM_RUN); sceIoRemove(PSCONFIRM_FLAG);
      put("psconfirm.run found: the last confirm-dialog call killed the shell, psconfirm disabled\n"); } }
  /* general probe guard: PROBE_RUN exists from boot until the shell has survived 40 s. If it is
   * still there now, the last boot died with probes armed, so every probe flag is deleted. */
  { SceUID rp = sceIoOpen(PROBE_RUN, SCE_O_RDONLY, 0);
    if (rp >= 0) { sceIoClose(rp); sceIoRemove(PROBE_RUN);
      sceIoRemove(CGD_FLAG); sceIoRemove(HOOKLAUNCH_FLAG); sceIoRemove(JOB_FLAG); sceIoRemove(DLG_FLAG); sceIoRemove(MAPROOT_FLAG); sceIoRemove(MODMAP_FLAG);
      put("probe.run found: last boot died with probes armed, ALL probe flags deleted\n"); } }
  { const char *flags[6] = { CGD_FLAG, JOB_FLAG, DLG_FLAG, MAPROOT_FLAG, MODMAP_FLAG, HOOKLAUNCH_FLAG }; int any = 0;
    for (int i = 0; i < 6; i++) { SceUID q = sceIoOpen(flags[i], SCE_O_RDONLY, 0); if (q >= 0) { sceIoClose(q); any = 1; } }
    if (any) { SceUID m = sceIoOpen(PROBE_RUN, SCE_O_WRONLY | SCE_O_CREAT, 0777); if (m >= 0) { sceIoClose(m); probe_marker = 1; } } }
  { SceUID jp = sceIoOpen(JOB_PENDING, SCE_O_RDONLY, 0);
    if (jp >= 0) { sceIoClose(jp); sceIoRemove(JOB_PENDING); sceIoRemove(JOB_FLAG);
      put("job.pending found: last hookjob install died, hooks disabled\n"); } }
  { SceUID dp = sceIoOpen(DLG_PENDING, SCE_O_RDONLY, 0);
    if (dp >= 0) { sceIoClose(dp); sceIoRemove(DLG_PENDING); sceIoRemove(DLG_FLAG);
      put("dlg.pending found: last hookdlg install died, hook disabled\n"); } }
  { SceUID ep = sceIoOpen(EARLY_PENDING, SCE_O_RDONLY, 0);
    if (ep >= 0) { sceIoClose(ep); sceIoRemove(EARLY_PENDING); sceIoRemove(EARLY_FLAG); early_dead = 1;
      put("early.pending found: last early launch died, feature disabled\n"); } }
  { SceUID pf = sceIoOpen(COVER_PENDING, SCE_O_RDONLY, 0);
    if (pf >= 0) { sceIoClose(pf); sceIoRemove(COVER_PENDING); cover_dead = 1;
      put("cover pending found: last run ended covered, covers limited to 8 s this boot\n"); } }
  ev_hook = taiHookFunctionImport(&ev_ref, "SceShell", 0xA6605D6F, 0x15AC8C52, ev_patched);
  HK(Activate, 0x8AF17416, 0x43631508); HK(Deactivate, 0x8AF17416, 0xFFA093D9);
  HK(LiveBoard, 0x8AF17416, 0x389F048D); HK(Destroy, 0x8AF17416, 0x54368A0B);
  HK(LaunchShell, 0xA6605D6F, 0xA1D43805); HK(LaunchName2, 0xA6605D6F, 0x42945D67);
  HK(Quit, 0x8AF17416, 0x0DD747FB); HK(SuspendBg, 0x8AF17416, 0x5BCF6927);
  HK(ResumeBg, 0x8AF17416, 0x64DD4321); HK(LaunchCancel, 0x8AF17416, 0x5369BC0A);
  HK(LaResult, 0x8AF17416, 0x5CF3BA39); HK(OverwriteParam, 0xA6605D6F, 0xEC1FDACE);
  HK(LaunchUri2, 0xA6605D6F, 0x0ED6AF54); HK(DestroyOther, 0x8AF17416, 0x5420D23C);
  Stat_h = taiHookFunctionImport(&Stat_ref, "SceShell", 0xA6605D6F, 0x47958DE5, Stat_p);
  put(Stat_h >= 0 ? "hook Stat ok\n" : "hook Stat FAILED\n");
  HK(Event, 0xA6605D6F, 0xCFAD5A3A); HK(UriLaunch, 0xA6605D6F, 0x003C634F);
  dlg_install();
  job_install();
  hl_install();
  /* cgd_install() is deferred to the watchdog thread (after the shell is ready): the shell loads
   * common_gui_dialog itself, so its imports are not resolved yet while this module starts. */
  SceUID th = sceKernelCreateThread("NoLiveAreaWD", watchdog, 0x10000100, 0x4000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  put(th >= 0 ? "watchdog started\n" : "watchdog FAILED\n");
  return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, const void *argp) {
  (void)args; (void)argp;
  if (ev_hook >= 0) taiHookRelease(ev_hook, ev_ref);
  return SCE_KERNEL_STOP_SUCCESS;
}
