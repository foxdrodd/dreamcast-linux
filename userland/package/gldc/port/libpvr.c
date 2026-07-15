#include <float.h>
#include <stdio.h>
#include <string.h>

#include "../platform.h"
#include "libpvr.h"

/*
 * Word-based copy into the PVR's 64-bit VRAM area.  That area corrupts CPU
 * byte / 16-bit stores (and 32-bit stores made in scattered order), so GLdc's
 * default byte-wise memcpy_fast mangles texture uploads.  Route all texture
 * VRAM writes through libc memcpy (the same word/burst copy pvr_txr_load uses
 * and which is proven clean).  A real out-of-line function so the compiler
 * cannot re-inline it as narrow stores at the call sites.
 */
void _glVramCopy(void* dst, const void* src, size_t bytes) {
    memcpy(dst, src, bytes);
}

/*
 * Scene submission for the libpvr platform.  The clipping / perspective-divide
 * / tri-strip logic is identical to the KOS (sh4.c) platform -- it is pure CPU
 * work.  The only difference is the transport: instead of writing each header
 * (32 bytes) and vertex (64 bytes, TA float-colour format) to the store queues,
 * we hand them to libpvr's pvr_prim(), which accumulates the frame and DMAs it
 * to the TA at pvr_scene_finish().
 */

#define CLIP_DEBUG 0

/* Register offsets in the 0x005f8000 block (PVR_SET masks to the block). */
#define SPAN_SORT_CFG       0x005F8030
#define PVR_TEXTURE_MODULO  0x005F804C

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

GL_FORCE_INLINE void _glPVRSetTextureStride(uint32_t stride) {
    PVR_SET(PVR_TEXTURE_MODULO, (stride / 32));
}

void* GPUMemoryAlloc(size_t size) {
    /* GLdc writes texture data directly through this pointer and derives the
     * texture-control-word address from it (ptr & 0x00fffff8) >> 3, so it must
     * be a real CPU pointer into VRAM whose low bits are the heap offset. */
    return pvr_vram_ptr(pvr_mem_malloc(size));
}

void InitGPU(_Bool autosort, _Bool fsaa) {
    (void) autosort;
    (void) fsaa;
    /* libpvr adopts pvr2fb's mode and sets up the TA/ISP buffers itself. */
    pvr_init();
}

void ShutdownGPU(void) {
    pvr_shutdown();
}

GL_FORCE_INLINE float _glFastInvert(float x) {
    return (1.0f / __builtin_sqrtf(x * x));
}

GL_FORCE_INLINE void _glPerspectiveDivideVertex(Vertex* vertex, int count) {
    TRACE();
    for(int v = 0; v < count; ++v) {
        const float f = _glFastInvert(vertex[v].w);
        vertex[v].xyz[0] *= f;
        vertex[v].xyz[1] *= f;
        if(vertex[v].w == 1.0f) {
            vertex[v].xyz[2] = _glFastInvert(1.0001f + vertex[v].xyz[2]);
        } else {
            vertex[v].xyz[2] = f;
        }
    }
}

/* A PolyHeader is 32 bytes on the wire; a Vertex is 64 (TA float-colour). */
static inline void _glPushHeader(Vertex* v, size_t count) {
    pvr_prim(v, (int)(count * 32));
}

static inline void _glPushVertex(Vertex* v, size_t count) {
    pvr_prim(v, (int)(count * 64));
}

static inline void _glClipEdge(const Vertex* const v1, const Vertex* const v2, Vertex* vout) {
    const float d0 = v1->w + v1->xyz[2];
    const float d1 = v2->w + v2->xyz[2];
    const float t = (fabsf(d0) * (1.0f / sqrtf((d1 - d0) * (d1 - d0))));
    const float invt = 1.0f - t;

    vout->xyz[0] = invt * v1->xyz[0] + t * v2->xyz[0];
    vout->xyz[1] = invt * v1->xyz[1] + t * v2->xyz[1];
    vout->xyz[2] = invt * v1->xyz[2] + t * v2->xyz[2];
    vout->xyz[2] = (vout->xyz[2] < FLT_EPSILON) ? FLT_EPSILON : vout->xyz[2];

    vout->uv[0] = invt * v1->uv[0] + t * v2->uv[0];
    vout->uv[1] = invt * v1->uv[1] + t * v2->uv[1];

    vout->w = invt * v1->w + t * v2->w;

    vout->argb[0] = invt * v1->argb[0] + t * v2->argb[0];
    vout->argb[1] = invt * v1->argb[1] + t * v2->argb[1];
    vout->argb[2] = invt * v1->argb[2] + t * v2->argb[2];
    vout->argb[3] = invt * v1->argb[3] + t * v2->argb[3];
}

