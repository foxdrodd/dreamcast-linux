# libpvr — PowerVR2 3D rendering for Dreamcast Linux userland

A small, **KallistiOS-compatible** PVR API implemented over the `/dev/pvr` kernel
shim (`drivers/gpu/pvr2`).  It lets userland drive the Dreamcast's PowerVR2 tile
accelerator for hardware 3D, and is the foundation the GL layer (GLdc) and the
demo sit on.

## Architecture

```
your app / GLdc
    └─ libpvr            KOS-style API: pvr_init, scene/list/prim, poly compile
         └─ /dev/pvr     mmap(VRAM, PVR regs, TA FIFO) + WAIT_VSYNC/WAIT_RENDER
              └─ PowerVR2 TA/ISP/TSP   (real HW, or QEMU's dc-ta software TBDR)
```

The data structures, constants and header-compile logic mirror KOS `dc/pvr.h`
(see `include/libpvr.h`, and `pvr_poly_compile` in `src/libpvr.c`, adapted from
KOS `pvr_prim.c`) so KOS-derived code ports with minimal change.  The runtime
replaces KOS's bare-metal store queues / threads with the mmap'd hardware windows
and the blocking vblank/render-done ioctls that `/dev/pvr` exposes.

## Build / test

```
make CROSS=sh4-linux-gnu-          # libpvr.a + examples/tritest (static)
make install DESTDIR=<sysroot>     # /usr/lib/libpvr.a + /usr/include/libpvr.h
```

`examples/tritest` is the verification vehicle: run static as PID 1 in an
initramfs, a QEMU screendump shows a Gouraud triangle (see the repo's PVR dev
loop).  Verified end-to-end: `pvr_init` → `pvr_poly_cxt_col`/`pvr_poly_compile`
→ per-frame `pvr_scene_begin`/`pvr_list_begin`/`pvr_prim`/`pvr_scene_finish`
renders the triangle through the real TA path.

## `/dev/pvr` ABI

`PVR2_DC_IOC_INFO` returns the physical bases/sizes of the three windows; mmap
each with `offset = <phys base>`.  `PVR2_DC_IOC_WAIT_VSYNC` / `_WAIT_RENDER`
block until the next vertical blank / end-of-render interrupt.  Definitions are
shared with the kernel in `drivers/gpu/pvr2/pvr2_dc_abi.h` (mirrored inline in
`src/libpvr.c`).

## Known limitations / TODO (before the cube demo & GLdc)

- **Single-buffered.** pvr2fb owns scanout (`DISP_DIWADDRL`); libpvr renders
  straight into pvr2fb's live framebuffer.  True double buffering needs libpvr
  to take scanout away from pvr2fb (blank the console and own DIWADDRL, or drive
  fbdev Y-pan on a double-height virtual fb).  Until then, animation tears.
- **Textures** are wired in the API (`pvr_poly_cxt_txr`, `pvr_mem_malloc`,
  `pvr_txr_load`) but only *linear* (non-twiddled) uploads are exercised; the TA
  model fetches textures linearly.  Twiddled upload / VQ / palette are untested.
- **`pvr_set_bg_color`** is a stub — the TA model currently clears to black; add
  a background-plane path (QEMU + libpvr) to honour it.
- **Floating point:** build with the default `-m4` (hardware FP) and keep
  per-vertex float math out of tight loops.  A QEMU SH4 double-precision FPU
  quirk trips loop-carried FP register allocation (SIGILL); precomputing static
  geometry, as `tritest` does, avoids it.  `-m4-nofpu` soft-float is **broken**
  in the stock Ubuntu `sh4-linux-gnu` cross toolchain (wrong results) — do not
  use it.

## T2 integration (follow-up)

Not yet wired into the T2 userland build.  Add a `libpvr` package (T2 fetches a
tarball via `[D]`, or use the local-source mechanism `fbdoom` uses) that runs
this `Makefile` with `CROSS=$SDECFG…-`, and select it in
`userland/target/dc/pkgsel/20-base.in`.  The demo and GLdc packages then link
`-lpvr`.
