#!/usr/bin/env python3
"""VMU LCD + vmufat on the same VMU: look for maple bus conflicts in QEMU.

The VMU LCD and the VMU flash are two functions of the same maple device,
so LCD block writes and filesystem block reads/writes share one queue and
one DMA. This test runs the same vmufat workload three times:

  1. baseline  - no LCD traffic
  2. status    - vmu_lcd_status -t 1 running (the real tool)
  3. hammer    - vmu_lcd_status plus a loop writing frames to the LCD as
                 fast as it can (two writers on one LCD)

Each round writes random files to the mounted card, syncs, drops caches and
verifies them, then unmounts, drops caches, remounts and verifies again, so
the second check really reads back over the maple bus.

Pass criteria: every file verifies in every round, no new kernel messages
that look like trouble (I/O errors, timeouts, stale replies, oops/BUG/WARN),
the LCD hammer gets frames through, and afterwards a known frame written to
the LCD shows up pixel-exact in the QEMU screendump.

    vmu_lcd_vmufat_test.py [--cdi CDI] [--kernel VMLINUX] [--bin BINARY]
                           [--out DIR] [--hammer-secs N]
"""
import argparse
import functools
import http.server
import os
import re
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/home/flo/.claude/skills/dreamcast-linux/scripts")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dcboot import DCBoot  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--cdi", default=os.path.join(
    ROOT, "build/linux-7.3.1-with-userland-muslX.cdi"))
ap.add_argument("--kernel", default=None,
                help="boot this vmlinux instead of the CDI's 1ST_READ.BIN")
ap.add_argument("--bin", default=os.path.join(
    ROOT, "drivers/vmu-lcd/vmu_lcd_status_nolibc"))
ap.add_argument("--native", default=os.path.join(
    ROOT, "drivers/vmu-lcd/vmu_lcd_status_native"),
    help="host build, renders the expected reference frame")
ap.add_argument("--out", default=None)
args = ap.parse_args()

out = args.out or tempfile.mkdtemp(prefix="vmu-lcd-vmufat-")
os.makedirs(out, exist_ok=True)
serve = os.path.join(out, "srv")
os.makedirs(serve, exist_ok=True)

# ---------------------------------------------------------------- guest files
subprocess.run(["cp", args.bin, os.path.join(serve, "vls")], check=True)

from vmu_lcd_vmufat_guest import SCRIPTS, BAD_DMESG as _BAD, write_scripts  # noqa: E402

write_scripts(serve)

vmu = os.path.join(out, "vmu.bin")
with open(vmu, "wb") as f:
    f.write(b"\0" * 131072)

handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                            directory=serve)
handler.log_message = lambda *a: None
httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
port = httpd.server_address[1]
threading.Thread(target=httpd.serve_forever, daemon=True).start()

