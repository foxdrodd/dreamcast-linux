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
| `demos/nehe05_cube.c` | NeHe lesson 5 — spinning Gouraud pyramid + multicolour cube (no textures). |
| `demos/nehe06_texcube.c` | NeHe lesson 6 — texture-mapped spinning cube (loads `assets/NeHe.bmp`). Proves the twiddled-texture path through `glTexImage2D`. |
| `demos/torus.c` | Lit, texture-mapped spinning torus — procedural checker texture, CPU T&L, and world-fixed directional Gouraud shading (normals rotated in C so the light stays put as the donut spins). The full textured 3D path in one demo. |
| `demos/control.c` | **Interactive** lit textured object you drive with the Dreamcast controller (Maple → evdev). Component-6 acceptance demo: controllable, textured, lit, double-buffered 3D. See controls below. |
| `demos/loadbmp.[ch]` | Minimal 24-bit BMP loader (from GLdc samples) used by the texture demo. |
| `assets/NeHe.bmp` | 256×128 texture for `gldc_texcube`. |
| `gldc_cube`, `gldc_texcube`, `gldc_torus`, `gldc_control` | Prebuilt static sh4 binaries (run straight off NFS). |

### `gldc_control` — controller input

Reads the controller straight from the kernel evdev node (`/dev/input/eventN`, auto-detected
by the `"Dreamcast Controller"` name; the `maplecontrol` joystick driver exposes buttons as
`BTN_*` and the stick/triggers as `ABS_*`). Raw `struct input_event` reads, non-blocking, one
drain per frame — no SDL needed.

| Input | Action |
|-------|--------|
| Analog stick | tilt / spin (deflection = angular velocity) |
| D-pad | nudge rotation (digital) |
| R trigger / L trigger | zoom in / out |
| A | toggle auto-spin |
| B | switch shape (torus ↔ cube) |
| X | cycle texture (checker / stripes / neon grid) |
| START | reset view |

Optional startup args: `gldc_control [cube|torus] [texstyle 0..2]`.

## sh4zam-accelerated builds (`gldc_torus_shz`, `gldc_control_shz`)

