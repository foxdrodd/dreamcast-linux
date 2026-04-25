#!/bin/bash

# Call it from the repository root.

# Paths to the built userlands:
declare -A userland=(
	[uclibc]=/media/flo/nvme0-ssd/shuclibc-701
	[musl]=/media/flo/nvme0-ssd/shmusl-701
)

linux_version=$(cat dreamcast/build-dreamcast.sh|grep '^LINUX_VERSION=.*'|cut -d"=" -f2|tr -d '"')

# Update Kernel Image to get the 1ST_READ.BIN
./rebuildkernel.sh

cd build
reldir=release-${linux_version}
mkdir -p $reldir

for i in "${!userland[@]}"; do
	sed -i 's/PRETTY.*/PRETTY_NAME="Dreamcast Linux ('$(date +%Y-%m-%d)')"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION=.*/VERSION="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION_ID=.*/VERSION_ID="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	iso=linux-$linux_version-with-userland-$i
	genisoimage -l -r -C 0,11702 -G IP.BIN -o $iso.iso 1ST_READ.BIN ${userland[$i]}
	/opt/toolchains/dc/bin/cdi4dc $iso.iso $iso.cdi
	zstd $iso.cdi -o $reldir/$iso.cdi.zst
	usrland=linux-$linux_version-userland-$i
	tar --zstd -cf $reldir/$usrland.tar.zst -C ${userland[$i]} .
done

zstd linux616.cdi -o $reldir/linux-$linux_version-base-busybox.cdi.zst
zstd 1ST_READ.BIN -o $reldir/1ST_READ.BIN.zst
zstd kernel-boot.bin -o $reldir/kernel-boot.bin.zst

cd ..
