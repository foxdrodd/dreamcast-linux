"""Guest-side helper scripts shared by the VMU LCD + vmufat tests.

Used by tests/vmu_lcd_vmufat_test.py (QEMU) and
tests/harness/vmu-lcd-vmufat-hw.py (real Dreamcast over serial). Each
script is written to the guest as /tmp/<name> and run with mksh.
"""

SCRIPTS = {
    # stop a vmu_lcd_status that init may have started
    "killvls.sh": r"""
for p in /proc/[0-9]*; do
  [ "$(cat $p/comm 2>/dev/null)" = vmu_lcd_status ] && kill ${p#/proc/}
done
for p in /proc/[0-9]*; do
  [ "$(cat $p/comm 2>/dev/null)" = vls ] && kill ${p#/proc/}
done
true
""",
    # is a process with this name ($1, default vls) running?
    "alive.sh": r"""
for p in /proc/[0-9]*; do
  [ "$(cat $p/comm 2>/dev/null)" = "${1:-vls}" ] && echo ALIVE
done
true
""",
    # LCD hammer: alternate two frames until /tmp/stop exists
    "hammer.sh": r"""
ok=0; fail=0
while [ ! -e /tmp/stop ]; do
  for f in /tmp/a.raw /tmp/b.raw; do
    if dd if=$f of=/dev/vmu_lcd0 bs=192 2>/dev/null; then
      ok=$((ok+1))
    else
      fail=$((fail+1))
    fi
  done
done
echo "HAMMER ok=$ok fail=$fail" > /tmp/hammer.out
""",
    # one filesystem round: write, sync, verify; remount, verify again
    "fsround.sh": r"""
tag=$1
rm -rf /tmp/src; mkdir -p /tmp/src
for s in 1 2 3 5 8; do
  dd if=/dev/urandom of=/tmp/src/${tag}k$s bs=1k count=$s 2>/dev/null
done
rm -f /mnt/vmu/*
for f in /tmp/src/*; do
  cp $f /mnt/vmu/ || echo "CPFAIL ${f##*/}"
done
sync
echo 3 > /proc/sys/vm/drop_caches
for f in /tmp/src/*; do
  b=${f##*/}
  if [ "$(md5sum < $f)" = "$(md5sum < /mnt/vmu/$b 2>/dev/null)" ]; then
    echo "OK1 $b"
  else
    echo "BAD1 $b"
  fi
done
umount /mnt/vmu || echo UMOUNTFAIL
echo 3 > /proc/sys/vm/drop_caches
mount -t vmufat /dev/mtdblock0 /mnt/vmu || echo MOUNTFAIL
for f in /tmp/src/*; do
  b=${f##*/}
  if [ "$(md5sum < $f)" = "$(md5sum < /mnt/vmu/$b 2>/dev/null)" ]; then
    echo "OK2 $b"
  else
    echo "BAD2 $b"
  fi
done
echo ROUNDDONE
""",
}

# start background writers with their output away from the console
SCRIPTS["start_status.sh"] = """
/tmp/vls -t 1 > /dev/null 2>&1 &
"""
SCRIPTS["start_hammer.sh"] = """
rm -f /tmp/stop /tmp/hammer.out
mksh /tmp/hammer.sh > /dev/null 2>&1 &
"""

# kernel log lines that mean trouble
BAD_DMESG = (r"I/O error|EIO|timed? ?out|timeout|stale|corrupt|Oops|BUG|"
             r"WARNING|Call Trace|deadlock|no response|failed")


def write_scripts(directory):
    import os
    for name, body in SCRIPTS.items():
        with open(os.path.join(directory, name), "w") as f:
            f.write("#!/bin/mksh\n" + body.lstrip())
