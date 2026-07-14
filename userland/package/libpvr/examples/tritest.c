/*
 * tritest - a Gouraud triangle rendered through libpvr (KOS-style API).
 *
 * Demonstrates the full libpvr path: pvr_init -> per-frame scene/list/prim ->
 * pvr_scene_finish (STARTRENDER + render-wait).  Built static and run as PID 1
 * in an initramfs, a QEMU screendump shows the triangle.
 *
 * The vertices are built once, outside the frame loop: their coordinates are
 * static, and keeping the float math out of the loop also keeps the SH-4 FP
 * code straight-line (avoiding a QEMU SH4 double-precision FPU-emulation quirk
 * that trips loop-carried FP register allocation).
 *
 * SPDX-License-Identifier: MIT
 */
#include <libpvr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t v[3];
	int fw, fh, i;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);

	if (pvr_init() < 0)
		for (;;) pause();

	fw = pvr_screen_w;
	fh = pvr_screen_h;

	/* Opaque, Gouraud-shaded, non-culled polygon header. */
	pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
	cxt.gen.culling = PVR_CULLING_NONE;
	pvr_poly_compile(&hdr, &cxt);

	/* Three vertices, built once (screen-space; z = 1/w). */
	for (i = 0; i < 3; i++) {
		v[i].flags = PVR_CMD_VERTEX;
		v[i].z = 1.0f;
		v[i].u = v[i].v = 0.0f;
		v[i].oargb = 0;
	}
	v[2].flags = PVR_CMD_VERTEX_EOL;
	v[0].x = fw * 0.16f; v[0].y = fh * 0.85f; v[0].argb = 0xffff0000;
	v[1].x = fw * 0.50f; v[1].y = fh * 0.15f; v[1].argb = 0xff00ff00;
	v[2].x = fw * 0.84f; v[2].y = fh * 0.85f; v[2].argb = 0xff0000ff;

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
