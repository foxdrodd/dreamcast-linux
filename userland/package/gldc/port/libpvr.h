#pragma once

/*
 * GLdc platform for Dreamcast running Linux, backed by libpvr (/dev/pvr).
 *
 * Same SH4 CPU and PowerVR2 as the KOS (sh4.h) platform, so the FPU math and
 * XMTRX matrix asm are reused as-is; the difference is that all GPU/VRAM/TA
 * access goes through libpvr's mmap'd /dev/pvr instead of KOS's bare-metal P4
 * pointers and store queues.  Scene submission accumulates parameters via
 * pvr_prim() and libpvr DMAs them to the TA.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <libpvr.h>

/* sh4zam SH4 math library (github.com/gyrovorbis/sh4zam) -- FIPR dot products
 * and FSRRA reciprocal-sqrt for the vector helpers below.  Built with
 * -DSHZ_BACKEND=1 (Makefile.libpvr) to force the SH4 asm backend.  Under our
 * -m4 ABI (resting FPSCR.PR=1) the composite ops (shz_vec3_dot / _normalize)
 * compute correctly; the bare standalone shz_inv_sqrtf_fsrra does NOT (it
 * no-ops at PR=1), so 1/x-style inverses stay on the port's PR-managed
 * MATH_fsrra below.  See memory sh4zam-integration. */
#include <sh4zam/shz_sh4zam.h>

#include "../types.h"
#include "../private.h"

#ifndef NDEBUG
#define PERF_WARNING(msg) printf("[PERF] %s\n", msg)
#else
#define PERF_WARNING(msg) (void) 0
#endif

#ifndef GL_FORCE_INLINE
#define GL_NO_INSTRUMENT inline __attribute__((no_instrument_function))
#define GL_INLINE_DEBUG GL_NO_INSTRUMENT __attribute__((always_inline))
#define GL_FORCE_INLINE static GL_INLINE_DEBUG
#endif

/* --------------------------- SH4 fast math ------------------------------- */

/*
 * Enter/leave single-precision FP mode (FPSCR.PR = 0) around a hand-written FP
 * arithmetic op.  The -m4 Linux ABI rests in double mode (PR = 1) and GCC's
 * emitted float code XOR-toggles PR relative to that resting state, so any asm
 * that clears PR MUST restore it on exit or every following C float op runs in
 * the wrong precision.  ENTER saves FPSCR in r2 and clears PR; EXIT restores it.
 * Clobbers r0, r1, r2 -- list all three in the asm clobbers.
 */
#define FP_SINGLE_ENTER \
    "sts fpscr, r2\n\t" \
    "mov #1, r1\n\t" \
    "shll16 r1\n\t" "shll r1\n\t" "shll r1\n\t" "shll r1\n\t" \
    "not r1, r1\n\t" \
    "mov r2, r0\n\t" \
    "and r1, r0\n\t" \
    "lds r0, fpscr\n\t"
#define FP_SINGLE_EXIT \
    "lds r2, fpscr\n\t"

GL_FORCE_INLINE float MATH_fsrra(float x) {   /* 1/sqrt(x) */
    asm volatile (
        FP_SINGLE_ENTER
        "fsrra %[v]\n\t"
        FP_SINGLE_EXIT
        : [v] "+f" (x) : : "r0", "r1", "r2");
    return x;
}

GL_FORCE_INLINE float MATH_Fast_Invert(float x) {   /* 1/x */
    int neg = x < 0.0f;
    x = MATH_fsrra(x * x);
    return neg ? -x : x;
}

#define PREFETCH(addr) __builtin_prefetch((addr))

GL_FORCE_INLINE void* memcpy_fast(void *dest, const void *src, size_t len) {
    if(!len) return dest;
    const uint8_t *s = (uint8_t *)src;
    uint8_t *d = (uint8_t *)dest;
    uint32_t diff = (uint32_t)d - (uint32_t)(s + 1);
    asm volatile (
        "clrs\n"
    ".align 2\n"
    "0:\n\t"
        "dt %[size]\n\t"
        "mov.b @%[in]+, %[scratch]\n\t"
        "bf.s 0b\n\t"
        " mov.b %[scratch], @(%[offset], %[in])\n"
        : [in] "+&r" ((uint32_t)s), [scratch] "=&r" ((uint32_t)d), [size] "+&r" (len)
        : [offset] "z" (diff)
        : "t", "memory"
    );
    return dest;
}

