# Makefile for the SEGA Dreamcast VMU LCD userspace tools.
#
# Builds the SH4 (Dreamcast) binaries by default. Use `make native` to build
# host binaries for testing on your workstation.

CROSS_COMPILE ?= sh4-linux-gnu-
CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -O2 -Wall -Wshadow -static
LDFLAGS =

NATIVE_CC ?= gcc

# kernel tree for nolibc (tools/include/nolibc)
LINUX_DIR ?= ../../.dreamcast/src/linux-7.3.1
NOLIBC_CFLAGS = -Os -s -static -nostdlib -fno-asynchronous-unwind-tables \
		-fno-ident -Wall -include nolibc.h \
		-I $(LINUX_DIR)/tools/include/nolibc

TOOLS = vmu_lcd_text vmu_lcd_anim vmu_lcd_t2logo vmu_lcd_status
HDRS  = vmu_lcd.h

all: $(TOOLS)

vmu_lcd_text: vmu_lcd_text.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

vmu_lcd_anim: vmu_lcd_anim.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

vmu_lcd_t2logo: vmu_lcd_t2logo.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

vmu_lcd_status: vmu_lcd_status.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Tiny static build without any libc (kernel nolibc), ~10 KB.
vmu_lcd_status_nolibc: vmu_lcd_status.c
	$(CC) $(NOLIBC_CFLAGS) -o $@ $< -lgcc

# Host builds for local testing (suffixed _native, not for the Dreamcast).
native: vmu_lcd_text_native vmu_lcd_anim_native vmu_lcd_t2logo_native \
	vmu_lcd_status_native

vmu_lcd_text_native: vmu_lcd_text.c $(HDRS)
	$(NATIVE_CC) -O2 -Wall -o $@ $<

vmu_lcd_anim_native: vmu_lcd_anim.c $(HDRS)
	$(NATIVE_CC) -O2 -Wall -o $@ $<

vmu_lcd_t2logo_native: vmu_lcd_t2logo.c $(HDRS)
	$(NATIVE_CC) -O2 -Wall -o $@ $<

vmu_lcd_status_native: vmu_lcd_status.c
	$(NATIVE_CC) -O2 -Wall -o $@ $<

clean:
	rm -f $(TOOLS) vmu_lcd_text_native vmu_lcd_anim_native \
	      vmu_lcd_t2logo_native vmu_lcd_status_native vmu_lcd_status_nolibc

.PHONY: all native clean
