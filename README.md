# Dreamcast Linux

This is a docker environment that builds a mainline linux distro that is bootable on the Sega Dreamcast. The latest bootable CDI image can be found under the Releases.

[It is actually tested on real hardware](https://github.com/foxdrodd/dc-hacking/blob/main/linux-on-dreamcast/linux-booting-dmesg.md). 

My main testcase is via serial and NFS chroot, so it doesn't contain many useful applications by itself. This is certainly an area of improvement.
Furthermore the build-process is currently a bit sophisticated, meaning its not really reproducible right know, like the patches not applied automatically.

Tested Devices, feel free to add PR to add your test device:

| Medium | Revision                 | Works                  | Description |
| ------ | --------------------- | ---------------------- | -----------
| GDEMU  | HKT-3030, PAL E, Rev. 1 | :heavy_check_mark: Yes | framebuffer, serial console, network |
| GDEMU  | HKT-3020, NTSC U, Rev. 1 | :x: No | stays at sega screen  |
| dcload-serial  | HKT-3000, NTSC J, Ugo Denshi | :heavy_check_mark: Yes | framebuffer, serial console, (has no bba)  |
| GDROM  | HKT-3030, PAL E, Rev. 1 |  | soon to be tested |
| GDROM  | HKT-3020, NTSC U, Rev. 1 |  | soon to be tested  |
| GDROM  | HKT-3000, NTSC J, Ugo Denshi |  | soon to be tested  |
| CDI | GXemul 0.7.0+dfsg |  :x: No | `[ exception 0x160, pc=0x8c4abede vaddr=0x00000000  ]` see [gxemul-analyze.txt](debuggings/gxemul-analyze.txt) |
| CDI | lxdream 0.9.1 |  :x: No | stays at sega screen, with memset/memcpy sh4-specifics removed, boots to console, but no interaction possible see [lxdream-analyze.txt](debuggings/lxdream-analyze.txt) |
| CDI | flycast 2.6 |  :x: No | stays at sega screen, see [flycast-analyze.txt](debuggings/flycast-analyze.txt)   |
| CDI | redream 1.5.0 |  :x: No | [redream-analyze.txt](debuggings/redream-analyze.txt) |


**ATTENTION:** You need to customize the `.dreamcast/src/linux-xx/arch/sh/Kconfig` to modify BOOT_LINK_OFFSET, aligned to `.dreamcast/src/sh-boot/tools/dreamcast/kernel-boot.S` `L_binary_zImage_bin_dest:` and `L_entry:`. (The memory position of the zipped kernel image, depending on how big it is). In the example below, we chose 0x00600000 / 0xac600000.

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

## Apply patches

```
dreamcast-linux/.dreamcast/src/linux-6.19.9 ❯ patch -p1 < ../../../hotfix-dreamcast-*.patch
dreamcast-linux/.dreamcast/src/linux-6.19.9 ❯ patch -p1 < ../../../hotfix-vmu*.patch
```

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
* https://asciinema.org/a/722003?t=5

## License

This source is licensed under MIT. See attached licenses for third party libraries included for more information.
