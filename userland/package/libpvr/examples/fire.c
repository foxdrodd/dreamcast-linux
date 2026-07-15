/* fire - the classic demoscene per-pixel fire effect, on libpvr.
 *
 * A small CPU "heat" buffer is seeded hot along the bottom, propagated upward
 * with cooling (pure integer work), colour-mapped through a fire palette into a
 * twiddled RGB565 texture, uploaded to VRAM each frame, and stretched over the
 * whole screen by the PVR with bilinear filtering -- same dynamic-texture path
 * as the plasma demo.  Drawn with a white vertex colour so the fire colours
 * come straight from the texture.
 *
 * Built directly on libpvr (no GLdc): libpvr's texture pipeline is the one
 * proven correct by txrdiag, so the fire palette reproduces faithfully.
 */
#include <libpvr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define FW 128
#define FH 128

/* Linear/iterative twiddling (from Marcus' tatest, as used by plasma). */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

#define PVR_PACK_COLOR(a, r, g, b) ( \
	((uint32_t)((a) * 255) << 24) | ((uint32_t)((r) * 255) << 16) | \
	((uint32_t)((g) * 255) << 8)  |  (uint32_t)((b) * 255))

static uint8_t  heat[FH][FW];
static uint16_t pal[256];          /* fire palette, RGB565 */

static pvr_poly_hdr_t hdr[2];
static pvr_ptr_t      txr[2];
static uint16_t      *txr_buf[2];
static int            txr_cur;

/* Cheap xorshift PRNG (Date/rand not needed). */
static uint32_t rng = 0x1234567u;
static inline uint32_t xr(void) {
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

static inline uint16_t rgb565(int r, int g, int b) {
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void build_palette(void) {
	for(int i = 0; i < 256; i++) {
		int r, g, b;
		if(i < 64)       { r = i * 4;   g = 0;            b = 0; }
		else if(i < 128) { r = 255;     g = (i - 64) * 4; b = 0; }
		else if(i < 192) { r = 255;     g = 255;          b = (i - 128) * 4; }
		else             { r = 255;     g = 255;          b = 255; }
		pal[i] = rgb565(r, g, b);
	}
}

static void fire_step(void) {
	uint16_t *vrout = txr_buf[txr_cur];

	/* Seed the bottom row: mostly white-hot with random cold embers. */
	for(int x = 0; x < FW; x++)
		heat[FH - 1][x] = (xr() & 7) ? 255 : 0;

	/* Propagate upward with cooling. */
	for(int y = 0; y < FH - 1; y++) {
		for(int x = 0; x < FW; x++) {
			int xl  = (x == 0)      ? FW - 1 : x - 1;
			int xr_ = (x == FW - 1) ? 0      : x + 1;
			int y2  = (y + 2 < FH) ? y + 2 : FH - 1;
			int sum = heat[y + 1][xl] + heat[y + 1][x] + heat[y + 1][xr_] + heat[y2][x];
			int v = (sum >> 2) - (xr() & 7);
			heat[y][x] = (v < 0) ? 0 : v;
		}
	}

	/* Colour-map + twiddle into the texture.  Buffer row y maps straight to
	 * texture row y; the quad's v runs 1->0 top-to-bottom, so the hot bottom
	 * row (FH-1) lands at the bottom of the screen. */
	for(int y = 0; y < FH; y++)
		for(int x = 0; x < FW; x++)
			vrout[TWIDOUT(x, y)] = pal[heat[y][x]];

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

static void do_frame(void) {
	pvr_vertex_t vert;

	pvr_wait_ready();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_prim(&hdr[txr_cur], sizeof(*hdr));

	vert.flags = PVR_CMD_VERTEX;
	vert.z = 1.0f;
	vert.oargb = 0;
	vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);

	vert.x = 0.0f;   vert.y = 480.0f; vert.u = 0.0f; vert.v = 1.0f; pvr_prim(&vert, sizeof(vert));
	vert.x = 0.0f;   vert.y = 0.0f;   vert.u = 0.0f; vert.v = 0.0f; pvr_prim(&vert, sizeof(vert));
	vert.x = 640.0f; vert.y = 480.0f; vert.u = 1.0f; vert.v = 1.0f; pvr_prim(&vert, sizeof(vert));

	vert.flags = PVR_CMD_VERTEX_EOL;
	vert.x = 640.0f; vert.y = 0.0f;   vert.u = 1.0f; vert.v = 0.0f; pvr_prim(&vert, sizeof(vert));

	pvr_list_finish();
	pvr_scene_finish();

	fire_step();
	txr_cur ^= 1;
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);

	if(pvr_init() < 0) { printf("pvr_init failed\n"); return 1; }
	printf("fire: %dx%d, rendering (kill from serial to stop)\n",
	       pvr_screen_w, pvr_screen_h);

	build_palette();
	pvr_setup();

	for(;;)
		do_frame();

	return 0;
}
