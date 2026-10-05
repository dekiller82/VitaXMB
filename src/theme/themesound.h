#pragma once

/*
 * themesound.h - a theme's sounds. They are Sony ADPCM ("VAGp") samples in the sound table of the theme's system_plugin.rco:
 * snd_cursor (moving), snd_decide (confirm), snd_cancel, snd_category_decide (changing category), snd_option, snd_error.
 * A theme replaces the app's own samples; leaving a theme brings them back.
 */

static const struct { const char *label; int slot; } thsnd[] = {
	{ "snd_cursor", SND_CURSOR }, { "snd_decide", SND_CONFIRM }, { "snd_cancel", SND_CANCEL },
	{ "snd_category_decide", SND_CATEGORY }, { "snd_option", SND_OPTION }, { "snd_error", SND_ERROR },
};

static void sound_apply_theme(void)
{
	for (unsigned k = 0; k < sizeof(thsnd) / sizeof(thsnd[0]); k++) {          /* the app's own first */
		int slot = thsnd[k].slot, frames = 0;
		int16_t *pcm = sound_read_asset(slot, &frames);
		sound_set(slot, pcm, pcm ? frames : 0);
	}
	if (pt_active < 0) return;
	size_t n = 0;
	uint8_t *d = pt_active_file("/vsh/resource/system_plugin.rco", &n);
	if (!d) return;
	Rco *r = rco_open(d, n);
	if (!r) return;
	uint32_t pS = r->H[7], pD = r->H[34];
	if (pS != 0xFFFFFFFFu && pD != 0xFFFFFFFFu && pS + 0x28 <= r->size) {
		uint32_t count = rco_u32(r->data + pS + 16), pos = pS + 0x28;
		for (uint32_t i = 0; i < count && pos + 0x38 <= r->size; i++) {
			uint32_t lab = rco_u32(r->data + pos + 4), next = rco_u32(r->data + pos + 20);
			const uint8_t *e = r->data + pos + 0x28;
			uint32_t off = rco_u32(e + 8);
			if (lab != 0xFFFFFFFFu && r->H[16] + lab < r->size && (size_t)pD + off < r->size)
				for (unsigned k = 0; k < sizeof(thsnd) / sizeof(thsnd[0]); k++)
					if (strcmp((const char *)r->data + r->H[16] + lab, thsnd[k].label) == 0) {
						int frames = 0;
						int16_t *pcm = vag_to_pcm(r->data + pD + off, r->size - pD - off, &frames);
						if (pcm) sound_set(thsnd[k].slot, pcm, frames);
						trace("theme sound %s: %d frames\n", thsnd[k].label, pcm ? frames : 0);
					}
			pos += next ? next : 0x38;
		}
	}
	rco_free(r);
}
