/*
 * libpvr runtime - PowerVR2 rendering over the /dev/pvr kernel shim.
 *
 * This performs the *real* Holly TA/ISP initialization that hardware requires:
 * a VRAM buffer layout (param/vertex buffer, object-pointer blocks, region
 * array), the region array (tile matrix) built into VRAM, the magic ISP/TSP
 * config registers, the TA buffer-pointer registers confirmed with TA_INIT,
 * and - per frame - a background plane plus the ISP render-address setup before
 * STARTRENDER.  (KOS does all of this; the QEMU dc-ta model does not need it and
 * harmlessly ignores the extra register writes, so the same code runs on both.)
 *
 * Single-buffered: pvr2fb owns scanout (framebuffer at VRAM offset 0); libpvr
 * renders straight into it and waits for render-done each frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include "libpvr.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

/* ------- /dev/pvr ABI (mirrors drivers/gpu/pvr2/pvr2_dc_abi.h) ----------- */
struct pvr2_dc_info {
	uint32_t vram_phys, vram_size;
	uint32_t regs_phys, regs_size;
	uint32_t ta_fifo_phys, ta_fifo_size;
	uint32_t vram64_phys, vram64_size;
};
#define PVR2_DC_IOC_MAGIC 'P'
#define PVR2_DC_IOC_INFO        _IOR(PVR2_DC_IOC_MAGIC, 0, struct pvr2_dc_info)
#define PVR2_DC_IOC_WAIT_VSYNC  _IO(PVR2_DC_IOC_MAGIC, 1)
#define PVR2_DC_IOC_WAIT_RENDER _IO(PVR2_DC_IOC_MAGIC, 2)

/* ------- PVR register byte offsets (0x005f8000 block) -------------------- */
#define R_RESET             0x008
#define R_ISP_START         0x014   /* STARTRENDER (write 0xffffffff) */
#define R_ISP_VERTBUF_ADDR  0x020   /* PARAM_BASE: param buffer for the render */
#define R_ISP_TILEMAT_ADDR  0x02c   /* REGION_BASE: region array / tile matrix */
#define R_SPANSORT_CFG      0x030
#define R_FB_R_CTRL         0x044   /* DIWMODE: bit0 enable, bits2-3 bpp-1 */
#define R_FB_W_CTRL         0x048
#define R_RENDER_MODULO     0x04c   /* render pitch: (w*bpp)/8 */
#define R_FB_R_SOF1         0x050   /* pvr2fb scanout address (page) */
#define R_FB_R_SIZE         0x05c
#define R_RENDER_ADDR       0x060   /* render output address (VRAM offset) */
#define R_PCLIP_X           0x068
#define R_PCLIP_Y           0x06c
#define R_CHEAP_SHADOW      0x074
#define R_UNK_007C          0x07c
#define R_UNK_0080          0x080
#define R_TEXTURE_CLIP      0x084
#define R_BGPLANE_Z         0x088
#define R_BGPLANE_CFG       0x08c
#define R_UNK_0098          0x098
#define R_UNK_00A0          0x0a0
#define R_UNK_00A8          0x0a8
#define R_FOG_TABLE_COLOR   0x0b0
#define R_FOG_VERTEX_COLOR  0x0b4
#define R_FOG_DENSITY       0x0b8
#define R_COLOR_CLAMP_MAX   0x0bc
#define R_COLOR_CLAMP_MIN   0x0c0
#define R_TEXTURE_MODULO    0x0e4
#define R_UNK_0110          0x110
#define R_UNK_0118          0x118
#define R_TA_OPB_START      0x124
#define R_TA_VERTBUF_START  0x128
#define R_TA_OPB_END        0x12c
#define R_TA_VERTBUF_END    0x130
#define R_TA_VERTBUF_POS    0x138
#define R_TILEMAT_CFG       0x13c
#define R_OPB_CFG           0x140
#define R_TA_INIT           0x144   /* confirm TA settings (write 0x80000000) */
#define R_TA_OPB_INIT       0x164

#define TA_INIT_GO   0x80000000u
#define ISP_START_GO 0xffffffffu

