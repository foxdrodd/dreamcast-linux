/* tunnel - the classic demoscene infinite tunnel, on libpvr.
 *
 * For each low-res output pixel we precompute (once) a texture coordinate from
 * its angle around the centre and its inverse distance to it -- angle -> U wraps
 * around the tunnel, 1/distance -> V is depth.  Each frame we just add a scroll
 * offset (forward motion + rotation) and sample a small tileable source texture,
 * plus a precomputed per-pixel shade for distance fog.  The PVR bilinear-upscales
 * the 128x128 result to the whole screen -- same dynamic-texture path as plasma
 * and fire, but the heavy per-pixel trig is all in the one-time table build.
 */
#include <libpvr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define FW  128          /* output (screen) low-res */
#define FH  128
#define SRC 64           /* source texture, tileable (power of two) */

#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

#define PVR_PACK_COLOR(a, r, g, b) ( \
	((uint32_t)((a) * 255) << 24) | ((uint32_t)((r) * 255) << 16) | \
	((uint32_t)((g) * 255) << 8)  |  (uint32_t)((b) * 255))

static uint16_t src_tex[SRC * SRC];        /* source pattern (RGB565) */
static uint8_t  tab_u[FH * FW];            /* precomputed angle coord  */
static uint8_t  tab_v[FH * FW];            /* precomputed depth coord  */
static uint8_t  shade[FH * FW];            /* precomputed distance fog (0..31) */

static pvr_poly_hdr_t hdr[2];
static pvr_ptr_t      txr[2];
static uint16_t      *txr_buf[2];
static int            txr_cur;

static inline uint16_t rgb565(int r, int g, int b) {
	if(r > 255) r = 255;
	if(g > 255) g = 255;
	if(b > 255) b = 255;
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void hue2rgb(float h, int *r, int *g, int *b) {
	h -= floorf(h);
	float x = h * 6.0f; int i = (int)x; float f = x - i;
	int v = 255, p = 0, q = (int)(255 * (1 - f)), t = (int)(255 * f);
	switch(i) {
		case 0: *r=v; *g=t; *b=p; break;  case 1: *r=q; *g=v; *b=p; break;
		case 2: *r=p; *g=v; *b=t; break;  case 3: *r=p; *g=q; *b=v; break;
		case 4: *r=t; *g=p; *b=v; break;  default:*r=v; *g=p; *b=q; break;
	}
}

static void build_source(void) {
	for(int sy = 0; sy < SRC; sy++) {
		int r, g, b;
		hue2rgb((float)sy / SRC, &r, &g, &b);     /* colour rings along depth */
		for(int sx = 0; sx < SRC; sx++) {
			int rr = r, gg = g, bb = b;
			/* dark grid ribs so motion reads clearly */
			if(((sx & 15) < 2) || ((sy & 15) < 2)) { rr /= 3; gg /= 3; bb /= 3; }
			src_tex[sy * SRC + sx] = rgb565(rr, gg, bb);
		}
	}
}

static void build_tables(void) {
	const float cx = FW / 2.0f, cy = FH / 2.0f;
	for(int y = 0; y < FH; y++) {
		for(int x = 0; x < FW; x++) {
			float dx = x - cx, dy = y - cy;
			float dist = sqrtf(dx * dx + dy * dy);
			if(dist < 1.0f) dist = 1.0f;
			float ang = atan2f(dy, dx);                 /* -pi..pi */
			int i = y * FW + x;
			tab_u[i] = (uint8_t)((int)((ang / (2.0f * (float)M_PI) + 0.5f) * SRC) & (SRC - 1));
			tab_v[i] = (uint8_t)((int)(SRC * 24.0f / dist) & (SRC - 1));
			/* fog: far (centre) darker, near (edge) brighter */
			int s = (int)(dist * 0.9f);
			shade[i] = s > 31 ? 31 : s;
		}
	}
}

/* Shade an RGB565 texel by a 0..31 factor (>>5 fixed point), staying in 565. */
static inline uint16_t shade565(uint16_t c, int s) {
	int r = ((c >> 11) & 0x1f) * s >> 5;
	int g = ((c >> 5)  & 0x3f) * s >> 5;
	int b = ( c        & 0x1f) * s >> 5;
	return (r << 11) | (g << 5) | b;
}

static void tunnel_step(int rot, int fwd) {
	uint16_t *vrout = txr_buf[txr_cur];
	for(int y = 0; y < FH; y++) {
		for(int x = 0; x < FW; x++) {
			int i = y * FW + x;
			int u = (tab_u[i] + rot) & (SRC - 1);
			int v = (tab_v[i] + fwd) & (SRC - 1);
			vrout[TWIDOUT(x, y)] = shade565(src_tex[v * SRC + u], shade[i]);
		}
	}
	pvr_txr_load(txr_buf[txr_cur], txr[txr_cur], FW * FH * 2);
}

static void pvr_setup(void) {
	pvr_poly_cxt_t cxt;
	pvr_set_bg_color(0, 0, 0);
	for(int i = 0; i < 2; i++) {
		txr[i] = pvr_mem_malloc(FW * FH * 2);
		txr_buf[i] = aligned_alloc(32, FW * FH * 2);
		memset(txr_buf[i], 0, FW * FH * 2);
		pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
				 FW, FH, txr[i], PVR_FILTER_BILINEAR);
		pvr_poly_compile(&hdr[i], &cxt);
	}
	txr_cur = 0;
}

static void do_frame(int rot, int fwd) {
	pvr_vertex_t v;
	pvr_wait_ready();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_prim(&hdr[txr_cur], sizeof(*hdr));

	v.flags = PVR_CMD_VERTEX; v.z = 1.0f; v.oargb = 0;
	v.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
	v.x=0;   v.y=480; v.u=0; v.v=1; pvr_prim(&v, sizeof(v));
	v.x=0;   v.y=0;   v.u=0; v.v=0; pvr_prim(&v, sizeof(v));
	v.x=640; v.y=480; v.u=1; v.v=1; pvr_prim(&v, sizeof(v));
	v.flags = PVR_CMD_VERTEX_EOL; v.x=640; v.y=0; v.u=1; v.v=0; pvr_prim(&v, sizeof(v));

	pvr_list_finish();
	pvr_scene_finish();

	tunnel_step(rot, fwd);
	txr_cur ^= 1;
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;
	mkdir("/dev", 0755); mount("dev", "/dev", "devtmpfs", 0, 0);
	if(pvr_init() < 0) { printf("pvr_init failed\n"); return 1; }
	printf("tunnel: %dx%d (kill from serial to stop)\n", pvr_screen_w, pvr_screen_h);

	build_source();
	build_tables();
	pvr_setup();

	int t = 0;
	for(;;) { do_frame(t / 2, t * 2); t++; }
	return 0;
}