The same `torus.c`/`control.c` sources build a second variant that replaces the per-vertex
`cosf`/`sinf` (called 4× per vertex) with [sh4zam](https://github.com/gyrovorbis/sh4zam)'s
`shz_sincosf` — one SH4 `FSCA` instruction for sin+cos — via a `-DUSE_SH4ZAM` `SINCOS()` macro.
Measured on real hardware, the spinning torus goes **20.2 → 30.2 fps (+50%)**, pixel-identical.

Build requirements (see `Makefile` `gldc_torus_shz`): the **gcc-17 sh4 cross** (full C23, sh4zam
needs it) at `/media/flo/nvme0-ssd/gcc/sh4-gcc17/install/bin/`, `-DSHZ_BACKEND=1` (else sh4zam
silently uses its software C backend on non-`__DREAMCAST__` targets), and **no `-ffast-math`**
(else `shz_sincosf` falls back to `__builtin_sinf/cosf`). Under our `-m4` ABI (resting FPSCR.PR=1)
the `FSCA`/`FTRV`/`FIPR` primitives compute correctly bare; only the trivial standalone
`shz_inv_sqrtf_fsrra` is unreliable (its non-`volatile` asm no-ops at PR=1) — use the composite
ops (`shz_vec3_normalize`, `shz_sincosf`, `shz_vec3_dot`, `shz_xmtrx_*`).

## Why sh4zam is NOT wired into GLdc core (attempted, reverted)

Wiring sh4zam into the GLdc platform layer itself (so `VEC3_DOT`/`VEC3_NORMALIZE` etc. use
`shz_vec3_dot`/`shz_vec3_normalize`) was tried and **reverted** — it broke the rotation of every
GLdc demo that uses `glRotatef` (the NeHe cube/texcube "zoomed" instead of rotating).

Root cause: `glRotatef` (`GL/matrix.c`) normalises its rotation axis with `VEC3_NORMALIZE`.
sh4zam's `shz_vec3_normalize` uses **FSRRA** internally, and under our `-m4` ABI (resting
FPSCR.PR=1) that bare FSRRA **silently no-ops** in `glRotatef`'s codegen context — so the axis
came out un-normalised and the rotation matrix was garbage. (The demo-level `shz_sincosf`/**FSCA**
in `torus.c`/`control.c` is unaffected — FSCA computes correctly bare at PR=1; only FSRRA is the
problem, and it's *context-dependent*, which is why isolated tests passed but `glRotatef` failed.)

Lesson: sh4zam's asm assumes KOS's PR=0 resting mode and is not PR-safe for the Linux `-m4`
toolchain. GLdc's own `MATH_fsrra` (which wraps FSRRA in a save/clear/restore-PR block) is the
correct primitive for GLdc core; sh4zam is used only at the demo level where FSCA suffices. A
proper GLdc-core integration would require patching sh4zam's PR-sensitive asm to self-manage the
PR bit — not worth it for the current low-poly, fill-bound NeHe demos.
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
and `libpvr` built next door (`../libpvr/libpvr.a`). The GLdc lib itself has **no** sh4zam
dependency; only the `*_shz` demo variants use sh4zam (an **sh4zam** checkout at
`SHZ=/home/flo/devel/sh4zam` plus the gcc-17 sh4 cross — see above).

```sh
make -C ../libpvr libpvr.a          # once, if not built
make GLDC=/home/flo/devel/GLdc libgldc   # build GLdc's libpvr platform lib
make GLDC=/home/flo/devel/GLdc           # -> ./gldc_cube (static)
```

## Run (real hardware)

```sh
# on the host: files reach the DC over NFS (host uclibc dir -> DC /mnt)
mount -t nfs -o nolock,vers=3 192.168.0.225:/home/flo/devel/t2-hacking/uclibc /mnt
killall gldc_cube gldc_texcube gldc_torus gldc_control 2>/dev/null  # avoid stacking instances that fight over the TA
/mnt/gldc_cube &
/mnt/gldc_torus &    # needs no asset -- texture is generated procedurally
/mnt/gldc_control &  # interactive: drive it with the Dreamcast controller
# textured cube needs its BMP alongside on the DC (default /mnt/NeHe.bmp; argv[1] overrides):
cp assets/NeHe.bmp /path/to/uclibc/      # host side, so it lands at DC /mnt/NeHe.bmp
/mnt/gldc_texcube &
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

## The second gotcha: byte/16-bit CPU writes corrupt the 64-bit VRAM area

Textures live in the PVR's **64-bit VRAM area** (bank-interleaved). CPU **byte or
16-bit stores** to that area — and 32-bit stores made in scattered (twiddled) order —
get corrupted (address bits leak into the low colour bits, so dark texels pick up a
blue tint). Only **word-based `memcpy`-style bulk writes** (what `libpvr`'s
`pvr_txr_load` uses) are clean. GLdc's stock `FASTCPY`/`MEMCPY` is a byte-wise
`memcpy_fast`, so texture uploads came out blue-shifted.

Fix (in `port/`): `FASTCPY`/`MEMCPY`/`MEMCPY4` are redefined to `_glVramCopy`
(`port/libpvr.c`), an out-of-line `libc memcpy` wrapper — out-of-line so the compiler
can't re-inline it as narrow stores. This fixes the `glTexSubImage2D` upload path
(which stages in RAM then `FASTCPY`s to VRAM). The GLdc-clone core also carries a
small companion patch in `GL/texture.c` (the `glTexImage2D`-with-data general-case
loop stages into RAM then `FASTCPY`s, instead of converting straight into VRAM) — not
reproduced under `port/` since it lives in GLdc core; re-apply if rebuilding from a
fresh GLdc checkout. Rule: never write textures to VRAM with sub-word CPU stores.
