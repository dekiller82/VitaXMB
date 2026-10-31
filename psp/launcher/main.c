/*
 * VitaXMB Launcher - the PSP half of "start this game from VitaXMB".
 *
 * Adrenaline 8 (the virtual PSP on the Vita) can start one program by itself when it opens: "Autorun /PSP/GAME/BOOT/EBOOT.PBP". This is that
 * program. VitaXMB writes the path of the game to ms0:/PSP/GAME/BOOT/VXMB.TXT, switches Autorun on and opens Adrenaline; this reads the path,
 * deletes the file (so it never starts the same game twice) and starts the ISO / CSO or the EBOOT.PBP the way Adrenaline's own XMB does
 * (see vshctrl/src/patch_vsh.c in https://github.com/isage/Adrenaline). With no VXMB.TXT it simply goes back to the XMB.
 *
 * GPL-3.0, like VitaXMB.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspumd.h>
#include <pspinit.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <kubridge.h>

PSP_MODULE_INFO("VXMBBOOT", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

#define TARGET "ms0:/PSP/GAME/BOOT/VXMB.TXT"

static int has_ext(const char *p, const char *ext)
{
	size_t n = strlen(p), e = strlen(ext);
	if (n < e) return 0;
	for (size_t i = 0; i < e; i++)
		if (tolower((unsigned char)p[n - e + i]) != ext[i]) return 0;
	return 1;
}

/* a PS1 classic: its DATA.PSAR starts with PSISOIMG / PSTITLEI */
static int is_pops(const char *path)
{
	SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (fd < 0) return 0;
	unsigned char h[0x28];
	int ok = sceIoRead(fd, h, sizeof(h)) == sizeof(h) && !memcmp(h, "\0PBP", 4);
	int pops = 0;
	if (ok) {
		unsigned psar = h[0x24] | (h[0x25] << 8) | (h[0x26] << 16) | ((unsigned)h[0x27] << 24);
		char m[8];
		if (psar && sceIoLseek32(fd, (int)psar, PSP_SEEK_SET) == (int)psar && sceIoRead(fd, m, 8) == 8)
			pops = !memcmp(m, "PSISOIMG", 8) || !memcmp(m, "PSTITLEI", 8);
	}
	sceIoClose(fd);
	return pops;
}

/* VXMB.LOG next to VXMB.TXT: what was asked and what the loadexec calls answered (the last line tells how far it got when the system reboots into the game) */
static void logmsg(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n <= 0) return;
	SceUID fd = sceIoOpen("ms0:/PSP/GAME/BOOT/VXMB.LOG", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd < 0) return;
	sceIoWrite(fd, b, n > (int)sizeof(b) ? (int)sizeof(b) : n);
	sceIoClose(fd);
}

/* The block the stock XMB passes to the loadexec call (vshmain.prx 0xd434 fills it): the SDK's structure has nine words; the system's is 0x30 bytes, and after
 * unk5 come the size and the bytes of the game's PARAM.SFO. POPS reads the game's identity from there; without it the PS1 emulator stops after the restart with
 * 80010002. */
typedef struct {
	SceSize size, args;
	void *argp;
	const char *key;
	unsigned int vshmain_args_size;
	void *vshmain_args;
	char *configfile;
	void *unk4, *unk5;
	unsigned int extra_size;
	void *extra;
	unsigned int unk2c;
} VxmbLoadParam;

/* the PARAM.SFO inside an EBOOT.PBP (header: 0x08 offset of PARAM.SFO, 0x0C offset of ICON0.PNG); 0 when there is none or it is larger than max */
static int read_pbp_sfo(const char *path, void *buf, int max)
{
	SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (fd < 0) return 0;
	unsigned int h[10];
	int size = 0;
	if (sceIoRead(fd, h, sizeof(h)) == sizeof(h) && h[0] == 0x50425000u && h[3] > h[2] && h[3] - h[2] <= (unsigned)max) {
		if (sceIoLseek32(fd, (int)h[2], PSP_SEEK_SET) == (int)h[2] && sceIoRead(fd, buf, (int)(h[3] - h[2])) == (int)(h[3] - h[2])) size = (int)(h[3] - h[2]);
	}
	sceIoClose(fd);
	return size;
}

/* a value of a PARAM.SFO: a string (returned as a pointer into the buffer) or an integer; 0 / NULL when the key is not there */
static const char *sfo_find(const unsigned char *sfo, int n, const char *key, int *ival)
{
	if (n < 20 || memcmp(sfo, "\0PSF", 4)) return NULL;
	unsigned int ktab, dtab, cnt;
	memcpy(&ktab, sfo + 8, 4); memcpy(&dtab, sfo + 12, 4); memcpy(&cnt, sfo + 16, 4);
	for (unsigned int i = 0; i < cnt && 20 + 16 * (i + 1) <= (unsigned)n; i++) {
		unsigned short ko, fmt;
		unsigned int len, dof;
		memcpy(&ko, sfo + 20 + 16 * i, 2); memcpy(&fmt, sfo + 22 + 16 * i, 2); memcpy(&len, sfo + 24 + 16 * i, 4); memcpy(&dof, sfo + 32 + 16 * i, 4);
		if (ktab + ko >= (unsigned)n || dtab + dof >= (unsigned)n) continue;
		if (strcmp((const char *)sfo + ktab + ko, key)) continue;
		if (fmt == 0x0404) { if (ival) memcpy(ival, sfo + dtab + dof, 4); return (const char *)sfo + dtab + dof; }
		return (const char *)sfo + dtab + dof;
	}
	return NULL;
}