/* ------- TA header field shifts (from KOS dc/pvr/pvr_legacy.h) ----------- */
#define S_CMD_TXRENABLE   3
#define S_CMD_TYPE        24
#define S_CMD_CLRFMT      4
#define S_CMD_SHADE       1
#define S_CMD_UVFMT       0
#define S_CMD_SPECULAR    2
#define S_PM1_DEPTHCMP    29
#define S_PM1_CULLING     27
#define S_PM1_DEPTHWRITE  26
#define S_PM1_TXRENABLE   25
#define S_PM2_SRCBLEND    29
#define S_PM2_DSTBLEND    26
#define S_PM2_SRCENABLE   25
#define S_PM2_DSTENABLE   24
#define S_PM2_FOG         22
#define S_PM2_CLAMP       21
#define S_PM2_ALPHA       20
#define S_PM2_TXRALPHA    19
#define S_PM2_UVFLIP      17
#define S_PM2_UVCLAMP     15
#define S_PM2_FILTER      13
#define S_PM2_MIPBIAS     8
#define S_PM2_TXRENV      6
#define S_PM2_USIZE       3
#define S_PM2_VSIZE       0
#define S_PM3_MIPMAP      31

/*
 * VRAM layout (byte offsets from VRAM base).  pvr2fb's framebuffer is at
 * offset 0 (640x480x2 = 0x96000); everything 3D lives above it.
 */
#define OFF_VERTBUF   0x100000u          /* TA param/vertex buffer          */
#define SZ_VERTBUF    0x100000u          /* 1 MB                            */
#define OFF_OPB       0x200000u          /* object pointer blocks           */
#define OFF_REGION    0x226000u          /* region array (tile matrix) area */
#define TILEMAT_HDR   0x48u              /* zero header before the matrix   */
/*
 * Texture heap.  This is an offset in the *64-bit* VRAM view (where the PVR
 * fetches textures), which interleaves the two banks: a 64-bit offset A maps to
 * physical ~A/2 in bank 0 (plus a mirror in bank 1).  The 32-bit-area buffers
 * above occupy physical 0..~0x22c000, so the texture heap must start high
 * enough that A/2 clears that -- 0x500000 (bank-0 footprint from ~0x280000)
 * leaves them untouched and still yields ~3 MB of texture space.
 */
#define OFF_TEXHEAP   0x500000u          /* texture heap (64-bit-area offset) */

/* Only the opaque list is enabled for now (bring-up).  Extend by enabling
 * more bits here + blanking unused-but-enabled lists in pvr_scene_finish. */
#define LISTS_ENABLED  (1u << PVR_LIST_OP_POLY)

/* Object-pointer-block size per tile per list, in bytes (KOS PVR_BINSIZE_16
 * = 16 words = 64 bytes).  Opaque only for now. */
#define OPB_SIZE_OP   64u
#define OPB_OVERFLOW  3u                 /* extra OPB copies for TA overflow */

int pvr_screen_w = 640;
int pvr_screen_h = 480;

static int pvr_fd = -1;
static volatile uint32_t *regs;
static volatile uint32_t *fifo;
static uint8_t *vram;           /* 32-bit area: framebuffer, TA buffers    */
static uint8_t *vram64;         /* 64-bit area: texture uploads            */
static struct pvr2_dc_info info;

/*
 * TA submission.  Real Holly only accepts 32-byte burst writes to the TA FIFO
 * (store queues / DMA), so we accumulate the frame's parameters and hand the
 * whole buffer to the kernel via write(/dev/pvr), which bursts it through the
 * SH4 store queues.  Set LIBPVR_DIRECT_FIFO=1 to instead write the mmap'd FIFO
 * word-by-word (QEMU's dc-ta model accepts that; real hardware does not).
 */
#define SUBMIT_MAX (512 * 1024)
static uint8_t submit_buf[SUBMIT_MAX];
static uint32_t submit_len;
static int use_submit;          /* 1 = accumulate + write(ta_fd); 0 = direct FIFO */

