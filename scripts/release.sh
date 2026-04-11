#!/bin/bash

# Call it from the repository root.

# Paths to the built userlands:
declare -A userland=(
	[uclibc]=/media/flo/nvme0-ssd/shmusl-uclibc
	[musl]=/media/flo/nvme0-ssd/shmusl-small2
)

linux_version=$(cat dreamcast/build-dreamcast.sh|grep '^LINUX_VERSION=.*'|cut -d"=" -f2|tr -d '"')

# Update Kernel Image to get the 1ST_READ.BIN
./rebuildkernel.sh

cd build

for i in "${!userland[@]}"; do
	sed -i 's/PRETTY.*/PRETTY_NAME="Dreamcast Linux ('$(date +%Y-%m-%d)')"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION=.*/VERSION="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION_ID=.*/VERSION_ID="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	iso=linux-$linux_version-with-userland-$i
	genisoimage -l -r -C 0,11702 -G IP.BIN -o $iso.iso 1ST_READ.BIN ${userland[$i]}
	/opt/toolchains/dc/bin/cdi4dc $iso.iso $iso.cdi
done

cp linux616.cdi linux-$linux_version-base-busybox.cdi

cd ..