# ---------------------------------------------------------------- helpers
results = []          # (name, ok, detail)


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print("%s  %s  %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def run(dc, cmd, t=60, quiet=False):
    o = dc.run(cmd, timeout=t)
    if not quiet:
        print("$ %s\n%s" % (cmd, o), flush=True)
    return o


def ppm_bits(path):
    """QEMU VMU screendump (48x32 cells, scaled) -> list of 0/1 rows."""
    with open(path, "rb") as f:
        data = f.read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    w, h = int(m.group(1)), int(m.group(2))
    pix = data[m.end():]
    sx, sy = w // 48, h // 32
    lum = []
    for y in range(32):
        row = []
        for x in range(48):
            px, py = x * sx + sx // 2, y * sy + sy // 2
            i = (py * w + px) * 3
            row.append(sum(pix[i:i + 3]))
        lum.append(row)
    flat = [v for r in lum for v in r]
    thr = (min(flat) + max(flat)) / 2
    return [[1 if v < thr else 0 for v in r] for r in lum]


def raw_bits(raw):
    return [[(raw[y * 6 + x // 8] >> (7 - x % 8)) & 1 for x in range(48)]
            for y in range(32)]


BAD_DMESG = re.compile(_BAD, re.I)


def fs_round(dc, tag):
    t0 = time.time()
    o = run(dc, "mksh /tmp/fsround.sh %s 2>&1" % tag, t=900)
    dt = time.time() - t0
    ok1 = len(re.findall(r"^OK1 ", o, re.M))
    ok2 = len(re.findall(r"^OK2 ", o, re.M))
    bad = re.findall(r"^(BAD\d|CPFAIL|UMOUNTFAIL|MOUNTFAIL).*", o, re.M)
    check("fs round '%s': 5 files verified before and after remount" % tag,
          ok1 == 5 and ok2 == 5 and not bad and "ROUNDDONE" in o,
          "%.1fs, ok %d/%d %s" % (dt, ok1, ok2, " ".join(bad)))
    return dt


# ---------------------------------------------------------------- test
with DCBoot(kernel=args.kernel, drives=[args.cdi], vmu=vmu, vmu_lcd=True,
            net=True) as dc:
    dc.boot(timeout=300)
    time.sleep(5)
    # The login banner and askfirst can swallow the first commands; wait
    # until the shell really answers (re-sending Enter) before going on.
    for _ in range(12):
        if "READY42" in dc.run("echo READY$((40+2))", timeout=5):
            break
        dc.send_raw("")
        time.sleep(3)
    else:
        print(bytes(dc.buf[-1500:]).decode("latin1"))
        sys.exit("guest shell never answered")
    run(dc, "uname -a")
    for name in ["vls"] + list(SCRIPTS):
        run(dc, "wget -q -O /tmp/%s http://10.0.2.2:%d/%s" % (name, port, name),
            quiet=True)
    run(dc, "chmod +x /tmp/vls /tmp/*.sh")
    auto = run(dc, "mksh /tmp/alive.sh vmu_lcd_status")
    print("INFO  vmu_lcd_status started by init: %s"
          % ("yes" if "ALIVE" in auto else "no"))
    run(dc, "mksh /tmp/killvls.sh")
    run(dc, "/tmp/vls -p splash > /tmp/a.raw")
    run(dc, "/tmp/vls -p ip > /tmp/b.raw")
    run(dc, "ls -l /dev/vmu_lcd0 /dev/mtdblock0 2>&1")
    dmesg0 = int(run(dc, "dmesg | wc -l").strip().splitlines()[-1])

    t0 = time.time()
    run(dc, "mkfs.vmufat /dev/mtdblock0 2>&1 | tail -2", t=400)
    run(dc, "sync", t=120)
    print("mkfs took %.0fs" % (time.time() - t0))
    run(dc, "mkdir -p /mnt/vmu")
    run(dc, "mount -t vmufat /dev/mtdblock0 /mnt/vmu 2>&1")
    check("vmufat mounted", "/mnt/vmu" in run(dc, "grep vmufat /proc/mounts"))

    times = {}
    times["baseline"] = fs_round(dc, "a")

    run(dc, "/tmp/vls -t 1 &")
    times["status"] = fs_round(dc, "b")

    run(dc, "rm -f /tmp/stop /tmp/hammer.out")
    run(dc, "mksh /tmp/hammer.sh &")
    time.sleep(3)
    times["hammer"] = fs_round(dc, "c")
    run(dc, "touch /tmp/stop")
    time.sleep(3)
    h = run(dc, "cat /tmp/hammer.out")
    m = re.search(r"ok=(\d+) fail=(\d+)", h)
    hok, hfail = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    check("LCD hammer got frames through during fs I/O",
          hok > 0 and hfail >= 0,
          "%d written, %d refused (busy/NAK) over %.0fs"
          % (hok, hfail, times["hammer"]))

    alive = run(dc, "mksh /tmp/alive.sh")
    check("vmu_lcd_status survived", "ALIVE" in alive)

    # Final LCD check: stop writers, show a known frame, compare pixels.
    run(dc, "mksh /tmp/killvls.sh")
    time.sleep(2)
    run(dc, "/tmp/vls -p dhcp > /tmp/c.raw")
    run(dc, "dd if=/tmp/c.raw of=/dev/vmu_lcd0 bs=192 2>&1 | tail -1")
    time.sleep(1.5)
    shot = os.path.join(out, "final.ppm")
    dc.screendump(shot, "vmu")
    ref = subprocess.run([args.native, "-p", "dhcp"],
                         capture_output=True, check=True).stdout
    got, want = ppm_bits(shot), raw_bits(ref)
    diff = sum(a != b for ra, rb in zip(got, want) for a, b in zip(ra, rb))
    check("LCD still shows a known frame pixel-exact", diff == 0,
          "%d of 1536 pixels differ" % diff)

    run(dc, "umount /mnt/vmu")
    new = run(dc, "dmesg | tail -n +%d" % (dmesg0 + 1))
    bad = [ln for ln in new.splitlines() if BAD_DMESG.search(ln)]
    check("no suspicious kernel messages", not bad,
          "; ".join(bad[:5]) if bad else "%d new lines" % len(new.splitlines()))

httpd.shutdown()

print("\nfs round times: " + ", ".join(
    "%s %.1fs" % kv for kv in times.items()))
print("artifacts in", out)
nfail = sum(not ok for _, ok, _ in results)
print("%d checks, %d failed" % (len(results), nfail))
sys.exit(1 if nfail else 0)