static uint32_t heap_ptr = OFF_TEXHEAP;
static uint32_t bg_argb = 0xff000000;

/* geometry, computed in pvr_init */
static uint32_t g_tw, g_th, g_tiles;
static uint32_t g_opb_total;             /* bytes of one OPB set (all tiles) */
static uint32_t g_tilemat;               /* region-array matrix start offset */
static uint32_t g_lists_open, g_lists_closed;

static inline void REG(unsigned o, uint32_t v) { regs[o / 4] = v; }
static inline uint32_t RREG(unsigned o) { return regs[o / 4]; }

static inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/*
 * pvr2fb's live scanout offset (0-based VRAM offset).  We render into it
 * (single-buffered), so a frame's render output replaces the console.
 */
static uint32_t fb_addr(void)
{
	return RREG(R_FB_R_SOF1) & (info.vram_size - 1);
}

uint32_t pvr_debug_fb_addr(void) { return fb_addr(); }
uint32_t pvr_debug_reg(unsigned off) { return RREG(off); }

/* Raw PVR register access for higher layers (e.g. GLdc: span-sort, texture
 * modulo, punch-through alpha ref).  off = byte offset in the 0x005f8000 block. */
void pvr_regw(unsigned off, uint32_t val) { REG(off, val); }
uint32_t pvr_regr(unsigned off) { return RREG(off); }
uint32_t pvr_debug_vram_u32(uint32_t off) { uint32_t v; memcpy(&v, vram + (off & (info.vram_size - 1)), 4); return v; }

/* ---------------------- region array (tile matrix) ----------------------- */

/*
 * Build the region array in VRAM.  Each of the tw*th 32x32 tiles gets a
 * 6-word entry: a control word (tile x/y) plus one object-pointer-block
 * address per list (0x80000000 = list disabled for this tile).  The TA fills
 * those OPBs as it bins submitted polygons; the ISP walks the region array at
 * render time.  Mirrors KOS pvr_init_tile_matrix (single buffer).
 */
static void build_region_array(void)
{
	uint32_t *hdr = (uint32_t *)(vram + OFF_REGION);
	uint32_t *vr  = (uint32_t *)(vram + g_tilemat);
	uint32_t opb_op = OFF_OPB;               /* opaque OPB base */
	uint32_t x, y, tn, i;

	/* Zero header preceding the matrix. */
	for (i = 0; i < TILEMAT_HDR / 4; i++)
		hdr[i] = 0;

	/* Initial "init" tile. */
	vr[0] = 0x10000000;
	vr[1] = vr[2] = vr[3] = vr[4] = vr[5] = 0x80000000;
	vr += 6;

	for (x = 0; x < g_tw; x++) {
		for (y = 0; y < g_th; y++) {
			tn = g_tw * y + x;
			vr[0] = (y << 8) | (x << 2);          /* presort=0 */
			vr[1] = (LISTS_ENABLED & (1u << PVR_LIST_OP_POLY))
				? opb_op + OPB_SIZE_OP * tn : 0x80000000;
			vr[2] = 0x80000000;                   /* op modifier   */
			vr[3] = 0x80000000;                   /* translucent   */
			vr[4] = 0x80000000;                   /* trans mod     */
			vr[5] = 0x80000000;                   /* punch-through */
			vr += 6;
		}
	}
	vr[-6] |= (1u << 31);                          /* end-of-matrix marker */
}

/* Program the TA buffer pointers and confirm them (a per-frame reset that
 * rewinds the TA's vertex/OPB write pointers to the start). */
static void ta_reg_setup(void)
{
	REG(R_TA_OPB_START,     OFF_OPB);
	REG(R_TA_OPB_INIT,      OFF_OPB + g_opb_total);
	REG(R_TA_OPB_END,       OFF_OPB + g_opb_total * (1 + OPB_OVERFLOW));
	REG(R_TA_VERTBUF_START, OFF_VERTBUF);
	REG(R_TA_VERTBUF_END,   OFF_VERTBUF + SZ_VERTBUF);
	REG(R_TILEMAT_CFG,      ((g_th - 1) << 16) | (g_tw - 1));
	REG(R_OPB_CFG,          0x2 /* opaque, binsize16 -> sconst 2 at shift 0 */);
	REG(R_TA_INIT,          TA_INIT_GO);
	(void)RREG(R_TA_INIT);
}

