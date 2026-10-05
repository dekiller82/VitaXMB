#pragma once

/*
 * xform.h - the XMB layer in perspective.
 *
 * A theme can turn the whole XMB (the category bar and the lists) in space: Clear XMB Black's topmenu_plugin.rco has a
 * plane around the bar ("basetop") and an animation (XMB_RT) that rotates it 0.6 rad about the vertical axis and -0.3 about the
 * horizontal one, so the right side of the bar recedes. While xf_on is set, every rectangle, picture and glyph the program draws
 * is moved through that turn and a pinhole camera at the PSP's 500 units, then drawn as a quad of its own.
 *
 * The program draws with vita2d's functions; the macros below route the few that matter through the wrappers.
 */

#define XF_CAMERA 500.0f                      /* distance of the camera from the plane, PSP pixels (the value the firmware's own camera uses) */

static int xf_on;                             /* the layer is being drawn through the turn */
static int xf_set_up;                         /* the active theme turns the XMB */
static float xf_m[9];                         /* rotation matrix, row major */
static float xf_ox, xf_oy;                    /* the plane's own position, PSP pixels (y up) */

static void xf_config(int active, float rx, float ry, float rz, float ox, float oy)
{
	xf_set_up = active;
	xf_on = 0;
	if (!active) return;
	float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
	/* right-handed turns, v' = Ry(Rx(Rz v)), the viewer on the +z side: a positive angle about y sends the right side away, a negative one about x the top */
	float rzm[9] = { cz, -sz, 0, sz, cz, 0, 0, 0, 1 };
	float rxm[9] = { 1, 0, 0, 0, cx, -sx, 0, sx, cx };
	float rym[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy };
	float t[9], m[9];
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { t[i * 3 + j] = 0; for (int k = 0; k < 3; k++) t[i * 3 + j] += rxm[i * 3 + k] * rzm[k * 3 + j]; }
	for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { m[i * 3 + j] = 0; for (int k = 0; k < 3; k++) m[i * 3 + j] += rym[i * 3 + k] * t[k * 3 + j]; }
	memcpy(xf_m, m, sizeof(m));
	xf_ox = ox; xf_oy = oy;
}

/* a point on the screen (960x544) as it appears after the turn */
static void xf_pt(float x, float y, float *ox, float *oy)
{
	float lx = x * 0.5f - 240.0f, ly = 136.0f - y * 0.5f;
	float px = xf_m[0] * lx + xf_m[1] * ly + xf_ox;
	float py = xf_m[3] * lx + xf_m[4] * ly + xf_oy;
	float pz = xf_m[6] * lx + xf_m[7] * ly;
	float d = XF_CAMERA - pz;
	if (d < 40.0f) d = 40.0f;
	float s = XF_CAMERA / d;
	*ox = (px * s + 240.0f) * 2.0f;
	*oy = (136.0f - py * s) * 2.0f;
}

static void xf_rect(float x, float y, float w, float h, unsigned int color)
{
	if (!xf_on) { (vita2d_draw_rectangle)(x, y, w, h, color); return; }
	vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (!v) return;
	float cx[4] = { x, x + w, x, x + w }, cy[4] = { y, y, y + h, y + h };
	for (int i = 0; i < 4; i++) {
		float ox, oy;
		xf_pt(cx[i], cy[i], &ox, &oy);
		v[i] = (vita2d_color_vertex){ ox, oy, 0.5f, color };
	}
	vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

/* a quad of texture (tx,ty,tw,th) with its corners at the four given screen points (strip order) */
static void xf_quad(const vita2d_texture *t, const float *px, const float *py, float tx, float ty, float tw, float th, unsigned int color)
{
	vita2d_texture_vertex *v = vita2d_pool_memalign(4 * sizeof(*v), sizeof(*v));
	if (!v) return;
	float W = (float)vita2d_texture_get_width(t), H = (float)vita2d_texture_get_height(t);
	float u0 = tx / W, u1 = (tx + tw) / W, v0 = ty / H, v1 = (ty + th) / H;
	float uu[4] = { u0, u1, u0, u1 }, vv[4] = { v0, v0, v1, v1 };
	for (int i = 0; i < 4; i++) {
		float ox, oy;
		xf_pt(px[i], py[i], &ox, &oy);
		v[i] = (vita2d_texture_vertex){ ox, oy, 0.5f, uu[i], vv[i] };
	}
	vita2d_draw_array_textured(t, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4, color);
}

static void xf_tex_tint_scale(const vita2d_texture *t, float x, float y, float sx, float sy, unsigned int color)
{
	if (!xf_on) { (vita2d_draw_texture_tint_scale)(t, x, y, sx, sy, color); return; }
	float w = vita2d_texture_get_width(t) * sx, h = vita2d_texture_get_height(t) * sy;
	float px[4] = { x, x + w, x, x + w }, py[4] = { y, y, y + h, y + h };
	xf_quad(t, px, py, 0, 0, (float)vita2d_texture_get_width(t), (float)vita2d_texture_get_height(t), color);
}

static void xf_tex_part_rotate(const vita2d_texture *t, float x, float y, float tx, float ty, float tw, float th, float sx, float sy, float rad, unsigned int color)
{
	if (!xf_on) { (vita2d_draw_texture_part_tint_scale_rotate)(t, x, y, tx, ty, tw, th, sx, sy, rad, color); return; }
	float hw = tw * sx * 0.5f, hh = th * sy * 0.5f, c = cosf(rad), s = sinf(rad);
	float ax[4] = { -hw, hw, -hw, hw }, ay[4] = { -hh, -hh, hh, hh }, px[4], py[4];
	for (int i = 0; i < 4; i++) { px[i] = x + ax[i] * c - ay[i] * s; py[i] = y + ax[i] * s + ay[i] * c; }
	xf_quad(t, px, py, tx, ty, tw, th, color);
}

static void xf_array(SceGxmPrimitiveType mode, const vita2d_color_vertex *verts, size_t count)
{
	if (xf_on) {
		vita2d_color_vertex *v = (vita2d_color_vertex *)verts;       /* the caller's pool memory, drawn once */
		for (size_t i = 0; i < count; i++) xf_pt(v[i].x, v[i].y, &v[i].x, &v[i].y);
	}
	vita2d_draw_array(mode, verts, count);
}

#define vita2d_draw_rectangle xf_rect
#define vita2d_draw_texture_tint_scale xf_tex_tint_scale
#define vita2d_draw_texture_part_tint_scale_rotate xf_tex_part_rotate
#define vita2d_draw_array xf_array
