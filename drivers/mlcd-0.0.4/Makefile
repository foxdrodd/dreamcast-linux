# Makefile for SEGA VMU LCD Display driver package.
#
# Copyright (C) 2002/2003 Christian Berger <c.berger@tu-braunschweig.de>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#

PREFIX = /usr/local
INITRD = 
CROSS = sh4-linux-
NATIVE_CC = gcc
CC = $(CROSS)$(NATIVE_CC)
CFLAGS_EXTRA = -DMODULE -DLINUX -D__KERNEL__ -I $(PREFIX)/sh4-linux
OPTIMIZATION = -O2
WARNINGS = -Wall -Wshadow -Wunused
CFLAGS = $(WARNINGS) $(OPTIMIZATION) $(CFLAGS_EXTRA)

UCLIBC_CC = $(PREFIX)/sh-linux-uclibc/bin/sh-uclibc-gcc
UCLIBC_STRIP = $(PREFIX)/sh-linux-uclibc/bin/sh-uclibc-strip
UCLIBC_WARNINGS = -Wall -Wshadow -Wunused -ansi

JAVAC = javac
# For example: JAVAC_OPTIMIZATION = -g for debugging purposes.
JAVAC_OPTIMIZATION =
JAVAC_FLAGS_EXTRA = 
JAVAC_WARNINGS = -nowarn
JAVAC_FLAGS = $(JAVAC_WARNINGS) $(JAVAC_OPTIMIZATION) $(JAVAC_FLAGS_EXTRA)

.EXPORT_ALL_VARIABLES:

all: mlcd.o text2lcd

all_with_emulator: all SEGA_VMU_Display.class BitBlock2Hex.class text2lcd_native

# This target produces the virtual SEGA VMU LCD Display.
SEGA_VMU_Display.class: SEGA_VMU_Display.java
	- rm -f SEGA_VMU_Display*class
	$(JAVAC) $(JAVAC_FLAGS) SEGA_VMU_Display.java

# This target produces a tool for creating a bitmapped font from a given character representation.
BitBlock2Hex.class: BitBlock2Hex.java
	- rm -f BitBlock2Hex*class
	$(JAVAC) $(JAVAC_FLAGS) BitBlock2Hex.java

# This target produces the SEGA VMU LCD Display driver.
mlcd.o: mlcd.c

# This target produces the SEGA VMU LCD Display userspace tool for transforming a string into a bitmapped font.
text2lcd: text2lcd.c
	- $(UCLIBC_CC) $(UCLIBC_WARNINGS) -o text2lcd text2lcd.c && $(UCLIBC_STRIP) text2lcd

# This target produces the SEGA VMU LCD Display userspace tool for transforming a string into a bitmapped font, native version.
text2lcd_native: text2lcd.c
	- $(NATIVE_CC) $(UCLIBC_WARNINGS) -o text2lcd_native text2lcd.c

clean:
	- rm -f mlcd.o text2lcd text2lcd_native core
	- rm -f *class

install: all
	- cp -f mlcd.o $(INITRD)/lib/modules/misc
	- cp -f text2lcd $(INITRD)/usr/bin