/* Copies may target the PVR's 64-bit VRAM area, which corrupts byte / 16-bit
 * CPU stores -- so texture uploads must use a word-based copy, NOT the byte-wise
 * memcpy_fast.  Route them through _glVramCopy (libc memcpy, out-of-line). */
extern void _glVramCopy(void* dst, const void* src, size_t bytes);
#define FASTCPY(dst, src, bytes) _glVramCopy((dst), (src), (bytes))
#define MEMCPY(dst, src, bytes)  _glVramCopy((dst), (src), (bytes))
#define MEMCPY4(dst, src, bytes) _glVramCopy((dst), (src), (bytes))
#define MEMSET4(dst, v, size)    memset((dst), (v), (size))

/* Vector helpers on sh4zam: FIPR dot, FSRRA-based normalize/length.
 * NB: access the result via _n.e[] not _n.x -- the macro params are named x/y/z
 * and would text-substitute into a `.x` member access. */
#define VEC3_NORMALIZE(x, y, z) \
    do { \
        shz_vec3_t _n = shz_vec3_normalize((shz_vec3_t){ .e = { (x), (y), (z) } }); \
        (x) = _n.e[0]; (y) = _n.e[1]; (z) = _n.e[2]; \
    } while(0)

#define VEC3_LENGTH(x, y, z, d) \
    d = shz_vec3_magnitude((shz_vec3_t){ .e = { (x), (y), (z) } })

#define VEC3_DOT(x1, y1, z1, x2, y2, z2, d) \
    d = shz_vec3_dot((shz_vec3_t){ .e = { (x1), (y1), (z1) } }, \
                     (shz_vec3_t){ .e = { (x2), (y2), (z2) } })

/* ------------------------ XMTRX matrix (asm) ----------------------------- */

typedef float matrix_t[4][4] __attribute__((aligned(8)));

extern void mat_load(const matrix_t *src);
extern void mat_store(matrix_t *out);
extern void mat_apply(const matrix_t *src);
extern void mat_identity(void);

GL_FORCE_INLINE void UploadMatrix4x4(const Matrix4x4* mat) {
    mat_load((const matrix_t*) mat);
}
GL_FORCE_INLINE void DownloadMatrix4x4(Matrix4x4* mat) {
    mat_store((matrix_t*) mat);
}
GL_FORCE_INLINE void MultiplyMatrix4x4(const Matrix4x4* mat) {
    mat_apply((const matrix_t*) mat);
}

#define mat_trans_single4(x, y, z, w) { \
        register float __x __asm__("fr0") = (x); \
        register float __y __asm__("fr1") = (y); \
        register float __z __asm__("fr2") = (z); \
        register float __w __asm__("fr3") = (w); \
        __asm__ __volatile__( \
            FP_SINGLE_ENTER \
            "ftrv	xmtrx,fv0\n\t" \
            "fdiv	fr3,fr0\n\t" \
            "fdiv	fr3,fr1\n\t" \
            "fdiv	fr3,fr2\n\t" \
            "fldi1	fr4\n\t" \
            "fdiv	fr3,fr4\n\t" \
            "fmov	fr4,fr3\n\t" \
            FP_SINGLE_EXIT \
            : "=f" (__x), "=f" (__y), "=f" (__z), "=f" (__w) \
            : "0" (__x), "1" (__y), "2" (__z), "3" (__w) \
            : "fr4", "r0", "r1", "r2" ); \
        x = __x; y = __y; z = __z; w = __w; \
    }

#define mat_trans_single3_nodiv_nomod(x, y, z, x2, y2, z2) { \
        register float __x __asm__("fr12") = (x); \
        register float __y __asm__("fr13") = (y); \
        register float __z __asm__("fr14") = (z); \
        __asm__ __volatile__( \
            FP_SINGLE_ENTER \
            "fldi1 fr15\n\t" \
            "ftrv  xmtrx, fv12\n\t" \
            FP_SINGLE_EXIT \
            : "=f" (__x), "=f" (__y), "=f" (__z) \
            : "0" (__x), "1" (__y), "2" (__z) \
            : "fr15", "r0", "r1", "r2" ); \
        x2 = __x; y2 = __y; z2 = __z; \
    }

