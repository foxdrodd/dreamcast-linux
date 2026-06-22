#!/bin/bash

# Call it from the repository root.

# Paths to the built userlands:
declare -A userland=(
	[uclibc]=/media/flo/nvme0-ssd/shuclibc-711
	[musl]=/media/flo/nvme0-ssd/shmusl-711
)

linux_version=$(cat dreamcast/build-dreamcast.sh|grep '^LINUX_VERSION=.*'|cut -d"=" -f2|tr -d '"')
reldir=release-${linux_version}

# Update Kernel Image to get the 1ST_READ.BIN
#

# First build the busybox variant:
sed -i 's,CONFIG_INITRAMFS_SOURCE=.*,CONFIG_INITRAMFS_SOURCE="/usr/src/dreamcast/initrd",' dreamcast/kernel.config
./rebuildkernel.sh
cd build
mkdir -p $reldir
zstd -f linux616.cdi -o $reldir/linux-$linux_version-base-busybox.cdi.zst &
cp -vf linux616.cdi linux-$linux_version-base-busybox.cdi

zstd -f 1ST_READ.BIN -o $reldir/1ST_READ.BIN.zst &
cp -vf 1ST_READ.BIN 1ST_READ.BIN-busybox

zstd -f kernel-boot.bin -o $reldir/kernel-boot.bin.zst &
cp -vf kernel-boot.bin kernel-boot.bin-busybox
cd ..

wait

# then build the userland variants with tiny-initrd:
sed -i 's,CONFIG_INITRAMFS_SOURCE=.*,CONFIG_INITRAMFS_SOURCE="/usr/src/dreamcast/tiny-initrd",' dreamcast/kernel.config
./rebuildkernel.sh

cd build
mkdir -p $reldir

for i in "${!userland[@]}"; do
	sed -i 's/PRETTY.*/PRETTY_NAME="Dreamcast Linux ('$(date +%Y-%m-%d)')"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION=.*/VERSION="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	sed -i 's/VERSION_ID=.*/VERSION_ID="'$(date +%Y-%m-%d)'"/' ${userland[$i]}/etc/os-release
	echo dreamcast > ${userland[$i]}/etc/hostname
	sed -i '/# nameserver 1\.1/ s/^..//' ${userland[$i]}/etc/resolv.conf

        echo PS1=\"root@dreamcast:\\\$PWD\\$ \" > ${userland[$i]}/etc/profile
        echo "hostname dreamcast" >> ${userland[$i]}/etc/profile
        echo flashfetch >> ${userland[$i]}/etc/profile

	cp -vf ../userland/package/fbdoom/*.wad ${userland[$i]}/

	iso=linux-$linux_version-with-userland-$i
	genisoimage -l -r -C 0,11702 -G IP.BIN -o $iso.iso 1ST_READ.BIN ${userland[$i]}
	/opt/toolchains/dc/bin/cdi4dc $iso.iso $iso.cdi
	zstd -f $iso.cdi -o $reldir/$iso.cdi.zst &
	usrland=linux-$linux_version-userland-$i
	tar --zstd -cf $reldir/$usrland.tar.zst -C ${userland[$i]} . &
done

# zstd -f linux616.cdi -o $reldir/linux-$linux_version-base-busybox.cdi.zst &
# zstd -f 1ST_READ.BIN -o $reldir/1ST_READ.BIN.zst &
# zstd -f kernel-boot.bin -o $reldir/kernel-boot.bin.zst &

wait

cd ..
