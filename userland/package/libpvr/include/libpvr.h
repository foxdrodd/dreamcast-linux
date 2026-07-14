/*
 * libpvr - PowerVR2 3D rendering for Dreamcast Linux userland.
 *
 * A small, KallistiOS-compatible PVR API implemented over the /dev/pvr kernel
 * shim (drivers/gpu/pvr2).  The data structures, constants and header-compile
 * logic mirror KOS's dc/pvr.h so that KOS-derived code (e.g. GLdc) drops on top
 * with minimal changes; the runtime (init, scene/list/prim, VRAM alloc, vsync)
 * talks to the hardware through the mmap'd registers, TA FIFO and VRAM that
 * /dev/pvr exposes, instead of KOS's bare-metal store queues and threads.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIBPVR_H
#define LIBPVR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A location in PowerVR VRAM, expressed as a byte offset from the base of the
 * 8 MB video RAM (0 .. 8MB).  pvr_mem_malloc() returns these. */
typedef uint32_t pvr_ptr_t;

/* ---- primitive list types (values match the PVR list ordering) ---------- */
typedef int pvr_list_t;
#define PVR_LIST_OP_POLY   0   /* opaque polygons        */
#define PVR_LIST_OP_MOD    1   /* opaque modifier volume */
#define PVR_LIST_TR_POLY   2   /* translucent polygons   */
#define PVR_LIST_TR_MOD    3   /* translucent modifier   */
#define PVR_LIST_PT_POLY   4   /* punch-through polygons */

/* ---- TA command words (first word of each 32-byte parameter) ------------ */
#define PVR_CMD_POLYHDR    0x80840000u
#define PVR_CMD_VERTEX     0xe0000000u
#define PVR_CMD_VERTEX_EOL 0xf0000000u
#define PVR_CMD_MODIFIER   0x80000000u
#define PVR_CMD_SPRITE     0xa0000000u

/* ---- context enum values (match KOS dc/pvr.h) --------------------------- */
#define PVR_CLRFMT_ARGBPACKED 0

#define PVR_CULLING_NONE   0
#define PVR_CULLING_SMALL  1
#define PVR_CULLING_CCW    2
#define PVR_CULLING_CW     3

#define PVR_DEPTHCMP_NEVER    0
#define PVR_DEPTHCMP_LESS     1
#define PVR_DEPTHCMP_EQUAL    2
#define PVR_DEPTHCMP_LEQUAL   3
#define PVR_DEPTHCMP_GREATER  4
#define PVR_DEPTHCMP_NOTEQUAL 5
#define PVR_DEPTHCMP_GEQUAL   6
#define PVR_DEPTHCMP_ALWAYS   7
#define PVR_DEPTHWRITE_ENABLE  0
#define PVR_DEPTHWRITE_DISABLE 1

#define PVR_BLEND_ZERO        0
#define PVR_BLEND_ONE         1
#define PVR_BLEND_DESTCOLOR   2
#define PVR_BLEND_INVDESTCOLOR 3
#define PVR_BLEND_SRCALPHA    4
#define PVR_BLEND_INVSRCALPHA 5
#define PVR_BLEND_DESTALPHA   6
#define PVR_BLEND_INVDESTALPHA 7
#define PVR_BLEND_DISABLE  0
#define PVR_BLEND_ENABLE   1

#define PVR_FOG_TABLE   0
#define PVR_FOG_VERTEX  1
#define PVR_FOG_DISABLE 2

#define PVR_FILTER_NEAREST  0
#define PVR_FILTER_BILINEAR 1
#define PVR_FILTER_NONE     PVR_FILTER_NEAREST

#define PVR_TXRENV_REPLACE       0
#define PVR_TXRENV_MODULATE      1
#define PVR_TXRENV_DECAL         2
#define PVR_TXRENV_MODULATEALPHA 3