enum Visible {
    NONE_VISIBLE = 0,
    FIRST_VISIBLE = 1,
    SECOND_VISIBLE = 2,
    THIRD_VISIBLE = 4,
    FIRST_AND_SECOND_VISIBLE = FIRST_VISIBLE | SECOND_VISIBLE,
    SECOND_AND_THIRD_VISIBLE = SECOND_VISIBLE | THIRD_VISIBLE,
    FIRST_AND_THIRD_VISIBLE = FIRST_VISIBLE | THIRD_VISIBLE,
    ALL_VISIBLE = 7
};

static size_t CURRENT_TEXTURE_STRIDE = 0;

static inline bool is_header(Vertex* v) {
    return !(v->flags == GPU_CMD_VERTEX || v->flags == GPU_CMD_VERTEX_EOL);
}

void SceneListSubmit(Vertex* vertices, int n) {
    TRACE();

    /* You need at least a header, and 3 vertices to render anything */
    if(n < 4) {
        return;
    }

    PVR_SET(SPAN_SORT_CFG, 0x0);

    static Vertex __attribute__((aligned(32))) qv;
    Vertex* queued_vertex = NULL;

#define QUEUE_VERTEX(v) \
    do { queued_vertex = &qv; *queued_vertex = *(v); } while(0)

#define SUBMIT_QUEUED_VERTEX(sflags) \
    do { if(queued_vertex) { queued_vertex->flags = (sflags); _glPushVertex(queued_vertex, 1); queued_vertex = NULL; } } while(0)

    int visible_mask = 0;

    Vertex* v0 = vertices;
    for(int i = 0; i < n - 1; ++i, ++v0) {
        if(is_header(v0)) {
            PolyHeader* header = (PolyHeader*) v0;
            if(header->meta.texture_is_strided && header->meta.texture_stride != CURRENT_TEXTURE_STRIDE) {
                _glPVRSetTextureStride(header->meta.texture_stride);
                CURRENT_TEXTURE_STRIDE = header->meta.texture_stride;
            }

            _glPushHeader(v0, 1);
            visible_mask = 0;
            continue;
        }

        Vertex* v1 = v0 + 1;
        Vertex* v2 = (i < n - 2) ? v0 + 2 : NULL;

        assert(!is_header(v1));

        bool is_trailing = (v1->flags == GPU_CMD_VERTEX_EOL) || ((v2) ? is_header(v2) : true);

        if(is_trailing) {
            if(visible_mask == ALL_VISIBLE) {
                SUBMIT_QUEUED_VERTEX(qv.flags);

                _glPerspectiveDivideVertex(v0, 2);
                v1->flags = GPU_CMD_VERTEX_EOL;
                _glPushVertex(v0, 2);
            } else {
                SUBMIT_QUEUED_VERTEX(GPU_CMD_VERTEX_EOL);
            }

            i++;
            v0++;
            visible_mask = 0;
            continue;
        }

        visible_mask = (
            (v0->xyz[2] >= -v0->w) << 0 |
            (v1->xyz[2] >= -v1->w) << 1 |
            (v2->xyz[2] >= -v2->w) << 2
        );

        if(visible_mask == NONE_VISIBLE) {
            SUBMIT_QUEUED_VERTEX(GPU_CMD_VERTEX_EOL);
        } else {
            SUBMIT_QUEUED_VERTEX(qv.flags);
        }

        Vertex __attribute__((aligned(32))) scratch[4];
        Vertex* a = &scratch[0], *b = &scratch[1], *c = &scratch[2], *d = &scratch[3];

        switch(visible_mask) {
            case ALL_VISIBLE:
                _glPerspectiveDivideVertex(v0, 1);
                QUEUE_VERTEX(v0);
            break;
            case NONE_VISIBLE:
                break;
            case FIRST_VISIBLE:
                _glClipEdge(v0, v1, a);
                a->flags = GPU_CMD_VERTEX;
                _glClipEdge(v2, v0, b);
                b->flags = GPU_CMD_VERTEX;
                _glPerspectiveDivideVertex(v0, 1);
                _glPushVertex(v0, 1);
                _glPerspectiveDivideVertex(a, 2);
                _glPushVertex(a, 2);
                QUEUE_VERTEX(b);
            break;
            case SECOND_VISIBLE:
                memcpy_vertex(c, v1);
                _glClipEdge(v0, v1, a);
                a->flags = GPU_CMD_VERTEX;
                _glClipEdge(v1, v2, b);
                b->flags = v2->flags;
                _glPerspectiveDivideVertex(a, 3);
                _glPushVertex(a, 1);
                _glPushVertex(c, 1);
                QUEUE_VERTEX(b);
            break;
            case THIRD_VISIBLE:
                memcpy_vertex(c, v2);
                _glClipEdge(v1, v2, a);
                a->flags = GPU_CMD_VERTEX;
                _glClipEdge(v2, v0, b);
                b->flags = GPU_CMD_VERTEX;
                _glPerspectiveDivideVertex(a, 3);
                _glPushVertex(a, 2);
                QUEUE_VERTEX(c);
            break;
            case FIRST_AND_SECOND_VISIBLE:
                memcpy_vertex(c, v1);
                _glClipEdge(v2, v0, b);
                b->flags = GPU_CMD_VERTEX;
                _glPerspectiveDivideVertex(v0, 1);
                _glPushVertex(v0, 1);
                _glClipEdge(v1, v2, a);
                a->flags = v2->flags;
                _glPerspectiveDivideVertex(a, 3);
                _glPushVertex(c, 1);
                _glPushVertex(b, 2);
                QUEUE_VERTEX(a);
            break;
            case SECOND_AND_THIRD_VISIBLE:
                memcpy_vertex(c, v1);
                memcpy_vertex(d, v2);
                _glClipEdge(v0, v1, a);
                a->flags = GPU_CMD_VERTEX;
                _glClipEdge(v2, v0, b);
                b->flags = GPU_CMD_VERTEX;
                _glPerspectiveDivideVertex(a, 4);
                _glPushVertex(a, 1);
                _glPushVertex(c, 1);
                _glPushVertex(b, 2);
                QUEUE_VERTEX(d);
            break;
            case FIRST_AND_THIRD_VISIBLE:
                memcpy_vertex(c, v2);
                c->flags = GPU_CMD_VERTEX;
                _glClipEdge(v0, v1, a);
                a->flags = GPU_CMD_VERTEX;
                _glClipEdge(v1, v2, b);
                b->flags = GPU_CMD_VERTEX;
                _glPerspectiveDivideVertex(v0, 1);
                _glPushVertex(v0, 1);
                _glPerspectiveDivideVertex(a, 3);
                _glPushVertex(a, 1);
                _glPushVertex(c, 1);
                _glPushVertex(b, 1);
                QUEUE_VERTEX(c);
            break;
            default:
                fprintf(stderr, "ERROR\n");
        }
    }

    SUBMIT_QUEUED_VERTEX(GPU_CMD_VERTEX_EOL);
}

void SceneBegin(void) {
    CURRENT_TEXTURE_STRIDE = 0;
    pvr_wait_ready();
    pvr_scene_begin();
}

void SceneListBegin(GPUList list) {
    pvr_list_begin((pvr_list_t) list);
}

void SceneListFinish(void) {
    pvr_list_finish();
}

void SceneFinish(void) {
    pvr_scene_finish();
}

const VideoMode* GetVideoMode(void) {
    static VideoMode mode;
    mode.width = pvr_screen_w;
    mode.height = pvr_screen_h;
    return &mode;
}
