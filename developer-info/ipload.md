# Load via dcload-ip / ipload / 1ipload.bin

While serial takes longer to transfer the files, I hoped loading via network would be faster:

## CDI Image

https://dreamcast.wiki/File:Dcload-2023-06-22.zip

## Host Tools (from where you send the file):

https://github.com/sizious/dcload-ip

I initially thought I can just netcat to a port, but it looks like we need dcload-ip for this.

## Burn CDI on linux:

https://github.com/alex-free/dreamcast-cdi-burner

## Issues

DHCP did sometimes not work, only on second reboot. Might be related to my crap router, when I hosted dhcpd on my PC, it worked mostly.