/* -------------------------------- init ----------------------------------- */

int pvr_init(void)
{
	uint32_t diwsize, bpp;

	pvr_fd = open("/dev/pvr", O_RDWR);
	if (pvr_fd < 0)
		return -1;
	if (ioctl(pvr_fd, PVR2_DC_IOC_INFO, &info) < 0)
		return -2;

	/* Store-queue submit via write(/dev/pvr) by default; opt into direct
	 * FIFO writes (QEMU) with LIBPVR_DIRECT_FIFO=1. */
	use_submit = (getenv("LIBPVR_DIRECT_FIFO") == 0);

	regs = mmap(0, info.regs_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    pvr_fd, info.regs_phys);
	fifo = mmap(0, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED,
		    pvr_fd, info.ta_fifo_phys);
	vram = mmap(0, info.vram_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    pvr_fd, info.vram_phys);
	/*
	 * Map the 64-bit texture area at a 16 MB-aligned virtual address so that
	 * (ptr & 0x00ffffff) == the VRAM offset.  Higher layers (GLdc) hand the
	 * texture unit an address computed as (ptr & 0x00fffff8) >> 3 and expect
	 * that to equal offset>>3 -- KOS gets this for free because its VRAM lives
	 * at 0xa5000000; we reproduce it by aligning our mapping.  Reserve a span,
	 * pick an aligned start, then MAP_FIXED the device there.
	 */
	{
		size_t span = info.vram64_size + 0x1000000;
		void *probe = mmap(0, span, PROT_NONE,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (probe == MAP_FAILED)
			return -3;
		uintptr_t aligned = ((uintptr_t)probe + 0xffffff) & ~(uintptr_t)0xffffff;
		munmap(probe, span);
		vram64 = mmap((void *)aligned, info.vram64_size,
			      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
			      pvr_fd, info.vram64_phys);
	}
	if (regs == MAP_FAILED || fifo == MAP_FAILED || vram == MAP_FAILED ||
	    vram64 == MAP_FAILED)
		return -3;

	/* Adopt the resolution pvr2fb already programmed (FB_R_SIZE). */
	diwsize = RREG(R_FB_R_SIZE);
	bpp = ((RREG(R_FB_R_CTRL) >> 2) & 3) + 1;
	pvr_screen_w = ((diwsize & 0x3ff) + 1) * 4 / bpp;
	pvr_screen_h = ((diwsize >> 10) & 0x3ff) + 1;
	if (pvr_screen_w == 0 || pvr_screen_w > 2048) pvr_screen_w = 640;
	if (pvr_screen_h == 0 || pvr_screen_h > 2048) pvr_screen_h = 480;

	/* Tile geometry (32x32 tiles; height rounded up to a multiple of 32). */
	g_tw = pvr_screen_w / 32;
	g_th = (pvr_screen_h + 31) / 32;
	g_tiles = g_tw * g_th;
	g_opb_total = OPB_SIZE_OP * g_tiles;     /* opaque list only for now */
	g_tilemat = OFF_REGION + TILEMAT_HDR;

	heap_ptr = OFF_TEXHEAP;

	/* Reset the PVR pipeline. */
	REG(R_RESET, 0xffffffff);
	REG(R_RESET, 0x00000000);

	/* Magic ISP/TSP config values (from KOS pvr_init). */
	REG(R_UNK_00A8,        0x15d1c951);
	REG(R_UNK_00A0,        0x00000020);
	REG(R_UNK_0110,        0x00093f39);
	REG(R_UNK_0098,        0x00800408);
	REG(R_TEXTURE_CLIP,    0x00000000);
	REG(R_SPANSORT_CFG,    0x00000101);
	REG(R_FOG_TABLE_COLOR, 0x007f7f7f);
	REG(R_FOG_VERTEX_COLOR,0x007f7f7f);
	REG(R_COLOR_CLAMP_MIN, 0x00000000);
	REG(R_COLOR_CLAMP_MAX, 0xffffffff);
	REG(R_UNK_0080,        0x00000007);
	REG(R_CHEAP_SHADOW,    0x00000001);
	REG(R_UNK_007C,        0x0027df77);
	REG(R_TEXTURE_MODULO,  0x00000000);
	REG(R_FOG_DENSITY,     0x0000ff07);
	REG(R_UNK_0118,        0x00008040);

	/* Render target format/pitch/clip = the 16bpp mode pvr2fb set up. */
	REG(R_FB_W_CTRL,     1);                       /* RGB565 */
	REG(R_RENDER_MODULO, pvr_screen_w * 2 / 8);
	REG(R_PCLIP_X,       (pvr_screen_w - 1) << 16);
	REG(R_PCLIP_Y,       (pvr_screen_h - 1) << 16);

	/* Build the region array and program+confirm the TA buffers. */
	build_region_array();
	ta_reg_setup();
	return 0;
}

void pvr_shutdown(void)
{
	if (pvr_fd >= 0) {
		close(pvr_fd);
		pvr_fd = -1;
	}
}

/* ------------------------------ scene flow ------------------------------- */

void pvr_scene_begin(void)
{
	g_lists_open = g_lists_closed = 0;
	submit_len = 0;
	/* Rewind the TA to the start of the buffers for a fresh scene. */
	ta_reg_setup();
}

void pvr_list_begin(pvr_list_t list)
{
	g_lists_open = (1u << list);   /* the list type also travels in each header */
}

void pvr_prim(const void *data, int size)
{
	if (use_submit) {
		if (submit_len + size <= SUBMIT_MAX) {
			memcpy(submit_buf + submit_len, data, size);
			submit_len += size;
		}
	} else {
		const uint32_t *w = data;
		int i;

		for (i = 0; i < size / 4; i++)
			fifo[i & 7] = w[i];
	}
}

/* Burst the accumulated parameter buffer to the TA (store-queue path). */
static void ta_submit_flush(void)
{
	if (use_submit && submit_len) {
		ssize_t r = write(pvr_fd, submit_buf, submit_len);
		(void)r;
		submit_len = 0;
	}
}

void pvr_list_finish(void)
{
	uint32_t eol[8] = { 0 };        /* PCW para_type 0 = end of list */

	pvr_prim(eol, sizeof(eol));
	g_lists_closed |= g_lists_open;
	g_lists_open = 0;
}

/* A minimal blank polygon header for a list (KOS pvr_blank_polyhdr_buf). */
static void submit_blank_list(int list)
{
	uint32_t hdr[8] = { 0 };
	uint32_t eol[8] = { 0 };

	hdr[0] = 0x80840012u | ((uint32_t)list << S_CMD_TYPE);
	pvr_prim(hdr, sizeof(hdr));
	pvr_prim(eol, sizeof(eol));
}

/*
 * Background plane.  The ISP needs a full-screen "background" primitive so
 * empty tiles have something to draw; it goes right after the TA's written
 * data in the param buffer, and BGPLANE_CFG points the render at it.  Layout
 * mirrors KOS pvr_bkg_poly_t / pvr_begin_queued_render.
 */
struct bkg_poly {
	uint32_t flags1, flags2, dummy;
	float x1, y1, z1; uint32_t argb1;
	float x2, y2, z2; uint32_t argb2;
	float x3, y3, z3; uint32_t argb3;
};

static void setup_background_and_render(void)
{
	uint32_t pos = RREG(R_TA_VERTBUF_POS);
	struct bkg_poly *bkg;
	float zeps = 1.19209290e-07f;   /* FLT_EPSILON */

	/* Guard: if the TA didn't advance the write pointer (e.g. under a model
	 * that ignores it), fall back to just past our submitted data start. */
	if (pos < OFF_VERTBUF || pos >= OFF_VERTBUF + SZ_VERTBUF - sizeof(*bkg))
		pos = OFF_VERTBUF + 0x8000;

	bkg = (struct bkg_poly *)(vram + pos);
	bkg->flags1 = 0x90800000;
	bkg->flags2 = 0x20800440;
	bkg->dummy  = 0;
	bkg->x1 = 0.0f;               bkg->y1 = (float)pvr_screen_h; bkg->z1 = zeps; bkg->argb1 = bg_argb;
	bkg->x2 = 0.0f;               bkg->y2 = 0.0f;                bkg->z2 = zeps; bkg->argb2 = bg_argb;
	bkg->x3 = (float)pvr_screen_w;bkg->y3 = (float)pvr_screen_h; bkg->z3 = zeps; bkg->argb3 = bg_argb;

	REG(R_ISP_TILEMAT_ADDR, g_tilemat);
	REG(R_ISP_VERTBUF_ADDR, OFF_VERTBUF);
	REG(R_RENDER_ADDR,      fb_addr());        /* into pvr2fb's live buffer */
	REG(R_BGPLANE_CFG,      0x01000000 | ((pos - OFF_VERTBUF) << 1));
	REG(R_BGPLANE_Z,        f2u(0.0001f));
	REG(R_PCLIP_X,          (pvr_screen_w - 1) << 16);
	REG(R_PCLIP_Y,          (pvr_screen_h - 1) << 16);
	REG(R_RENDER_MODULO,    pvr_screen_w * 2 / 8);
	REG(R_ISP_START,        ISP_START_GO);
}

void pvr_scene_finish(void)
{
	int i;

	/* Blank any enabled list that wasn't drawn this frame (so its OPBs get
	 * proper terminators rather than stale data). */
	for (i = 0; i < 5; i++) {
		if ((LISTS_ENABLED & (1u << i)) && !(g_lists_closed & (1u << i)))
			submit_blank_list(i);
	}

	ta_submit_flush();              /* burst all params to the TA */
	setup_background_and_render();  /* reads TA_VERTBUF_POS, then STARTRENDER */
	ioctl(pvr_fd, PVR2_DC_IOC_WAIT_RENDER, 0);
}

void pvr_wait_ready(void)
{
	ioctl(pvr_fd, PVR2_DC_IOC_WAIT_VSYNC, 0);
}

void pvr_set_bg_color(float r, float g, float b)
{
	int ri = (int)(r * 255), gi = (int)(g * 255), bi = (int)(b * 255);

	bg_argb = 0xff000000 | (ri << 16) | (gi << 8) | bi;
}

/* ------------------------------ VRAM heap -------------------------------- */

pvr_ptr_t pvr_mem_malloc(size_t size)
{
	uint32_t off = heap_ptr;

	heap_ptr += (size + 31) & ~31u;
	return off;
}

size_t pvr_mem_available(void)
{
	return (info.vram64_size > heap_ptr) ? (info.vram64_size - heap_ptr) : 0;
}

/* CPU-writable pointer into the 64-bit texture area for a heap offset.  The
 * mapping is 16 MB-aligned, so (ptr & 0x00ffffff) == off (what GLdc's texture
 * control-word math relies on). */
void *pvr_vram_ptr(pvr_ptr_t off)
{
	return (void *)(vram64 + off);
}

void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t size)
{
	/* Textures are read by the PVR through the 64-bit VRAM view; upload
	 * there (dst is a 64-bit-area byte offset, as the TCW expects). */
	memcpy(vram64 + dst, src, size);
}

