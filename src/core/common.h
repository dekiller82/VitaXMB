#pragma once

/*
 * VitaXMB - a PSP-style XMB launcher for the PS Vita.
 * Scans ux0:app for installed games/homebrew and launches them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/audioout.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <psp2/display.h>
#include <psp2/registrymgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/rng.h>
#include <mbedtls/sha1.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <zlib.h>
#include <psp2/sysmodule.h>
#include <psp2/promoterutil.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <setjmp.h>
#include <png.h>
#include <taihen.h>
#include <psp2/vshbridge.h>
#include "vitashell_user.h"
#define MINIMP3_NO_STDIO
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <harfbuzz/hb.h>
#include <vita2d.h>

#define SCREEN_W   960
#define SCREEN_H   544
#define OWN_TITLEID "VXMB00001"
#define CONFIG_DIR  "ux0:data/VitaXMB"
#define CONFIG_PATH CONFIG_DIR "/config.bin"

#define MAX_ITEMS  256
#define ICON_KEEP  7        /* icons kept loaded within +/- this many rows */

#define WHITE(a) RGBA8(255, 255, 255, (a))

/* Textures the GPU may still be reading must not be freed mid-frame (that is what produces
 * GPUCRASH dumps). They are parked here and released after the GPU has gone idle. */
static vita2d_texture *free_list[256];
static int free_count;

static void defer_free(vita2d_texture *t)
{
	if (!t) return;
	if (free_count < 256) { free_list[free_count++] = t; return; }
	vita2d_wait_rendering_done();
	vita2d_free_texture(t);
}

static void trace(const char *fmt, ...);
static void flush_free(void)
{
	static int age;
	if (!free_count) { age = 0; return; }
	if (free_count < 8 && ++age < 45) return;      /* let a few pile up: every flush waits for the GPU */
	age = 0;
	trace("free %d textures\n", free_count);
	vita2d_wait_rendering_done();
	for (int i = 0; i < free_count; i++) vita2d_free_texture(free_list[i]);
	free_count = 0;
}
