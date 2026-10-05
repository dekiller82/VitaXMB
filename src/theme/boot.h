/*
 * boot.h - the PSP's cold-boot animation, played from opening_plugin.rco.
 *
 * Nothing about the show is written down here except what the plugin's own code does: it starts three animations
 * of the RCO (anim_logo, the month's anim_month_NN, anim_model), draws the planes of page_bg, the ribbon model of
 * page_3d and the logo of page_logo with whatever the animations make of them, and answers the events they fire:
 *   native:/system_bg_plane_show  the system's sky is shown beneath (it fades in)
 *   native:/system_bg_model_show  the system's wave model is shown: the ribbon hands over to it
 *   native:/anim_logo_finished    the plugin is done
 * A theme's own opening_plugin.rco is used when it has one.
 */

static float boot_sky_t0, boot_hand_t, boot_end_t;
static float boot_handoff_ms;                                 /* how long the background wave has been running when the boot ends */
static uint64_t boot_hand_abs;                                /* process time (us) at which the wave began */
static int boot_played;                                       /* set once the animation ran, so the XMB skips its own fade from black */

static void boot_event(const char *ev, float t, void *user)
{
	(void)user;
	if (strstr(ev, "system_bg_plane_show")) boot_sky_t0 = t;
	else if (strstr(ev, "system_bg_model_show")) boot_hand_t = t;
	else if (strstr(ev, "anim_logo_finished")) boot_end_t = t;
}

static uint8_t *boot_read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = sz > 0 && sz < 16 * 1024 * 1024 ? malloc((size_t)sz) : NULL;
	if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
	fclose(f);
	*n = b ? (size_t)sz : 0;
	return b;
}

/* Plays the animation; any of Cross, Circle or Start skips it. month is 0..11, col the background wave's colour. */
static void text_prewarm(int steps);                   /* text.h: builds the letters the menus will need, a few a frame */

