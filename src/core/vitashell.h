#pragma once

/* ---- VitaShell's modules: mount a game's encrypted PFS so its files read as plain ---- */

#define VS_DIR "ux0:VitaShell/module/"
#define CACHE_ROOT CONFIG_DIR "/cache"

static SceUID vs_patch_id = -1, vs_kernel_id = -1, vs_user_id = -1;
static int vs_state;                    /* 0 untried, 1 ready, -1 unavailable */
static char vs_mp[64];

static int vs_load_kernel(const char *name, const char *file, SceUID *id)
{
	int unk[2];
	if (_vshKernelSearchModuleByName(name, unk) >= 0) return 1;     /* already loaded */
	SceUID m = taiLoadKernelModule(file, 0, NULL);
	if (m < 0) { xlog("tai load %s: %08x\n", file, m); return 0; }
	int r = taiStartKernelModule(m, 0, NULL, 0, NULL, NULL);
	if (r < 0) {
		xlog("tai start %s: %08x\n", file, r);
		taiStopUnloadKernelModule(m, 0, NULL, 0, NULL, NULL);
		return 0;
	}
	*id = m;
	return 1;
}

static int vs_init(void)
{
	if (vs_state) return vs_state > 0;
	vs_state = -1;
	SceIoStat st;
	if (sceIoGetstat(VS_DIR "user.suprx", &st) < 0 || sceIoGetstat(VS_DIR "kernel.skprx", &st) < 0 ||
	    sceIoGetstat(VS_DIR "patch.skprx", &st) < 0) {
		xlog("VitaShell modules not found in " VS_DIR "\n");
		return 0;
	}
	vs_load_kernel("VitaShellPatch", VS_DIR "patch.skprx", &vs_patch_id);
	if (!vs_load_kernel("VitaShellKernel2", VS_DIR "kernel.skprx", &vs_kernel_id)) return 0;
	vs_user_id = sceKernelLoadStartModule(VS_DIR "user.suprx", 0, NULL, 0, NULL, NULL);
	if (vs_user_id < 0) { xlog("user.suprx: %08x\n", vs_user_id); return 0; }
	vs_state = 1;
	return 1;
}

/* Unload whatever this app loaded, so VitaShell can load its own copies later. */
static void vs_shutdown(void)
{
	if (vs_user_id >= 0) { sceKernelStopUnloadModule(vs_user_id, 0, NULL, 0, NULL, NULL); vs_user_id = -1; }
	if (vs_kernel_id >= 0) { taiStopUnloadKernelModule(vs_kernel_id, 0, NULL, 0, NULL, NULL); vs_kernel_id = -1; }
	if (vs_patch_id >= 0) { taiStopUnloadKernelModule(vs_patch_id, 0, NULL, 0, NULL, NULL); vs_patch_id = -1; }
	vs_state = 0;
}

static int vs_mount(const char *path)
{
	static const int ids[] = { 0x6E, 0x12E, 0x12F, 0x3ED };
	char klicensee[0x10];
	memset(klicensee, 0, sizeof(klicensee));
	vs_mp[0] = 0;
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		ShellMountIdArgs args;
		memset(&args, 0, sizeof(args));
		args.id = ids[i];
		args.process_titleid = "VITASHELL";
		args.path = path;
		args.klicensee = klicensee;
		args.mount_point = vs_mp;
		int r = shellUserMountById(&args);
		if (r >= 0) return r;
	}
	return sceAppMgrGameDataMount(path, 0, 0, vs_mp);
}

static void vs_umount(void)
{
	if (vs_mp[0]) { sceAppMgrUmount(vs_mp); vs_mp[0] = 0; }
}