#define mat_trans_normal3_nomod(x, y, z, x2, y2, z2) { \
        register float __x __asm__("fr8") = (x); \
        register float __y __asm__("fr9") = (y); \
        register float __z __asm__("fr10") = (z); \
        __asm__ __volatile__( \
            FP_SINGLE_ENTER \
            "fldi0 fr11\n\t" \
            "ftrv  xmtrx, fv8\n\t" \
            FP_SINGLE_EXIT \
            : "=f" (__x), "=f" (__y), "=f" (__z) \
            : "0" (__x), "1" (__y), "2" (__z) \
            : "fr11", "r0", "r1", "r2" ); \
        x2 = __x; y2 = __y; z2 = __z; \
    }

GL_FORCE_INLINE void TransformVec3(float* x) {
    mat_trans_single4(x[0], x[1], x[2], x[3]);
}
GL_FORCE_INLINE void TransformVec3NoMod(const float* xIn, float* xOut) {
    mat_trans_single3_nodiv_nomod(xIn[0], xIn[1], xIn[2], xOut[0], xOut[1], xOut[2]);
}
GL_FORCE_INLINE void TransformNormalNoMod(const float* in, float* out) {
    mat_trans_normal3_nomod(in[0], in[1], in[2], out[0], out[1], out[2]);
}
inline void TransformVec4(float* x) { (void) x; }

GL_FORCE_INLINE void TransformVertex(float x, float y, float z, float w, float* oxyz, float* ow) {
    register float __x __asm__("fr4") = x;
    register float __y __asm__("fr5") = y;
    register float __z __asm__("fr6") = z;
    register float __w __asm__("fr7") = w;
    __asm__ __volatile__(
        FP_SINGLE_ENTER
        "ftrv   xmtrx,fv4\n\t"
        FP_SINGLE_EXIT
        : "=f" (__x), "=f" (__y), "=f" (__z), "=f" (__w)
        : "0" (__x), "1" (__y), "2" (__z), "3" (__w)
        : "r0", "r1", "r2"
    );
    oxyz[0] = __x; oxyz[1] = __y; oxyz[2] = __z; *ow = __w;
}

/* --------------------------- GPU over libpvr ----------------------------- */

/* Register block helpers: PVR_SET/GET take either a full 0x005f80xx address or
 * a bare offset; mask to the 8 KB block offset either way. */
#define PVR_SET(o, v) pvr_regw((o) & 0x1fff, (uint32_t)(v))
#define PVR_GET(o)    pvr_regr((o) & 0x1fff)

void InitGPU(_Bool autosort, _Bool fsaa);
void ShutdownGPU(void);
void* GPUMemoryAlloc(size_t size);

GL_FORCE_INLINE size_t GPUMemoryAvailable(void) {
    return pvr_mem_available();
}
GL_FORCE_INLINE void GPUSetBackgroundColour(float r, float g, float b) {
    pvr_set_bg_color(r, g, b);
}

#define PT_ALPHA_REF 0x011c
GL_FORCE_INLINE void GPUSetAlphaCutOff(uint8_t val) {
    PVR_SET(PT_ALPHA_REF, val);
}

/* Palette / fog / z-clip: not yet wired in libpvr -- stubs so GLdc links and
 * runs (opaque, untextured-or-RGB paths).  Extend libpvr later. */
GL_FORCE_INLINE void GPUSetPaletteFormat(GPUPaletteFormat format) { (void) format; }
GL_FORCE_INLINE void GPUSetPaletteEntry(uint32_t idx, uint32_t value) { (void) idx; (void) value; }
GL_FORCE_INLINE void GPUSetClearDepth(float v) { (void) v; }
GL_FORCE_INLINE void GPUSetFogLinear(float start, float end) { (void) start; (void) end; }
GL_FORCE_INLINE void GPUSetFogExp(float density) { (void) density; }
GL_FORCE_INLINE void GPUSetFogExp2(float density) { (void) density; }
GL_FORCE_INLINE void GPUSetFogColor(float r, float g, float b, float a) { (void) r; (void) g; (void) b; (void) a; }
