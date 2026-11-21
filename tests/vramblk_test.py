#!/usr/bin/env python3
"""vramblk test in QEMU: raw integrity across access methods, then swap.

usage: vramblk_test.py <vmlinux> <userland.cdi>

1. For every (write method, read method) pair: write a 1 MiB random pattern
   into all six 1 MiB slots of /dev/vramblk0, sync and drop the page cache,
   and read each slot back, so a write with one method is always checked
   through the other.  Buffered I/O on purpose: O_DIRECT from a user buffer
   on SH4 can hand the driver stale data (fast GUP skips flush_anon_page,
   and the D-cache aliases), which looks like device corruption.
2. Swap on /dev/vramblk0 with the default methods (w0, r1): a tmpfs file larger than
   free RAM, md5 twice.
"""
import re
import sys

sys.path.insert(0, "/home/flo/.claude/skills/dreamcast-linux/scripts")
from dcboot import DCBoot  # noqa: E402

KERNEL, DISC = sys.argv[1], sys.argv[2]
P = "/sys/module/vramblk/parameters"
DEV = "/dev/vramblk0"
fails = []


def sh(dc, cmd, t=120, show=True):
    out = dc.run(cmd, timeout=t)
    if show:
        print("$ " + cmd)
        print(out, flush=True)
    return out


def md5(s):
    m = re.search(r"\b([0-9a-f]{32})\b", s)
    return m.group(1) if m else None


with DCBoot(kernel=KERNEL, drives=[DISC]) as dc:
    dc.boot(timeout=300)
    sh(dc, "uname -a")
    sh(dc, "dmesg | grep vramblk")
    sh(dc, "ls -l %s" % DEV)
    sh(dc, "cat /sys/block/vramblk0/size")
    sh(dc, "mkdir -p /tmp/vt")
    sh(dc, "mount -t tmpfs -o size=3m tmpfs /tmp/vt")

    for w, r in ((0, 0), (1, 1), (0, 1), (1, 0)):
        sh(dc, "echo %d > %s/wmode" % (w, P), show=False)
        sh(dc, "echo 0 > %s/rmode" % P, show=False)
        sh(dc, "dd if=/dev/urandom of=/tmp/vt/pat bs=64k count=16",
           show=False)
        want = md5(sh(dc, "md5sum /tmp/vt/pat", show=False))
        for i in range(6):
            sh(dc, "dd if=/tmp/vt/pat of=%s bs=64k seek=%d conv=notrunc"
               % (DEV, i * 16), show=False)
        sh(dc, "sync", show=False)
        sh(dc, "echo 3 > /proc/sys/vm/drop_caches", show=False)
        sh(dc, "echo %d > %s/rmode" % (r, P), show=False)
        got = []
        for i in range(6):
            sh(dc, "dd if=%s of=/tmp/vt/rd bs=64k skip=%d count=16"
               % (DEV, i * 16), show=False)
            got.append(md5(sh(dc, "md5sum /tmp/vt/rd", show=False)))
        ok = all(g == want for g in got)
        print("wmode=%d rmode=%d pattern %s: %s" %
              (w, r, want, "PASS" if ok else "FAIL %s" % got), flush=True)
        if not ok:
            fails.append("raw w%d r%d" % (w, r))

    sh(dc, "umount /tmp/vt")
    sh(dc, "echo 0 > %s/wmode" % P)
    sh(dc, "echo 1 > %s/rmode" % P)
    sh(dc, "mkswap %s" % DEV)
    sh(dc, "swapon -p 10 %s" % DEV)
    sh(dc, "cat /proc/swaps")
    m = re.search(r"(\d+) kB", sh(dc, "grep MemAvailable: /proc/meminfo"))
    blob = int(m.group(1)) // 1024 + 3
    sh(dc, "mount -t tmpfs -o size=16m tmpfs /tmp/vt")
    sh(dc, "dd if=/dev/urandom of=/tmp/vt/blob bs=64k count=%d" % (blob * 16),
       t=300)
    a = md5(sh(dc, "md5sum /tmp/vt/blob", t=300))
    sh(dc, "grep -E 'pswp(in|out)' /proc/vmstat")
    sh(dc, "dd if=/dev/urandom of=/tmp/vt/blob2 bs=64k count=32", t=300)
    sh(dc, "rm /tmp/vt/blob2")
    b = md5(sh(dc, "md5sum /tmp/vt/blob", t=300))
    sh(dc, "grep -E 'pswp(in|out)' /proc/vmstat")
    sh(dc, "cat /proc/swaps")
    ok = a and a == b
    print("swap (default methods), %d MB blob: %s %s %s" %
          (blob, "PASS" if ok else "FAIL", a, b))
    if not ok:
        fails.append("swap")
    sh(dc, "rm /tmp/vt/blob")
    sh(dc, "swapoff %s" % DEV)
    sh(dc, "dmesg | grep -iE 'vramblk|oops|bug|error' | tail")
    dc.quit()

print("RESULT:", "PASS" if not fails else "FAIL " + ", ".join(fails))
sys.exit(1 if fails else 0)
