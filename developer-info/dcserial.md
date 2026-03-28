# Howto test via dcserial

On devices, where I don't have a gdemu mod, I just want to throw in a CD with dcserial loader and push over my kernel, yes it takes a while, but it works in a "virgin" dc.

## dcload-serial CDI

https://github.com/KallistiOS/dcload-serial

## Host Tool

Transfer via serial takes about 3-4 minutes for 5MB:

```
/opt/toolchains/dc/bin/dc-tool-ser -x build/kernel-boot.bin -b 125200 
```