/* --------------------------- header compile ------------------------------ */

static inline uint32_t to_txr_ptr(pvr_ptr_t base)
{
	return (base & (info.vram_size - 1)) >> 3;
}

void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src)
{
	uint32_t cmd, mode2, mode3;

	cmd = PVR_CMD_POLYHDR
	    | ((uint32_t)(src->txr.enable ? 1 : 0) << S_CMD_TXRENABLE)
	    | ((uint32_t)src->list_type    << S_CMD_TYPE)
	    | ((uint32_t)src->fmt.color    << S_CMD_CLRFMT)
	    | ((uint32_t)(src->gen.shading ? 1 : 0) << S_CMD_SHADE)
	    | ((uint32_t)(src->fmt.uv ? 1 : 0)      << S_CMD_UVFMT)
	    | ((uint32_t)(src->gen.specular ? 1 : 0) << S_CMD_SPECULAR);
	dst->cmd = cmd;

	dst->mode1 = ((uint32_t)src->depth.comparison << S_PM1_DEPTHCMP)
	    | ((uint32_t)src->gen.culling  << S_PM1_CULLING)
	    | ((uint32_t)(src->depth.write ? 1 : 0) << S_PM1_DEPTHWRITE)
	    | ((uint32_t)(src->txr.enable ? 1 : 0)  << S_PM1_TXRENABLE);

	mode2 = ((uint32_t)src->blend.src << S_PM2_SRCBLEND)
	    | ((uint32_t)src->blend.dst << S_PM2_DSTBLEND)
	    | ((uint32_t)(src->blend.src_enable ? 1 : 0) << S_PM2_SRCENABLE)
	    | ((uint32_t)(src->blend.dst_enable ? 1 : 0) << S_PM2_DSTENABLE)
	    | ((uint32_t)src->gen.fog_type << S_PM2_FOG)
	    | ((uint32_t)(src->gen.color_clamp ? 1 : 0) << S_PM2_CLAMP)
	    | ((uint32_t)(src->gen.alpha ? 1 : 0) << S_PM2_ALPHA);

	if (!src->txr.enable) {
		mode3 = 0;
	} else {
		mode2 |= ((uint32_t)(src->txr.alpha ? 1 : 0) << S_PM2_TXRALPHA)
		    | ((uint32_t)src->txr.uv_flip  << S_PM2_UVFLIP)
		    | ((uint32_t)src->txr.uv_clamp << S_PM2_UVCLAMP)
		    | ((uint32_t)src->txr.filter   << S_PM2_FILTER)
		    | ((uint32_t)src->txr.mipmap_bias << S_PM2_MIPBIAS)
		    | ((uint32_t)src->txr.env      << S_PM2_TXRENV)
		    | ((uint32_t)(__builtin_ctz(src->txr.width) - 3)  << S_PM2_USIZE)
		    | ((uint32_t)(__builtin_ctz(src->txr.height) - 3) << S_PM2_VSIZE);
		mode3 = ((uint32_t)(src->txr.mipmap ? 1 : 0) << S_PM3_MIPMAP)
		    | (uint32_t)src->txr.format
		    | to_txr_ptr(src->txr.base);
	}
	dst->mode2 = mode2;
	dst->mode3 = mode3;
	dst->d1 = dst->d2 = dst->d3 = dst->d4 = 0;
}

