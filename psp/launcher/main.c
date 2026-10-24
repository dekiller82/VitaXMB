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
			param.key = "pops";
			sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS5, path, &param);
		} else {
			param.key = "game";
			sctrlKernelLoadExecVSHMs2(path, &param);
		}
	}

	sctrlKernelExitVSH(NULL);                                  /* it did not start: the XMB */
	return 0;
}
