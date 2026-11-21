#!/usr/bin/env python3
"""VRAM swap proof of concept: phram over PVR2 VRAM -> mtdblock -> swap.

Boots the 7.3.1 musl userland CDI with a phram-enabled vmlinux, maps
6 MB of VRAM (phys 0x05200000, 32-bit area, above the 640x480 console fb)
as an MTD, swaps on its mtdblock and checks data integrity by pushing a
tmpfs file larger than free RAM out to swap and md5-ing it twice.
"""
import re
import sys
import time

sys.path.insert(0, "/home/flo/.claude/skills/dreamcast-linux/scripts")
from dcboot import DCBoot  # noqa: E402

KERNEL, DISC, SHOT = sys.argv[1], sys.argv[2], sys.argv[3]
VRAM_START, VRAM_LEN = 0x05200000, 6 << 20


def step(dc, cmd, t=60):
    out = dc.run(cmd, timeout=t)
    print("$ " + cmd)
    print(out)
    print(flush=True)
    return out


def meminfo(dc, key):
    out = dc.run("grep %s: /proc/meminfo" % key)
    m = re.search(r"(\d+) kB", out)
    return int(m.group(1)) if m else 0


with DCBoot(kernel=KERNEL, drives=[DISC]) as dc:
    dc.boot(timeout=300)
    step(dc, "uname -a")
    step(dc, "free")
    step(dc, "cat /proc/mtd")
    step(dc, "echo vram,0x%x,0x%x > /sys/module/phram/parameters/phram"
         % (VRAM_START, VRAM_LEN))
    mtd = step(dc, "cat /proc/mtd")
    m = re.search(r"mtd(\d+):.*\"vram\"", mtd)
    if not m:
        step(dc, "dmesg | tail -5")
        sys.exit("phram device did not appear")
    dev = "/dev/mtdblock%s" % m.group(1)
    step(dc, "ls -l %s" % dev)
    step(dc, "mkswap %s" % dev)
    step(dc, "swapon -p 10 %s" % dev)
    step(dc, "cat /proc/swaps")
    step(dc, "free")

    # tmpfs pages are swappable: a file bigger than free RAM must spill.
    avail = meminfo(dc, "MemAvailable")
    blob_mb = avail // 1024 + 3
    print("MemAvailable %d kB -> blob %d MB" % (avail, blob_mb))
    step(dc, "mkdir -p /tmp/vt")
    step(dc, "mount -t tmpfs -o size=16m tmpfs /tmp/vt")
    t0 = time.time()
    step(dc, "dd if=/dev/urandom of=/tmp/vt/blob bs=64k count=%d"
         % (blob_mb * 16), t=300)
    print("write took %.1fs (host wall clock, TCG)" % (time.time() - t0))
    a = step(dc, "md5sum /tmp/vt/blob", t=300)
    step(dc, "grep -E 'pswp(in|out)' /proc/vmstat")
    step(dc, "cat /proc/swaps")
    # more pressure, so the blob's pages get evicted and read back again
    step(dc, "dd if=/dev/urandom of=/tmp/vt/blob2 bs=64k count=32", t=300)
    step(dc, "rm /tmp/vt/blob2")
    b = step(dc, "md5sum /tmp/vt/blob", t=300)
    step(dc, "grep -E 'pswp(in|out)' /proc/vmstat")
    step(dc, "free")
    step(dc, "dmesg | grep -iE 'phram|swap|oom|error|bug' | tail -15")
    dc.screendump(SHOT)

    ha = re.search(r"([0-9a-f]{32})", a)
    hb = re.search(r"([0-9a-f]{32})", b)
    ok = ha and hb and ha.group(1) == hb.group(1)
    print("INTEGRITY:", "PASS" if ok else "FAIL", ha and ha.group(1),
          hb and hb.group(1))

    step(dc, "rm /tmp/vt/blob")
    step(dc, "swapoff %s" % dev)
    step(dc, "cat /proc/swaps")
    dc.quit()
    sys.exit(0 if ok else 1)
