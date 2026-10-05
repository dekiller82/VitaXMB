/*
 * VitaXMB Updater - installs the update the main app has unpacked into ux0:data/pkg, then starts VitaXMB again.
 *
 * An app cannot be replaced while it runs, so the main app hands over to this small one (the same arrangement VitaShell uses with its
 * "VitaShell Updater"). The package was already prepared by VitaXMB (unpacked, head.bin made); this closes the other apps, asks the system's
 * package installer to install it (on a worker thread, while a message and a spinner are shown), writes the outcome to
 * ux0:data/VitaXMB/update_result.txt and launches VitaXMB.
 *
 * The installing method follows VitaShell (GPL-3.0, Copyright (C) 2015-2018 TheFloW; updater/main.c and package_installer.c).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>

#define PACKAGE_DIR "ux0:data/pkg"
#define RESULT_FILE "ux0:data/VitaXMB/update_result.txt"
#define MAIN_TITLEID "VXMB00001"

static volatile int g_result = 1;                                     /* 1 while installing, then the installer's result */
static volatile int g_done;

static void write_result(const char *text)
{
	SceUID fd = sceIoOpen(RESULT_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, text, strlen(text));
		sceIoClose(fd);
	}
}

static int load_paf(void)
{
	static uint32_t argp[] = { 0x180000, (uint32_t)-1, (uint32_t)-1, 1, (uint32_t)-1, (uint32_t)-1 };
	int result = -1;
	uint32_t opt[4] = { sizeof(opt), (uint32_t)(uintptr_t)&result, (uint32_t)-1, (uint32_t)-1 };
	return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, (const SceSysmoduleOpt *)opt);
}

static int promote_app(const char *path)
{
	int res = load_paf();
	if (res < 0) return res;
	res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	if (res >= 0) res = scePromoterUtilityInit();
	if (res >= 0) res = scePromoterUtilityPromotePkgWithRif(path, 1);
	scePromoterUtilityExit();
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
	uint32_t unload = 0;
	sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, (const SceSysmoduleOpt *)&unload);
	return res;
}

static int install_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	g_result = promote_app(PACKAGE_DIR);
	g_done = 1;
	return 0;
}

static void draw_text_centre(vita2d_pgf *pgf, float y, float scale, unsigned color, const char *text)
{
	int w = vita2d_pgf_text_width(pgf, scale, text);
	vita2d_pgf_draw_text(pgf, 480 - w / 2, (int)y, color, scale, text);
}

int main(int argc, const char *argv[])
{
	(void)argc; (void)argv;
	sceAppMgrDestroyOtherApp();                                       /* VitaXMB is closed so its files can be replaced */


	vita2d_init();
	vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
	vita2d_pgf *pgf = vita2d_load_default_pgf();

	SceUID th = sceKernelCreateThread("updater_install", install_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if (th >= 0) sceKernelStartThread(th, 0, NULL);
	else { g_result = th; g_done = 1; }

	int frame = 0, done_frames = 0;
	for (;;) {
		vita2d_start_drawing();
		vita2d_clear_screen();
		char line[64];
		if (!g_done) {
			draw_text_centre(pgf, 230, 1.6f, RGBA8(255, 255, 255, 255), "Installing the VitaXMB update");
			snprintf(line, sizeof(line), "Please wait... %d s", frame / 60);
			draw_text_centre(pgf, 280, 1.1f, RGBA8(200, 200, 200, 255), line);
			/* a spinner: a ring of dots with one lit */
			for (int i = 0; i < 12; i++) {
				float ang = i * 6.2831853f / 12.0f;
				int lit = ((frame / 4) % 12 == i);
				vita2d_draw_fill_circle(480 + cosf(ang) * 28.0f, 380 + sinf(ang) * 28.0f, lit ? 6.0f : 3.0f, lit ? RGBA8(255, 255, 255, 255) : RGBA8(120, 120, 120, 255));
			}
		} else {
			if (g_result >= 0) {
				draw_text_centre(pgf, 250, 1.6f, RGBA8(255, 255, 255, 255), "Update installed");
				draw_text_centre(pgf, 300, 1.1f, RGBA8(200, 200, 200, 255), "Starting VitaXMB...");
			} else {
				snprintf(line, sizeof(line), "The update could not be installed (%08X)", (unsigned)g_result);
				draw_text_centre(pgf, 250, 1.4f, RGBA8(255, 120, 120, 255), line);
				draw_text_centre(pgf, 300, 1.1f, RGBA8(200, 200, 200, 255), "Starting VitaXMB...");
			}
			if (++done_frames > 90) break;
		}
		vita2d_end_drawing();
		vita2d_swap_buffers();
		frame++;
	}

	char msg[48];
	if (g_result < 0) snprintf(msg, sizeof(msg), "failed %08X", (unsigned)g_result);
	else snprintf(msg, sizeof(msg), "ok");
	write_result(msg);
	vita2d_wait_rendering_done();
	sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" MAIN_TITLEID);
	sceKernelDelayThread(1000);
	sceKernelExitProcess(0);
	return 0;
}
