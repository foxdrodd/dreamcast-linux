# Dreamcast Linux

This is a docker environment that builds a mainline linux distro that is bootable on the Sega Dreamcast

It is actually tested on real hardware. Unfortunately it does not work in gxemul.

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

* http://linuxdevices.org/running-linux-on-the-sega-dreamcast-a/

## License

This source is licensed under MIT. See attached licenses for third party libraries included for more information.
