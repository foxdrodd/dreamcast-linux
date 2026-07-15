# gldc - fixed-function OpenGL 1.x (GLdc) demos over libpvr / /dev/pvr
# SPDX-License-Identifier: MIT
#
# The GLdc *core* is a third-party clone; the Dreamcast-Linux platform port
# (GL/platforms/libpvr.{h,c}, libpvr_mat.S, Makefile.libpvr) is our work and is
# snapshotted read-only under ./port/ -- the build source of truth is the clone
# at $(GLDC).  See README.md.
#
#   make GLDC=/path/to/GLdc                 # build the demos (static)
#   make GLDC=/path/to/GLdc libgldc         # (re)build GLdc's libpvr platform lib first

CROSS   ?= sh4-linux-gnu-
CC      := $(CROSS)gcc

GLDC    ?= /home/flo/devel/GLdc
LIBPVR  ?= ../libpvr

GLDC_LIB := $(GLDC)/build-libpvr/libGLdc.a
LIBPVR_A := $(LIBPVR)/libpvr.a

# Float-only + double-FP -m4 ABI (no -m4-single on this toolchain).  FP precision
# mode (FPSCR.PR) is handled *inside* the GLdc platform asm, not here -- do NOT
# force PR globally from the demo (it corrupts GCC's float codegen).  See README.
CFLAGS  := -m4 -O2 -fsingle-precision-constant -ffast-math \
           -I$(GLDC)/include -I$(LIBPVR)/include -Idemos

DEMOS   := gldc_cube gldc_texcube gldc_torus gldc_control

all: $(DEMOS)

# Spinning NeHe-05 pyramid + cube (no textures).
gldc_cube: demos/nehe05_cube.c $(GLDC_LIB) $(LIBPVR_A)
	$(CC) $(CFLAGS) -static -o $@ $< $(GLDC_LIB) $(LIBPVR_A) -lm

# Spinning NeHe-06 texture-mapped cube (loads assets/NeHe.bmp at runtime).
gldc_texcube: demos/nehe06_texcube.c demos/loadbmp.c $(GLDC_LIB) $(LIBPVR_A)
	$(CC) $(CFLAGS) -static -o $@ demos/nehe06_texcube.c demos/loadbmp.c $(GLDC_LIB) $(LIBPVR_A) -lm

# Lit, texture-mapped spinning torus (procedural checker texture, CPU T&L +
# world-fixed directional shading; shows the full textured 3D path).
gldc_torus: demos/torus.c $(GLDC_LIB) $(LIBPVR_A)
	$(CC) $(CFLAGS) -static -o $@ $< $(GLDC_LIB) $(LIBPVR_A) -lm

# sh4zam-accelerated build of the same torus (FSCA sincos in the per-vertex
# loop) for the A/B fps comparison.  Needs the gcc-17 sh4 cross (full C23),
# SHZ_BACKEND=SHZ_SH4 forced, and NO -ffast-math (else FSCA falls back to
# __builtin).  See memory sh4zam-integration / README.
SHZ_DIR   ?= /home/flo/devel/sh4zam
SHZ_CC    ?= /media/flo/nvme0-ssd/gcc/sh4-gcc17/install/bin/sh4-linux-gnu-gcc
SHZ_SYS   ?= /usr/sh4-linux-gnu
SHZ_FLAGS := -m4 -O2 -fsingle-precision-constant -DUSE_SH4ZAM -DSHZ_BACKEND=1 \
             -isystem $(SHZ_SYS)/include -B$(SHZ_SYS)/lib -L$(SHZ_SYS)/lib \
             -I$(SHZ_DIR)/include -I$(GLDC)/include -I$(LIBPVR)/include -Idemos
gldc_torus_shz: demos/torus.c $(GLDC_LIB) $(LIBPVR_A)
	$(SHZ_CC) $(SHZ_FLAGS) -static -o $@ $< $(GLDC_LIB) $(LIBPVR_A) -lm

# Interactive controllable object (Maple controller via evdev; Component-6 demo).
gldc_control: demos/control.c $(GLDC_LIB) $(LIBPVR_A)
	$(CC) $(CFLAGS) -static -o $@ $< $(GLDC_LIB) $(LIBPVR_A) -lm

# sh4zam-accelerated interactive demo (FSCA sincos in the per-vertex loop).
gldc_control_shz: demos/control.c $(GLDC_LIB) $(LIBPVR_A)
	$(SHZ_CC) $(SHZ_FLAGS) -static -o $@ $< $(GLDC_LIB) $(LIBPVR_A) -lm

# Convenience: build GLdc's libpvr platform static lib in the clone.
libgldc:
	$(MAKE) -C $(GLDC) -f Makefile.libpvr

$(GLDC_LIB):
	@echo "missing $(GLDC_LIB) -- run: make GLDC=$(GLDC) libgldc" >&2; exit 1

clean:
	rm -f $(DEMOS)

.PHONY: all libgldc clean
