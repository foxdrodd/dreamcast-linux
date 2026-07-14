/* trihold - render the RGB Gouraud triangle continuously so the frame can be
 * captured over VGA.  Same geometry as tridiag; loops until killed. */
#include <libpvr.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>

int main(void)
{
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t v[3];
	int fw, fh, i;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);

	if (pvr_init() < 0) { printf("pvr_init failed\n"); return 1; }
	fw = pvr_screen_w; fh = pvr_screen_h;
	printf("trihold: %dx%d, rendering (Ctrl-C to stop)\n", fw, fh);

	pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
	cxt.gen.culling = PVR_CULLING_NONE;
	pvr_poly_compile(&hdr, &cxt);

	for (i = 0; i < 3; i++) { v[i].flags = PVR_CMD_VERTEX; v[i].z = 1.0f;
		v[i].u = v[i].v = 0.0f; v[i].oargb = 0; }
	v[2].flags = PVR_CMD_VERTEX_EOL;
	v[0].x = fw*0.16f; v[0].y = fh*0.85f; v[0].argb = 0xffff0000;
	v[1].x = fw*0.50f; v[1].y = fh*0.15f; v[1].argb = 0xff00ff00;
	v[2].x = fw*0.84f; v[2].y = fh*0.85f; v[2].argb = 0xff0000ff;

	pvr_set_bg_color(0.1f, 0.1f, 0.4f);

	for (;;) {
		pvr_wait_ready();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_prim(&hdr, sizeof(hdr));
		pvr_prim(&v[0], sizeof(v[0]));
		pvr_prim(&v[1], sizeof(v[1]));
		pvr_prim(&v[2], sizeof(v[2]));
		pvr_list_finish();
		pvr_scene_finish();
	}
	return 0;
}
