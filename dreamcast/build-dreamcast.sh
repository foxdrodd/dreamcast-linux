#!/bin/bash
#
# Dreamcast Linux build script
# Author: Anders Evenrud <andersevenrud@gmail.com>
# Customized: Florian Fuchs <ffuchs@linuxdc.net>
#
set -e

# FIXME: Optimize the staging of GCC and libc. This can be done with newlib probably.
# TODO: Patch TTYs in busybox
# TODO: Create a new version of the roaster that creates ISO, not burn directly
# TODO: More variables for paths

# Customize these to your liking
GNU_MIRROR="https://gnuftp.uib.no"
MY_COPTS="-j24"

# Dependencies
BINUTILS_VERSION="2.32"
GCC_VERSION="8.3.0"
GDB_VERSION="8.3"
GLIBC_VERSION="2.30"
#LINUX_VERSION="5.2"
LINUX_VERSION_1="6.x"
LINUX_VERSION="6.16.9"
BUSYBOX_VERSION="1.31.0"

# Globals
export TARGET="sh4-linux"
export PREFIX="/opt/dreamcast"
export PATH="${PATH}:${PREFIX}/bin"
export INITRD=/usr/src/dreamcast/initrd

# Preparations
mkdir -p /opt/build

# Build
pushd dreamcast

  #
  # Sources
  #

  if [ ! -f "binutils-${BINUTILS_VERSION}.tar.xz" ]; then
    wget ${GNU_MIRROR}/binutils/binutils-${BINUTILS_VERSION}.tar.xz
    tar xJf binutils-${BINUTILS_VERSION}.tar.xz
  fi

  if [ ! -f "gcc-${GCC_VERSION}.tar.xz" ]; then
    wget ${GNU_MIRROR}/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz
    tar xJf gcc-${GCC_VERSION}.tar.xz
  fi

  if [ ! -f "gdb-${GDB_VERSION}.tar.xz" ]; then
    wget ${GNU_MIRROR}/gdb/gdb-${GDB_VERSION}.tar.xz
    tar xJf gdb-${GDB_VERSION}.tar.xz
  fi

  if [ ! -f "glibc-${GLIBC_VERSION}.tar.xz" ]; then
    wget ${GNU_MIRROR}/glibc/glibc-${GLIBC_VERSION}.tar.xz
    tar xJf glibc-${GLIBC_VERSION}.tar.xz
  fi

  if [ ! -f "linux-${LINUX_VERSION}.tar.xz" ]; then
    wget https://cdn.kernel.org/pub/linux/kernel/v${LINUX_VERSION_1}/linux-${LINUX_VERSION}.tar.xz
    tar xf linux-${LINUX_VERSION}.tar.xz
  fi

  if [ ! -f "busybox-${BUSYBOX_VERSION}.tar.bz2" ]; then
    wget https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2
    tar xjf busybox-${BUSYBOX_VERSION}.tar.bz2
  fi

  if [ ! -d "sh-boot" ]; then
    tar xzf ../sh-boot-20010831-1455.tar.gz
    patch -p0 < ../sh-boot-20010831-1455.diff
    patch -p0 < ../sh-boot-20010831-1455-sh4.diff
  fi

  #
  # Preparations
  #

  mkdir -p build-binutils build-gcc build-glibc build-gdb initrd

  #
  # Binutils
  #

  if [ ! -f "/opt/build/dont_binutils" ]; then
  pushd build-binutils
    ../binutils-${BINUTILS_VERSION}/configure \
      --target=$TARGET \
      --prefix=$PREFIX
    make ${MY_COPTS}
    make install
  popd
  fi

  #
  # Kernel headers
  #

  if [ ! -f "/opt/build/dont_linux" ]; then
  pushd linux-${LINUX_VERSION}
    cp ../../kernel.config .config
    make ARCH=sh CROSS_COMPILE=sh4-linux- headers_install
    if [ ! -d "${PREFIX}/${TARGET}/include" ]; then
      mkdir -p ${PREFIX}/${TARGET}/include
      cp -r usr/include/* ${PREFIX}/${TARGET}/include
    fi
  popd
  fi

  #
  # GCC stage 1
  #

  if [ ! -f "/opt/build/dont_gcc" ]; then
  pushd build-gcc
    ../gcc-${GCC_VERSION}/configure \
      --target=$TARGET \
      --prefix=$PREFIX \
      --with-multilib-list=m4,m4-nofpu \
      --enable-languages=c,c++
    make ${MY_COPTS} all-gcc
    make install-gcc
  popd
  fi

  #
  # Glibc
  #
  #

  if [ ! -f "/opt/build/dont_glibc" ]; then
  pushd build-glibc
    ../glibc-${GLIBC_VERSION}/configure \
      --host=$TARGET \
      --prefix=${PREFIX}/${TARGET} \
      --disable-debug \
      --disable-profile \
      --disable-sanity-checks \
      --build=$MACHTYPE \
      --with-headers=${PREFIX}/${TARGET}/include

    make install-bootstrap-headers=yes install-headers

    make csu/subdir_lib
    install csu/crt1.o csu/crti.o csu/crtn.o ${PREFIX}/${TARGET}/lib
    sh4-linux-gcc -nostdlib -nostartfiles -shared -x c /dev/null -o ${PREFIX}/${TARGET}/lib/libc.so
    mkdir -p ${PREFIX}/${TARGET}/include/gnu
    touch ${PREFIX}/${TARGET}/include/gnu/stubs.h
  popd
  fi

  if [ ! -f "/opt/build/dont_gcc" ]; then
  pushd build-gcc
    make ${MY_COPTS} all-target-libgcc
    make install-target-libgcc
  popd

  pushd build-glibc
    make ${MY_COPTS}
    make install
  popd

  #
  # GCC stage 2
  #

  pushd build-gcc
    make ${MY_COPTS} all
    make install
  popd

  pushd build-gdb
    ../gdb-${GDB_VERSION}/configure \
      --target=$TARGET \
      --prefix=$PREFIX
    make ${MY_COPTS}
    make install
  popd
  fi

  #
  # Busybox
  #

  if [ ! -f "/opt/build/dont_busybox" ]; then

  pushd busybox-${BUSYBOX_VERSION}
    cp ../../busybox.config .config

    make CROSS=sh4-linux- \
      DOSTATIC=true \
      CFLAGS_EXTRA="-I ${PREFIX}/${TARGET}/include" \
      CONFIG_PREFIX=${INITRD} \
      all install
  popd
  fi

  #
  # Linux ramdisk
  #
  if [ ! -f "/opt/build/dont_ramdisk" ]; then


  cp -vf ../chroot.sh ${INITRD}/bin/

  if [ ! -d "${INITRD}/dev" ]; then
    mkdir -p ${INITRD}/dev
    mknod ${INITRD}/dev/console c 5 1
  fi

  mkdir -p ${INITRD}/{etc/init.d,proc,sys}
  cat <<EOF > ${INITRD}/etc/fstab
devtmpfs /dev devtmpfs rw,nosuid,mode=755 0 0
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
sysfs /sys sysfs ro,nosuid,nodev,noexec,relatime 0 0
EOF

  cat <<EOF > ${INITRD}/etc/init.d/rcS
/bin/mount -a
EOF

  chmod a+x ${INITRD}/etc/init.d/rcS

    dd if=/dev/zero of=initrd.img bs=1k count=3000
    mke2fs -F -vm0 initrd.img
    mkdir -p initrd.dir
    mount -o loop initrd.img initrd.dir
#    (cd initrd ; tar cf - .) | (cd initrd.dir ; tar xvf -)
  
    cd initrd;  rm -f init; ln -s bin/busybox init; cd ..

    cp -r initrd/* initrd.dir/
    umount initrd.dir
    gzip -c -9 initrd.img > initrd.bin
  fi

  #
  # Linux kernel
  #

 if [ ! -f "/opt/build/dont_linux" ]; then

  pushd linux-${LINUX_VERSION}
#    make ARCH=sh CROSS_COMPILE=sh4-linux- clean zImage
     make ARCH=sh CROSS_COMPILE=sh4-linux- zImage
  popd
 fi

  #
  # Boot images
  #

  pushd sh-boot/tools/dreamcast
    cp ../../../linux-${LINUX_VERSION}/arch/sh/boot/zImage ./zImage.bin
    cp ../../../initrd.bin .
    make clean scramble kernel-boot.bin

    cp kernel-boot.bin /opt/build/
    cp IP.BIN /opt/build/
    cp scramble /opt/build/
  popd

  #
  # Finalize
  #
  pushd /opt/build

    ./scramble kernel-boot.bin 1ST_READ.BIN || exit 1
# cp kernel-boot.bin 1ST_READ.BIN # try without scrambling

    dd of=audio.raw if=/dev/zero bs=2352 count=300
    genisoimage -l -r -C 0,11702 -G IP.BIN -o data.iso 1ST_READ.BIN
  popd
popd
