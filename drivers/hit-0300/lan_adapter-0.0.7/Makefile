# Makefile for lan_adapter.c
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

CC = sh4-linux-gcc
CFLAGS_EXTRA = -DMODULE -DLINUX -D__KERNEL__ -I /usr/local/sh4-linux
OPTIMIZATION = -O2
WARNINGS = -Wall -Wshadow
CFLAGS = $(WARNINGS) $(OPTIMIZATION) $(CFLAGS_EXTRA)

.EXPORT_ALL_VARIABLES:

all: lan_adapter.o

lan_adapter.o: lan_adapter.c

clean:
	- rm -f lan_adapter.o module/lan_adapter.o core

install:

	- cp -f lan_adapter.o $(INITRD)/lib/modules