/* What the stock XMB does just before it starts a PS1 game (captured with psp/capture, vshmain.prx 0xe260 -> SysMemForKernel 0x807179E7): it hands the game's
 * identity to the system: sceKernelSetGameInfo(DISC_ID, PARENTAL_LEVEL, GAMEDATA_ID or NULL, APP_VER or "00.00", BOOTABLE, 0, the firmware version "6.61").
 * Without it POPS does not know which game it is starting and stops after the restart with 80010002. A kernel function: reached through kubridge. */
static int set_game_info(const unsigned char *sfo, int n)
{
	int parental = 0, bootable = 1;
	const char *disc = sfo_find(sfo, n, "DISC_ID", NULL);
	const char *gamedata = sfo_find(sfo, n, "GAMEDATA_ID", NULL);
	const char *appver = sfo_find(sfo, n, "APP_VER", NULL);
	sfo_find(sfo, n, "PARENTAL_LEVEL", &parental);
	sfo_find(sfo, n, "BOOTABLE", &bootable);
	unsigned int fn = sctrlHENFindFunction("sceSystemMemoryManager", "SysMemForKernel", 0x807179E7);
	if (!fn || !disc) { logmsg("game info: not set (function %08X, DISC_ID %s)\n", fn, disc ? disc : "missing"); return -1; }
	static char fw[] = "6.61";
	static char defver[] = "00.00";
	KernelCallArg a;
	memset(&a, 0, sizeof(a));
	a.arg1 = (unsigned int)disc;
	a.arg2 = (unsigned int)parental;
	a.arg3 = (unsigned int)gamedata;
	a.arg4 = (unsigned int)(appver ? appver : defver);
	a.arg5 = (unsigned int)bootable;
	a.arg6 = 0;
	a.arg7 = (unsigned int)fw;
	int r = kuKernelCall((void *)fn, &a);
	logmsg("game info '%s' parental %d bootable %d -> %08X (ret %08X)\n", disc, parental, bootable, r, a.ret1);
	return r;
}

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;
	char path[256];
	memset(path, 0, sizeof(path));

	SceUID fd = sceIoOpen(TARGET, PSP_O_RDONLY, 0);
	if (fd >= 0) {
		int n = sceIoRead(fd, path, sizeof(path) - 1);
		sceIoClose(fd);
		sceIoRemove(TARGET);                                   /* first of all: never start it twice */
		if (n < 0) n = 0;
		path[n] = 0;
		while (n > 0 && (path[n - 1] == '\r' || path[n - 1] == '\n' || path[n - 1] == ' ')) path[--n] = 0;
	}
	logmsg("launcher: path '%s'\n", path);
	if (!path[0]) {                                            /* nothing to start: the XMB */
		sctrlKernelExitVSH(NULL);
		return 0;
	}

	SceKernelLoadExecVSHParam param;
	memset(&param, 0, sizeof(param));
	param.size = sizeof(param);

	if (has_ext(path, ".iso") || has_ext(path, ".cso") || has_ext(path, ".zso") || has_ext(path, ".dax") || has_ext(path, ".jso")) {
		static char eboot[] = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
		sctrlSEMountUmdFromFile(path, 0, 0);                   /* the image is the "disc" */
		sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
		sctrlSESetBootConfFileIndex(MODE_INFERNO);             /* the Inferno driver, Adrenaline's default */
		param.args = strlen(eboot) + 1;
		param.argp = eboot;
		param.key = "umdemu";
		sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_UMD_EMU_MS1, eboot, &param);
	} else {
		param.args = strlen(path) + 1;
		param.argp = path;
		if (is_pops(path)) {
			static char vsh_args[0x400];                                /* what the stock XMB hands over besides: the vshmain arguments, a 0x400 byte block */
			static unsigned char sfo[0x1400];
			static VxmbLoadParam pp;
			int r, n = read_pbp_sfo(path, sfo, sizeof(sfo));
			memset(&pp, 0, sizeof(pp));
			pp.size = sizeof(pp);
			pp.args = strlen(path) + 1;
			pp.argp = path;
			pp.key = "pops";
			pp.vshmain_args_size = sizeof(vsh_args);
			pp.vshmain_args = vsh_args;
			pp.extra_size = n;
			pp.extra = n ? sfo : NULL;
			/* this program was started as a game, which made Adrenaline select an ISO driver for it (Inferno); the XMB starts POPS with the plain UMD setting */
			logmsg("boot config index was %d\n", (int)sctrlSEGetBootConfFileIndex());
			sctrlSESetBootConfFileIndex(MODE_UMD);
			sctrlSESetUmdFile("");
			if (n) set_game_info(sfo, n);
			logmsg("pops %s: apitype 0x%03X with PARAM.SFO (%d bytes)\n", path, PSP_INIT_APITYPE_MS5, n);
			r = sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS5, path, (SceKernelLoadExecVSHParam *)&pp);
			logmsg("  -> %08X\n", r);
			param.key = "pops";
			logmsg("pops %s: plain\n", path);
			r = sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS5, path, &param);
			logmsg("  -> %08X\n", r);
		} else {
			param.key = "game";
			logmsg("game %s\n", path);
			int r = sctrlKernelLoadExecVSHMs2(path, &param);
			logmsg("  -> %08X\n", r);
		}
	}

	sctrlKernelExitVSH(NULL);                                  /* it did not start: the XMB */
	return 0;
}
