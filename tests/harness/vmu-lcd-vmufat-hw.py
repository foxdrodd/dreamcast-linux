#!/usr/bin/env python3
"""VMU LCD + vmufat on the same VMU, on the REAL Dreamcast over serial.

Hardware twin of tests/vmu_lcd_vmufat_test.py (QEMU), same guest scripts
(tests/vmu_lcd_vmufat_guest.py): three vmufat rounds (no LCD traffic, with
vmu_lcd_status -t 1, with vmu_lcd_status plus an LCD frame hammer); each
round writes random files, syncs, drops caches, verifies, remounts and
verifies again. Afterwards new kernel messages are checked for trouble.

!! DESTRUCTIVE !! mkfs.vmufat ERASES the VMU at /dev/mtdblock0.

The DC fetches the scripts and the vmu_lcd_status binary over HTTP from
this host, so the BBA must be up and the host reachable.

    DC_PORT=/dev/ttyUSB1 ./vmu-lcd-vmufat-hw.py --host 192.168.0.225 --force
"""
import argparse
import functools
import http.server
import os
import re
import shutil
import socketserver
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from dc import DC  # noqa: E402
from vmu_lcd_vmufat_guest import BAD_DMESG, write_scripts  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--host", required=True, help="IP of this PC as seen by the DC")
ap.add_argument("--bin", default=os.path.join(
    ROOT, "drivers/vmu-lcd/vmu_lcd_status_nolibc"))
ap.add_argument("--force", action="store_true", help="erase the VMU, no prompt")
args = ap.parse_args()

if not args.force:
    a = input("mkfs.vmufat will ERASE the VMU at /dev/mtdblock0. Continue? [y/N] ")
    if a.strip().lower() != "y":
        sys.exit("aborted")

srv = tempfile.mkdtemp(prefix="vmu-hw-")
shutil.copy(args.bin, os.path.join(srv, "vls"))
write_scripts(srv)
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=srv)
handler.log_message = lambda *a: None
httpd = socketserver.TCPServer(("0.0.0.0", 0), handler)
port = httpd.server_address[1]
threading.Thread(target=httpd.serve_forever, daemon=True).start()

os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
logpath = os.path.join(HERE, "logs", "vmu-lcd-vmufat-%s.log"
                       % time.strftime("%Y%m%d-%H%M%S"))
log = open(logpath, "w")
results = []


def say(s):
    print(s, flush=True)
    log.write(s + "\n")
    log.flush()


def check(name, ok, detail=""):
    results.append(ok)
    say("%s  %s  %s" % ("PASS" if ok else "FAIL", name, detail))


dc = DC(os.environ.get("DC_PORT", "/dev/ttyUSB1"),
        int(os.environ.get("DC_BAUD", "115200")))


def run(cmd, t=60, quiet=False):
    """One atomic command (no ; | && - dc.py appends its own marker)."""
    rc, out = dc.run(cmd, timeout=t)
    if not quiet:
        say("$ %s  [rc=%d]\n%s" % (cmd, rc, out))
    return out


def fs_round(tag):
    t0 = time.time()
    o = run("mksh /tmp/fsround.sh %s" % tag, t=900)
    dt = time.time() - t0
    ok1 = len(re.findall(r"^OK1 ", o, re.M))
    ok2 = len(re.findall(r"^OK2 ", o, re.M))
    bad = re.findall(r"^(BAD\d|CPFAIL|UMOUNTFAIL|MOUNTFAIL).*", o, re.M)
    check("fs round '%s': 5 files verified before and after remount" % tag,
          ok1 == 5 and ok2 == 5 and not bad and "ROUNDDONE" in o,
          "%.1fs, ok %d/%d %s" % (dt, ok1, ok2, " ".join(bad)))
    return dt


try:
    if not dc.ensure_shell():
        sys.exit("no live shell on the DC console")
    run("uname -a")
    for name in ["vls"] + sorted(f for f in os.listdir(srv) if f.endswith(".sh")):
        run("wget -q -O /tmp/%s http://%s:%d/%s" % (name, args.host, port, name),
            quiet=True)
    run("chmod 755 /tmp/vls")
    run("ls -l /tmp/vls /dev/vmu_lcd0 /dev/mtdblock0")
    auto = run("mksh /tmp/alive.sh vmu_lcd_status")
    say("INFO  vmu_lcd_status started by init: %s"
        % ("yes" if "ALIVE" in auto else "no"))
    run("mksh /tmp/killvls.sh")
    run("/tmp/vls -p splash > /tmp/a.raw")
    run("/tmp/vls -p ip > /tmp/b.raw")
    dmesg0 = len(run("dmesg", t=60, quiet=True).splitlines())

    t0 = time.time()
    run("mkfs.vmufat /dev/mtdblock0", t=900)
    run("sync", t=300)
    say("mkfs took %.0fs" % (time.time() - t0))
    run("mkdir -p /mnt/vmu")
    run("mount -t vmufat /dev/mtdblock0 /mnt/vmu")
    check("vmufat mounted", "/mnt/vmu" in run("cat /proc/mounts"))

    times = {"baseline": fs_round("a")}
    run("mksh /tmp/start_status.sh")
    times["status"] = fs_round("b")
    run("mksh /tmp/start_hammer.sh")
    time.sleep(3)
    times["hammer"] = fs_round("c")
    run("touch /tmp/stop")
    time.sleep(5)
    h = run("cat /tmp/hammer.out")
    m = re.search(r"ok=(\d+) fail=(\d+)", h)
    hok, hfail = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    check("LCD hammer got frames through during fs I/O", hok > 0 and hfail >= 0,
          "%d written, %d refused (busy/NAK) over %.0fs"
          % (hok, hfail, times["hammer"]))
    check("vmu_lcd_status survived", "ALIVE" in run("mksh /tmp/alive.sh"))

    run("mksh /tmp/killvls.sh")
    run("umount /mnt/vmu")
    lines = run("dmesg", t=60, quiet=True).splitlines()
    new = lines[dmesg0:] if len(lines) >= dmesg0 else lines
    say("new kernel messages:\n" + "\n".join(new))
    bad = [ln for ln in new if re.search(BAD_DMESG, ln, re.I)]
    check("no suspicious kernel messages", not bad,
          "; ".join(bad[:5]) if bad else "%d new lines" % len(new))
    say("fs round times: " + ", ".join("%s %.1fs" % kv for kv in times.items()))
finally:
    dc.close()
    httpd.shutdown()

say("%d checks, %d failed  (log: %s)"
    % (len(results), results.count(False), logpath))
sys.exit(0 if all(results) else 1)
