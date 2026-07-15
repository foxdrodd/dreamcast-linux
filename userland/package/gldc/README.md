# gldc — fixed-function OpenGL 1.x on the Dreamcast PowerVR2 (over libpvr)

[GLdc](https://github.com/Kazade/GLdc) ported to run on Dreamcast **Linux**, on top
of [`libpvr`](../libpvr) → `/dev/pvr` → PowerVR2. Gives the familiar GL 1.x API
(matrix stack, `glBegin/glEnd`, Gouraud, texturing, blend → PVR modes); the SH-4 CPU
does transform & lighting via its `FTRV`/`FIPR` matrix asm, the PVR does
texturing/blend/hidden-surface removal.

This is Component 4 of the PVR2 3D plan. C1–C3 (QEMU TA, `/dev/pvr` kernel shim,
`libpvr`) are proven on real hardware; this package is the GL layer + demos.

## Layout

| Path | What |
|------|------|
| `demos/nehe05_cube.c` | NeHe lesson 5 — spinning Gouraud pyramid + multicolour cube (no textures). The acceptance demo. |
| `gldc_cube` | Prebuilt static sh4 binary of the above (run it straight off NFS). |
| `port/` | **Read-only snapshot** of the Dreamcast-Linux GLdc platform port — the actual porting work. Source of truth is the GLdc clone (see below). |

### The platform port (`port/`)

GLdc's core (`GL/*.c`) is cleanly abstracted behind `GL/platform.h`; the whole port
is one new platform, selected with `-DGLDC_LIBPVR`:

- `port/libpvr.h` — SH-4 fast math, the XMTRX matrix wrappers, GPU/register glue over libpvr.
- `port/libpvr.c` — scene submission (`pvr_prim`), GPU init/alloc, tri-strip clip logic.
- `port/libpvr_mat.S` — the KOS XMTRX matrix asm, Linux ABI (no leading underscore).
- `port/Makefile.libpvr` — builds `build-libpvr/libGLdc.a` in the clone.

These four files are copied here so the port is version-tracked in-tree even if the
clone is lost. **They are a snapshot, not the build input** — edit them in the GLdc
clone (`$(GLDC)/GL/platforms/…`) where they compile, then refresh this copy.

## Build

Needs a GLdc clone with the port files in place (default `GLDC=/home/flo/devel/GLdc`)
and `libpvr` built next door (`../libpvr/libpvr.a`).

```sh
make -C ../libpvr libpvr.a          # once, if not built
make GLDC=/home/flo/devel/GLdc libgldc   # build GLdc's libpvr platform lib
make GLDC=/home/flo/devel/GLdc           # -> ./gldc_cube (static)
```

## Run (real hardware)

```sh
# on the host: files reach the DC over NFS (host uclibc dir -> DC /mnt)
mount -t nfs -o nolock,vers=3 192.168.0.225:/home/flo/devel/t2-hacking/uclibc /mnt
killall gldc_cube 2>/dev/null       # avoid stacking instances that fight over the TA
/mnt/gldc_cube &
# grab VGA a second after launch (first frame or two are empty)
```

## The one gotcha that mattered: FP precision mode (FPSCR.PR)

The sh4-linux toolchain is **`-m4` double-FP only** (`__SH_FPU_DOUBLE__=1`; no
`-m4-single`). The SH-4 `FTRV`/`FIPR`/`FSRRA`/`FDIV` ops are only defined in
**single-precision mode (PR=0)**, but `-m4` rests in double mode (PR=1). GCC's
emitted float code *toggles* PR relative to that assumed resting PR=1 for each
single op, so:

- **Never force PR=0 globally** (e.g. a `set_single_fp()` at startup) — GCC's toggle
  then flips it to *double* for every float op → all matrix/perspective math computes
  garbage (Inf/NaN → collapsed geometry).
- Every hand-written FP-**arithmetic** asm block must **save FPSCR, clear PR, do the
  op, restore FPSCR** — otherwise it leaves PR=0 and the next GCC float op runs in
  double mode. This is done inside `port/libpvr_mat.S` (`mat_apply`/`mat_multiply`)
  and `port/libpvr.h` (`FP_SINGLE_ENTER`/`EXIT` around the ftrv transform macros and
  `MATH_fsrra`). `fmov`/`fldi` are PR-agnostic; `ftrv`/`fdiv`/`fmul`/`fmac`/`fsrra`
  are not.

Matrix memory must be 8-byte (ideally 32-byte) aligned or the `fmov` pair moves take
an unaligned FP fault the kernel can't fix up. GLdc's `Matrix4x4` globals/stack are
already `__attribute__((aligned(32)))`.
