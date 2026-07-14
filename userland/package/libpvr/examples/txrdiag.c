/* txrdiag - texture layout diagnostic.  Uploads a 64x64 twiddled RGB565
 * texture of four solid-colour quadrants (TL red, TR green, BL blue, BR white)
 * plus a 1-texel white diagonal, stretched over the screen with NEAREST
 * filtering.  How this scrambles on screen tells us whether libpvr's texture
 * memory view (32-bit area) mismatches the PVR's texture read (64-bit area),
 * versus a twiddle-order problem.  Correct output = 4 clean quadrants. */
#include <libpvr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )

int main(int argc, char **argv)
{
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_ptr_t txr;
	uint16_t *buf;
	pvr_vertex_t vert;
	int x, y;
	(void)argc; (void)argv;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);
	if (pvr_init() < 0) { printf("pvr_init failed\n"); return 1; }
	printf("txrdiag: %dx%d\n", pvr_screen_w, pvr_screen_h);

	txr = pvr_mem_malloc(64 * 64 * 2);
	buf = aligned_alloc(32, 64 * 64 * 2);

	for (y = 0; y < 64; y++) {
		for (x = 0; x < 64; x++) {
			uint16_t c;
			if (x == y)          c = 0xffff;        /* white diagonal */
			else if (x < 32 && y < 32) c = 0xf800;  /* TL red   */
			else if (x >= 32 && y < 32) c = 0x07e0; /* TR green */
			else if (x < 32 && y >= 32) c = 0x001f; /* BL blue  */
			else                 c = 0xffff;        /* BR white */
			buf[TWIDOUT(x, y)] = c;
		}
	}
	pvr_txr_load(buf, txr, 64 * 64 * 2);

	pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
			 64, 64, txr, PVR_FILTER_NEAREST);
	pvr_poly_compile(&hdr, &cxt);

	pvr_set_bg_color(0.2f, 0.2f, 0.2f);

	for (;;) {
		pvr_wait_ready();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_prim(&hdr, sizeof(hdr));

		vert.flags = PVR_CMD_VERTEX; vert.z = 1.0f;
		vert.argb = 0xffffffff; vert.oargb = 0;
		vert.x = 0;   vert.y = 480; vert.u = 0; vert.v = 1; pvr_prim(&vert, sizeof(vert));
		vert.x = 0;   vert.y = 0;   vert.u = 0; vert.v = 0; pvr_prim(&vert, sizeof(vert));
		vert.x = 640; vert.y = 480; vert.u = 1; vert.v = 1; pvr_prim(&vert, sizeof(vert));
		vert.flags = PVR_CMD_VERTEX_EOL;
		vert.x = 640; vert.y = 0;   vert.u = 1; vert.v = 0; pvr_prim(&vert, sizeof(vert));

		pvr_list_finish();
		pvr_scene_finish();
	}
	return 0;
}