#define PVR_UVFLIP_NONE   0
#define PVR_UVCLAMP_NONE  0
#define PVR_MIPBIAS_NORMAL 4

/* ---- texture format bits (OR into cxt.txr.format / TCW) ----------------- */
#define PVR_TXRFMT_ARGB1555    (0u << 27)
#define PVR_TXRFMT_RGB565      (1u << 27)
#define PVR_TXRFMT_ARGB4444    (2u << 27)
#define PVR_TXRFMT_TWIDDLED    (0u << 26)
#define PVR_TXRFMT_NONTWIDDLED (1u << 26)

/* ---- data structures (32-byte TA parameters) ---------------------------- */

/* Compiled polygon header. */
typedef struct {
	uint32_t cmd;
	uint32_t mode1;
	uint32_t mode2;
	uint32_t mode3;
	uint32_t d1, d2, d3, d4;
} pvr_poly_hdr_t;

/* Generic vertex (textured or Gouraud). */
typedef struct {
	uint32_t flags;             /* PVR_CMD_VERTEX or PVR_CMD_VERTEX_EOL */
	float    x, y, z;           /* screen-space; z = 1/w */
	float    u, v;              /* texture coordinates */
	uint32_t argb;              /* base colour */
	uint32_t oargb;             /* offset (specular) colour */
} pvr_vertex_t;

/* Human-readable polygon context (compiled into a header). */
typedef struct {
	pvr_list_t list_type;
	struct {
		bool shading;
		int  culling;
		int  fog_type;
		bool color_clamp;
		bool specular;
		bool alpha;
	} gen;
	struct {
		int  src, dst;
		bool src_enable, dst_enable;
	} blend;
	struct {
		int  color;             /* PVR_CLRFMT_* */
		bool uv;                /* 16-bit UV if true */
	} fmt;
	struct {
		int  comparison;        /* PVR_DEPTHCMP_* */
		bool write;             /* PVR_DEPTHWRITE_* (0 = enable) */
	} depth;
	struct {
		bool enable;
		int  filter;
		int  env;               /* PVR_TXRENV_* */
		bool alpha;             /* true => disable texture alpha */
		int  uv_flip, uv_clamp, mipmap_bias;
		bool mipmap;
		int  width, height;
		int  format;            /* PVR_TXRFMT_* bits */
		pvr_ptr_t base;
	} txr;
} pvr_poly_cxt_t;

/* ---- API ---------------------------------------------------------------- */

int  pvr_init(void);            /* open /dev/pvr, map VRAM/regs/FIFO */
void pvr_shutdown(void);

void pvr_scene_begin(void);     /* TA_LIST_INIT: start a new frame's lists */
void pvr_list_begin(pvr_list_t list);
void pvr_prim(const void *data, int size);   /* stream a 32-byte parameter */
void pvr_list_finish(void);
void pvr_scene_finish(void);    /* STARTRENDER + wait, then page flip */
void pvr_wait_ready(void);      /* block until the next vertical blank */

void pvr_set_bg_color(float r, float g, float b);

/* Simple linear VRAM heap for textures / buffers. */
pvr_ptr_t pvr_mem_malloc(size_t size);
void      pvr_txr_load(const void *src, pvr_ptr_t dst, size_t size);

/* Compile a context into a header, and helpers to fill common contexts. */
void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src);
void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list);
void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int fmt,
                      int tw, int th, pvr_ptr_t addr, int filter);

/* Framebuffer geometry (set by pvr_init from the current pvr2fb mode). */
extern int pvr_screen_w, pvr_screen_h;

uint32_t pvr_debug_fb_addr(void);   /* current scanout address (debug) */
uint32_t pvr_debug_reg(unsigned off);        /* raw PVR register read (debug) */
uint32_t pvr_debug_vram_u32(uint32_t off);   /* raw VRAM word read (debug) */

#ifdef __cplusplus
}
#endif

#endif /* LIBPVR_H */
