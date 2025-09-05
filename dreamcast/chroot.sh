# This script mounts the nfsroot and chroots into it
# supposes that you start in a quite empty busybox.

mkdir /mnt
mount -t nfs -o port=2049,nolock,proto=tcp 192.168.0.225:/home/flo/devel/t2-hacking/uclibc /mnt

losetup /dev/loop0 /mnt/swapfile
mkswap /dev/loop0
swapon /dev/loop0

cd /mnt/shmusl
mount -o bind /dev dev
mount -o bind /proc proc
mount -o bind /sys sys
mkdir /dev/pts
mount -vt devpts devpts /dev/pts
mount -o bind /dev/pts/ dev/pts

chroot . /bin/bash

