/* plasma - KallistiOS plasma demo (c)2002 Megan Potter, ported to libpvr.
 *
 * A tiny 64x64 plasma texture is regenerated on the CPU each frame into a
 * twiddled RGB565 buffer, uploaded to VRAM, and stretched over the whole
 * screen by the PVR with bilinear filtering -- the hardware does the smooth
 * upscale for free.  This is the first libpvr demo to exercise textured polys
 * and per-frame texture upload (pvr_mem_malloc / pvr_txr_load).
 *
 * Port changes from the KOS original: kos.h -> libpvr.h; pvr_init_defaults ->
 * pvr_init; KOS fast-math (fcos/fsin/F_PI) -> libm; PVR_PACK_COLOR and the
 * cosmetic vid_border_color() provided locally; the START-button quit replaced
 * by an endless loop (stop from the serial console).
 */
#include <libpvr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define F_PI 3.14159265358979f

/* Pack float [0,1] ARGB into a 32-bit PVR colour (KOS PVR_PACK_COLOR). */
#define PVR_PACK_COLOR(a, r, g, b) ( \
	((uint32_t)((a) * 255) << 24) | ((uint32_t)((r) * 255) << 16) | \
	((uint32_t)((g) * 255) << 8)  |  (uint32_t)((b) * 255))

/* Cosmetic on real KOS (Holly border colour = frame-time indicator); no-op. */
static inline void vid_border_color(int r, int g, int b)
{
	(void)r; (void)g; (void)b;
}

static float scols[8][3] = {
	{ 1.0f, 0.0f, 0.0f },   // Red
	{ 1.0f, 0.0f, 1.0f },   // Magenta
	{ 0.0f, 0.0f, 1.0f },   // Blue
	{ 0.0f, 1.0f, 1.0f },   // Cyan
	{ 0.0f, 1.0f, 0.0f },   // Green
	{ 1.0f, 1.0f, 0.0f },   // Yellow
	{ 1.0f, 0.0f, 0.0f },   // Red
	{ 1.0f, 0.0f, 1.0f }    // Magenta
};

void hsv_spectrum(float w, float *r, float *g, float *b)
{
	int w1 = (int)(w * 6);
	float i = 1.0f - ((w * 6) - w1);

	*r = scols[w1][0] * i + scols[w1 + 1][0] * (1.0f - i);
	*g = scols[w1][1] * i + scols[w1 + 1][1] * (1.0f - i);
	*b = scols[w1][2] * i + scols[w1 + 1][2] * (1.0f - i);
}

/* Linear/iterative twiddling algorithm from Marcus' tatest */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

short pcos[512], psin[512];
short xang, yang, xamp, yamp;
float hsv;

pvr_poly_hdr_t hdr[2];
pvr_ptr_t txr[2];
uint16_t *txr_buf[2];
int txr_cur;

void plasma_init(void)
{
	int i;

	xang = yang = 0;
	xamp = yamp = 256;
	hsv = 0.0f;

	for(i = 0; i < 512; i++) {
		pcos[i] = (short)(cosf(i * 2 * (2 * F_PI / 512.0f)) * 256.0f);
		psin[i] = (short)(sinf(i * 2 * (2 * F_PI / 512.0f)) * 256.0f);
	}
}

void plasma_drawtex(void)
{
	uint16_t *vrout = (uint16_t *)(txr_buf[txr_cur]);
	int x, y, p, q, r;

	xang++;
	yang--;
	xang &= 511;
	yang &= 511;
	xamp = 2 * pcos[yang];
	yamp = 2 * psin[xang];

	for(y = 30; y < 30 + 64; y++) {
		for(x = 30; x < 30 + 64; x++) {
			p = x + (psin[(xang + (y * 2 + y)) & 511] * xamp / 256);
			q = y + (pcos[(yang + (x * 4)) & 511] * yamp / 256);
			r = ((p + q) & 255);

			if(r >= 128)
				r = 255 - r;

			p = (r >> (8 - 5));
			r = (r >> (8 - 6));
			vrout[TWIDOUT(x - 30, y - 30)] =
				(p << 11) | (r << 5) | p;
		}
	}

	pvr_txr_load(txr_buf[txr_cur], txr[txr_cur], 64 * 64 * 2);
}

void pvr_setup(void)
{
	pvr_poly_cxt_t cxt;
	int i;

	pvr_set_bg_color(0, 0, 0);

	for(i = 0; i < 2; i++) {
		txr[i] = pvr_mem_malloc(64 * 64 * 2);
		txr_buf[i] = aligned_alloc(32, 64 * 64 * 2);
		memset(txr_buf[i], 0, 64 * 64 * 2);

		pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
				 64, 64, txr[i], PVR_FILTER_BILINEAR);
		pvr_poly_compile(&hdr[i], &cxt);
	}

	txr_cur = 0;
}

void do_frame(void)
{
	pvr_vertex_t vert;
	float r, g, b;

	vid_border_color(255, 0, 0);
	pvr_wait_ready();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_prim(&hdr[txr_cur], sizeof(*hdr));

	hsv_spectrum(hsv, &r, &g, &b);

	vert.flags = PVR_CMD_VERTEX;
	vert.x = 0.0f;
	vert.y = 480.0f;
	vert.z = 1.0f;
	vert.u = 0.0f;
	vert.v = 1.0f;
	vert.argb = PVR_PACK_COLOR(1.0f, r, g, b);
	vert.oargb = 0;
	pvr_prim(&vert, sizeof(vert));

	vert.y = 0.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = 640.0f;
	vert.y = 480.0f;
	vert.u = 1.0f;
	vert.v = 1.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.flags = PVR_CMD_VERTEX_EOL;
	vert.y = 0.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	pvr_list_finish();
	pvr_scene_finish();

	vid_border_color(255, 255, 0);

	plasma_drawtex();
	txr_cur ^= 1;
	hsv += 1.0f / 480.0f;

	while(hsv >= 1.0f)
		hsv -= 1.0f;
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);

	if(pvr_init() < 0) { printf("pvr_init failed\n"); return 1; }
	printf("plasma: %dx%d, rendering (kill from serial to stop)\n",
	       pvr_screen_w, pvr_screen_h);

	pvr_setup();
	plasma_init();

	for(;;)
		do_frame();

	return 0;
}