static void boot_play(int month, void (*backdrop)(float, float), const unsigned char col[3], void (*shot)(const char *))
{
	size_t n = 0;
	Rco *r = NULL;
	uint8_t *d = pt_active_file("/vsh/resource/opening_plugin.rco", &n);        /* the theme's own, if it has one */
	if (d) r = rco_open(d, n);
	if (!r) {
		d = boot_read_file("app0:assets/psp/opening_plugin.rco", &n);
		if (d) r = rco_open(d, n);
	}
	if (!r) { trace("boot: no readable opening_plugin.rco\n"); return; }

	boot_sky_t0 = boot_hand_t = boot_end_t = 1.0e9f;
	RcoPlayer logo, mon, model;
	char mname[24];
	snprintf(mname, sizeof(mname), "anim_month_%02d", month + 1);
	int ok = rco_play_start(&logo, r, "anim_logo", boot_event, NULL);
	rco_play_start(&mon, r, mname, boot_event, NULL);
	rco_play_start(&model, r, "anim_model", boot_event, NULL);
	size_t glen = 0;
	const uint8_t *gmo = rco_model(r, &glen);
	trace("boot: rco ok, anim_logo %d, model %s (%u bytes)\n", ok, gmo ? "yes" : "no", (unsigned)glen);
	if (!ok) { rco_play_free(&logo); rco_play_free(&mon); rco_play_free(&model); rco_free(r); return; }
	if (gmo) wv_install(&boot_m, gmo, glen, NULL, 0);           /* a theme may ship a broken or empty ribbon: the rest of the show still plays */

	int n_logo = rco_find(r, "logo"), n_model = rco_find(r, "model"), n_bg = rco_find(r, "page_bg");

	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	sceCtrlPeekBufferPositive(0, &pad, 1);
	unsigned int held0 = pad.buttons;                          /* a button already down at start does not skip */
	uint64_t t0 = sceKernelGetProcessTimeWide();
	static const unsigned char white[3] = { 255, 255, 255 };
	boot_hand_abs = 0;
	static int shot_i;
	float shot_ms = 2500.0f;                                   /* debug: the moment to capture is written in ux0:data/VitaXMB/bootshot */
	if (shot) {
		FILE *sf = fopen("ux0:data/VitaXMB/bootshot", "rb");
		char sb[16] = "";
		if (sf) { size_t k = fread(sb, 1, sizeof(sb) - 1, sf); sb[k] = 0; fclose(sf); }
		if (atoi(sb) > 0) shot_ms = (float)atoi(sb);
	}
	float ms = 0.0f;
	for (;;) {
		ms = (float)(sceKernelGetProcessTimeWide() - t0) / 1000.0f;
		rco_play_step(&logo, ms);
		rco_play_step(&mon, ms);
		rco_play_step(&model, ms);
		if (ms >= boot_end_t + 17.0f) break;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		unsigned int fresh = pad.buttons & ~held0;
		held0 &= pad.buttons;
		if (fresh & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) break;

		/* system_bg_plane_show calls the system's show with a fade time of 0.0: the sky is simply there from that moment */
		float sky = ms >= boot_sky_t0 ? 1.0f : 0.0f;
		float hand = boot_hand_t > boot_sky_t0 ? (ms - boot_sky_t0) / (boot_hand_t - boot_sky_t0) : 1.0f;     /* 0..1 up to the ribbon becoming the wave */
		if (hand < 0.0f) hand = 0.0f;
		if (hand > 1.0f) hand = 1.0f;
		flush_free();
		vita2d_start_drawing();
		vita2d_clear_screen();
		/* the opening plugin draws over the system background: first the sky (once shown), then page_bg, the ribbon, the logo */
		if (sky > 0.0f && backdrop) backdrop(0.0f, sky);
		if (n_bg >= 0)
			for (int c = r->nodes[n_bg].child; c >= 0; c = r->nodes[c].next)
				if (r->nodes[c].type == 2) rco_draw_plane(r, c, 1.0f, 0);
		if (n_model >= 0 && boot_m.ok) {
			float ma = r->nodes[n_model].a * r->nodes[n_model].ca;
			float fr = ms * boot_m.fps / 1000.0f;
			if (fr > boot_m.end) fr = boot_m.end;
			unsigned char rc[3];                                     /* white at first, the wave's own colour by the time it takes over */
			for (int i = 0; i < 3; i++) rc[i] = 255; (void)hand; (void)col;
			if (ma > 1.0f) ma = 1.0f;
			if (ma > 0.0f && ms < boot_hand_t) wv_draw(&boot_m, fr, rc, ma, 1);
		}
		if (ms >= boot_hand_t) {                                    /* system_bg_model_show: the system's wave takes over in the ribbon's last pose, with no frame in between */
			if (!boot_hand_abs) boot_hand_abs = t0 + (uint64_t)(boot_hand_t * 1000.0f);
			wave_draw((ms - boot_hand_t) / 1000.0f, white, 1.0f);
		}
		if (n_logo >= 0) rco_draw_plane(r, n_logo, 1.0f, 0);
#ifdef VITAXMB_DEBUG
		{
			static int tr_i;
			if (ms >= tr_i * 250.0f && n_logo >= 0) {
				const RcoNode *lg = &r->nodes[n_logo];
				trace("boot %4.0f ms: logo a=%.2f ca=%.2f rgb=%.2f/%.2f/%.2f size=%.0fx%.0f tex=%p sky=%.2f\n", ms, lg->a, lg->ca, lg->r, lg->g, lg->b, lg->sx, lg->sy, (void *)rco_node_image(r, n_logo), sky);
				tr_i++;
			}
		}
#endif
		vita2d_end_drawing();
		text_prewarm(8);                                           /* the first menu frame would otherwise build every letter at once: a visible hitch at the hand-over */
		if (shot && shot_i < 1 && ms >= shot_ms) {
			char nm[24];
			snprintf(nm, sizeof(nm), "bs%d", shot_i++);
			shot(nm);
		}
		vita2d_swap_buffers();
	}
	boot_handoff_ms = boot_hand_t < 1.0e8f ? ms - boot_hand_t : 0.0f;
	if (boot_handoff_ms < 0.0f) boot_handoff_ms = 0.0f;
	vita2d_wait_rendering_done();
	rco_play_free(&logo); rco_play_free(&mon); rco_play_free(&model);
	rco_free(r);
	wv_free(&boot_m);
	boot_played = 1;
}
