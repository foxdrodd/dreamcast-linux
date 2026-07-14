# libpvr - PowerVR2 userland rendering over /dev/pvr
# SPDX-License-Identifier: MIT
#
# Cross build:  make CROSS=sh4-linux-gnu-
# Install:      make install DESTDIR=<sysroot> PREFIX=/usr

CROSS   ?=
CC      := $(CROSS)gcc
AR      := $(CROSS)ar
CFLAGS  ?= -O2 -Wall -Iinclude
PREFIX  ?= /usr

all: libpvr.a examples/tritest

libpvr.a: src/libpvr.o
	$(AR) rcs $@ $^

src/libpvr.o: src/libpvr.c include/libpvr.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Static example for the initramfs dev loop.
examples/tritest: examples/tritest.c libpvr.a
	$(CC) $(CFLAGS) -static -o $@ $< libpvr.a

install: libpvr.a
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	install -m644 libpvr.a $(DESTDIR)$(PREFIX)/lib/
	install -m644 include/libpvr.h $(DESTDIR)$(PREFIX)/include/

clean:
	rm -f src/libpvr.o libpvr.a examples/tritest

.PHONY: all install clean
