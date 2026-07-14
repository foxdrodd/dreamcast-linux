/* tridiag - instrumented triangle: render frames with a render-wait timeout,
 * then dump PVR state + sample the framebuffer.  Prints to stdout (serial). */
#include <libpvr.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mount.h>
#include <sys/stat.h>

static sigjmp_buf jb;
static void onalrm(int s){ (void)s; siglongjmp(jb, 1); }

static void dumpreg(const char *nm, unsigned off)
{
	printf("  %-18s [%03x] = %08x\n", nm, off, pvr_debug_reg(off));
}

int main(void)
{
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t v[3];
	int fw, fh, i, frame = 0, ok = 0, nonzero;
	uint32_t fb;

	mkdir("/dev", 0755);
	mount("dev", "/dev", "devtmpfs", 0, 0);

	i = pvr_init();
	printf("pvr_init=%d  screen=%dx%d\n", i, pvr_screen_w, pvr_screen_h);
	if (i < 0) return 1;
	fw = pvr_screen_w; fh = pvr_screen_h;

	printf("--- after init ---\n");
	dumpreg("ISP_VERTBUF", 0x020); dumpreg("ISP_TILEMAT", 0x02c);
	dumpreg("TA_OPB_START", 0x124); dumpreg("TA_VERTBUF_START", 0x128);
	dumpreg("TA_OPB_END", 0x12c); dumpreg("TA_VERTBUF_END", 0x130);
	dumpreg("TILEMAT_CFG", 0x13c); dumpreg("OPB_CFG", 0x140);
	dumpreg("FB_R_SOF1", 0x050);

	pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
	cxt.gen.culling = PVR_CULLING_NONE;
	pvr_poly_compile(&hdr, &cxt);
	printf("hdr: %08x %08x %08x %08x\n", hdr.cmd, hdr.mode1, hdr.mode2, hdr.mode3);

	for (i = 0; i < 3; i++) { v[i].flags = PVR_CMD_VERTEX; v[i].z = 1.0f;
		v[i].u = v[i].v = 0.0f; v[i].oargb = 0; }
	v[2].flags = PVR_CMD_VERTEX_EOL;
	v[0].x = fw*0.16f; v[0].y = fh*0.85f; v[0].argb = 0xffff0000;
	v[1].x = fw*0.50f; v[1].y = fh*0.15f; v[1].argb = 0xff00ff00;
	v[2].x = fw*0.84f; v[2].y = fh*0.85f; v[2].argb = 0xff0000ff;

	pvr_set_bg_color(0.1f, 0.1f, 0.4f);   /* distinct dark blue bg */

	signal(SIGALRM, onalrm);
	for (frame = 0; frame < 20; frame++) {
		pvr_wait_ready();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_prim(&hdr, sizeof(hdr));
		pvr_prim(&v[0], sizeof(v[0]));
		pvr_prim(&v[1], sizeof(v[1]));
		pvr_prim(&v[2], sizeof(v[2]));
		pvr_list_finish();
		if (sigsetjmp(jb, 1) == 0) {
			alarm(2);
			pvr_scene_finish();       /* STARTRENDER + WAIT_RENDER */
			alarm(0);
			ok++;
		} else {
			printf("frame %d: WAIT_RENDER TIMEOUT\n", frame);
			break;
		}
	}
	printf("rendered %d/%d frames ok\n", ok, frame + (ok == frame ? 0 : 1));

	printf("--- after render ---\n");
	dumpreg("TA_VERTBUF_POS", 0x138);
	dumpreg("BGPLANE_CFG", 0x08c);
	dumpreg("RENDER_ADDR", 0x060);

	/* Sample the framebuffer (offset 0) for non-zero / non-console pixels. */
	fb = pvr_debug_fb_addr();
	nonzero = 0;
	for (i = 0; i < fw * fh; i += 37) {   /* sparse scan */
		if (pvr_debug_vram_u32(fb + (i & ~1) * 2) != 0)
			nonzero++;
	}
	printf("fb@%08x: %d nonzero samples (of %d)\n", fb, nonzero, (fw*fh)/37);
	/* Show a few center pixels where the triangle should be. */
	for (i = 0; i < 4; i++) {
		uint32_t off = fb + ((fh/2) * fw + (fw/2 + i*2)) * 2;
		printf("  center px[%d] = %04x\n", i, pvr_debug_vram_u32(off) & 0xffff);
	}
	printf("TRIDIAG DONE\n");
	return 0;
}
