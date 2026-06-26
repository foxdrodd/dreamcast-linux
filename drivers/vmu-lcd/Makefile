# Makefile for the SEGA Dreamcast VMU LCD userspace tools.
#
# Builds the SH4 (Dreamcast) binaries by default. Use `make native` to build
# host binaries for testing on your workstation.

CROSS_COMPILE ?= sh4-linux-gnu-
CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -O2 -Wall -Wshadow -static
LDFLAGS =

NATIVE_CC ?= gcc

TOOLS = vmu_lcd_text vmu_lcd_anim
HDRS  = vmu_lcd.h

all: $(TOOLS)

vmu_lcd_text: vmu_lcd_text.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

vmu_lcd_anim: vmu_lcd_anim.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Host builds for local testing (suffixed _native, not for the Dreamcast).
native: vmu_lcd_text_native vmu_lcd_anim_native

vmu_lcd_text_native: vmu_lcd_text.c $(HDRS)
	$(NATIVE_CC) -O2 -Wall -o $@ $<

vmu_lcd_anim_native: vmu_lcd_anim.c $(HDRS)
	$(NATIVE_CC) -O2 -Wall -o $@ $<

clean:
	rm -f $(TOOLS) vmu_lcd_text_native vmu_lcd_anim_native

.PHONY: all native clean
