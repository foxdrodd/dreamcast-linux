# On Release Tests:

# Booting:
# Does kernel-boot.bin boot via dcload-serial/gdemu?
# Does busybox-variant boot via dcload-serial/gdemu?
# Do all CDI variants boot (uclibc, musl, busybox)
# Does it burn to CDR 700 MB and boot?

# Tools:
# Networking: Mount NFS
# Networking: swap over NFS
# Networking: iperf3, netcat
# htop, btop
# ircii starts
# fbdoom starts and works in uclibc/musl
# ssh, telnet starts
# fastfetch with custom logo
# GDROM access works

# VMU:
# VMU: Does hotplugging memcards work after boot?
# VMU: mtd access, info, raw read, raw write works
# VMU: mkfs.vmufat / mount -t vmufat and write files, umount / mount and access
# VMU: Default Logo is shown on LCD
# VMU: text2lcd works

# Consoles:
# Video mode selection works, something is displayed on the screen, not only serial
# tiny-initrd works 
# Switching consoles work
# Does it start a tty on serial and console
# Does it start a tty, when serial is not plugged in
#