void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list)
{
	bool alpha = list > PVR_LIST_OP_MOD;

	memset(dst, 0, sizeof(*dst));
	dst->list_type = list;
	dst->fmt.color = PVR_CLRFMT_ARGBPACKED;
	dst->gen.shading = true;
	dst->depth.comparison = PVR_DEPTHCMP_GREATER;
	dst->depth.write = false;               /* PVR_DEPTHWRITE_ENABLE */
	dst->gen.culling = PVR_CULLING_CCW;
	dst->gen.alpha = alpha;
	if (!alpha) {
		dst->blend.src = PVR_BLEND_ONE;
		dst->blend.dst = PVR_BLEND_ZERO;
	} else {
		dst->blend.src = PVR_BLEND_SRCALPHA;
		dst->blend.dst = PVR_BLEND_INVSRCALPHA;
	}
	dst->gen.fog_type = PVR_FOG_DISABLE;
}

void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int fmt,
		      int tw, int th, pvr_ptr_t addr, int filter)
{
	bool alpha = list > PVR_LIST_OP_MOD;

	pvr_poly_cxt_col(dst, list);
	dst->txr.enable = true;
	dst->txr.filter = filter;
	dst->txr.env = alpha ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_MODULATE;
	dst->txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
	dst->txr.width = tw;
	dst->txr.height = th;
	dst->txr.base = addr;
	dst->txr.format = fmt;
}
