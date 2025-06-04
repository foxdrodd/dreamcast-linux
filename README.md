# Dreamcast Linux

This is a docker environment that builds a mainline linux distro that is bootable on the Sega Dreamcast.

[It is actually tested on real hardware](https://github.com/foxdrodd/dc-hacking/blob/main/linux-on-dreamcast/linux-booting-dmesg.md). Unfortunately it does not work in gxemul. (`[ exception 0x160, pc=0x8c4abede vaddr=0x00000000  ]`)

[![asciicast](https://asciinema.org/a/722003.svg?t=6.5)](https://asciinema.org/a/722003?t=6.5)

ATTENTION: You need to customize the `.dreamcast/src/linux-xx/arch/sh/Kconfig` to modify BOOT_LINK_OFFSET, aligned to `.dreamcast/src/sh-boot/tools/dreamcast/kernel-boot.S` `L_binary_zImage_bin_dest:` and `L_entry:`. (The memory position of the zipped kernel image, depending on how big it is). In the example below, we chose 0x00600000 / 0xac800000.

```
config BOOT_LINK_OFFSET
        hex
        default "0x00210000" if SH_SHMIN
        default "0x00600000" if SH_DREAMCAST
        default "0x00810000" if SH_7780_SOLUTION_ENGINE
        default "0x009e0000" if SH_TITAN
        default "0x01800000" if SH_SDK7780
        default "0x02000000" if SH_EDOSK7760
        default "0x00800000"
        help
          This option allows you to set the link address offset of the zImage.
          This can be useful if you are on a board which has a small amount of
          memory.

```

I use kernel-integrated initramfs and not the sh-boot initrd. The path to the initramfs is defined in the kernel-config. (`CONFIG_INITRAMFS_SOURCE="/usr/src/dreamcast/initrd"
`)

## About

Creates a cross-compilation environment for the SH4 architecture using:

* gcc
* glibc
* linux
* binutils
* busybox

## Requirements

Docker on Linux.

## Usage

> Please inspect the local variables in the `dreamcast/build-dreamcast.sh` script before running this. Some options might not fit your setup.

```
make
```

You should now have the final images in `build/` folder.

All building happens in `.dreamcast/`.

## Links

* https://github.com/foxdrodd/dc-hacking
* http://linuxdevices.org/running-linux-on-the-sega-dreamcast-a/
* https://github.com/foxdrodd/sh-boot/

## License

This source is licensed under MIT. See attached licenses for third party libraries included for more information.
