#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
record-demo.py - record a demo video of the real DC from the VGA capture
dongle, like docs/assets/boot-<ver>.mp4: power-on screens -> boot -> tty1
prompt, then Enter (the login runs flashfetch) and a command (fbdoom by
default) typed on tty1.

Two captures are taken, so the serial upload of the typing helper between
them doesn't end up in the video:
  <prefix>-a.mkv   SysRq reboot -> Dreamcast/SEGA screens -> getty prompt
  <prefix>-b.mkv   Enter, flashfetch, the command running for --run-seconds
Then both are trimmed and joined into one H.264 640x480 20 fps mp4.

Input is typed with ttytype (TIOCSTI into /dev/tty1, ttytype/ttytype.c). The
prebuilt ttytype/ttytype-musl-sh4.gz runs on the musl/muslX images; on other
userlands it is compiled natively if the image has a cc (~2 min).

Reboots the DC via magic SysRq. The command is killed afterwards.

Config via env: DC_PORT, DC_BAUD (see dc.py), VIDEO_DEV (default /dev/video0)

Usage:
  ./record-demo.py                                  # -> logs/demo-<ts>.mp4
  ./record-demo.py --out ../../docs/assets/boot-7.3.1.mp4
  ./record-demo.py --skip-reboot                    # only part b (DC at prompt)
  ./record-demo.py --cmd "fbdoom -iwad /newdoom1.wad" --run-seconds 30

The trims (--trim-a / --trim-b, seconds START:END into each capture) depend on
the boot timing; check the joined video and re-cut from the kept .mkv files
with --recut PREFIX if needed.
"""
import argparse
import base64
import gzip
import hashlib
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dc import DC  # noqa: E402

TT = "/tmp/ttytype"


def capture(path):
    return subprocess.Popen(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "v4l2",
         "-input_format", "mjpeg", "-video_size", "640x480",
         "-i", os.environ.get("VIDEO_DEV", "/dev/video0"),
         "-c:v", "copy", "-y", path])


def stop(p):
    p.send_signal(signal.SIGINT)
    p.wait(timeout=20)


def run(dc, cmd, timeout=30):
    rc, out = dc.run(cmd, timeout=timeout)
    print("$ %s -> rc=%d" % (cmd[:60], rc), flush=True)
    return rc, out


def install_ttytype(dc):
    """Put ttytype at TT: upload the musl build, or compile it natively."""
    if run(dc, "ls /lib/ld-musl-sh.so.1")[0] == 0:
        data = open(os.path.join(HERE, "ttytype", "ttytype-musl-sh4.gz"),
                    "rb").read()
        want = hashlib.md5(gzip.decompress(data)).hexdigest()
        b64 = base64.b64encode(data).decode()
        run(dc, "rm -f %s %s.b64 %s.gz" % (TT, TT, TT))
        for i in range(0, len(b64), 1200):
            run(dc, "echo %s >> %s.b64" % (b64[i:i + 1200], TT))
        run(dc, "base64 -d %s.b64 > %s.gz" % (TT, TT))
        # the DC's gunzip re-compresses instead, gzip -dc works
        run(dc, "gzip -dc %s.gz > %s" % (TT, TT))
        run(dc, "chmod 755 %s" % TT)
        if want not in run(dc, "md5sum %s" % TT)[1]:
            sys.exit("ttytype upload corrupt")
        return
    if run(dc, "command -v cc")[0] != 0:
        sys.exit("not a musl image and no native cc: can't install ttytype")
    src = open(os.path.join(HERE, "ttytype", "ttytype.c"), "rb").read()
    b64 = base64.b64encode(src).decode()
    run(dc, "rm -f %s.c.b64" % TT)
    for i in range(0, len(b64), 1200):
        run(dc, "echo %s >> %s.c.b64" % (b64[i:i + 1200], TT))
    run(dc, "base64 -d %s.c.b64 > %s.c" % (TT, TT))
    # plain cc: -O2 OOMs the 11 MB box
    if run(dc, "cc -o %s %s.c" % (TT, TT), timeout=400)[0] != 0:
        sys.exit("native ttytype build failed")


def cut(prefix, trim_a, trim_b, out):
    a0, a1 = trim_a.split(":")
    b0, b1 = trim_b.split(":")
    parts, inputs = [], []
    if os.path.exists(prefix + "-a.mkv"):
        inputs += ["-i", prefix + "-a.mkv"]
        parts.append("[0:v]trim=%s:%s,setpts=PTS-STARTPTS[a]" % (a0, a1))
    n = len(parts)
    inputs += ["-i", prefix + "-b.mkv"]
    parts.append("[%d:v]trim=%s:%s,setpts=PTS-STARTPTS[b]" % (n, b0, b1))
    graph = ";".join(parts) + ";%sconcat=n=%d:v=1,fps=20,format=yuv420p[v]" % (
        "[a][b]" if n else "[b]", n + 1)
    tmp = prefix + "-cut.mp4"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
                   + inputs + ["-filter_complex", graph, "-map", "[v]",
                               "-c:v", "libx264", "-crf", "23",
                               "-preset", "slow", "-movflags", "+faststart",
                               tmp], check=True)
    os.replace(tmp, out)
    print("video: %s" % out, flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", help="mp4 to write (default logs/demo-<ts>.mp4)")
    ap.add_argument("--cmd", default="fbdoom -iwad /newdoom1.wad")
    ap.add_argument("--kill", default="fbdoom",
                    help="process to killall afterwards ('' for none)")
    ap.add_argument("--run-seconds", type=float, default=30)
    ap.add_argument("--skip-reboot", action="store_true",
                    help="no part a: the DC already shows the tty1 prompt")
    ap.add_argument("--trim-a", default="6:33")
    ap.add_argument("--trim-b", default="1.5:35")
    ap.add_argument("--recut", metavar="PREFIX",
                    help="only re-cut existing PREFIX-{a,b}.mkv captures")
    args = ap.parse_args()

    ts = time.strftime("%Y%m%d-%H%M%S")
    prefix = args.recut or os.path.join(HERE, "logs", "demo-" + ts)
    out = args.out or prefix + ".mp4"
    if args.recut:
        cut(prefix, args.trim_a, args.trim_b, out)
        return
    os.makedirs(os.path.dirname(prefix), exist_ok=True)

    dc = DC(os.environ.get("DC_PORT", "/dev/ttyUSB0"),
            int(os.environ.get("DC_BAUD", "115200")))

    # part a: reboot to the tty1 prompt
    if not args.skip_reboot:
        a = capture(prefix + "-a.mkv")
        time.sleep(3)
        dc.sysrq("b")
        t0 = time.time()
        if not dc.ensure_shell(timeout=200):
            stop(a)
            sys.exit("shell did not come back")
        print("shell back after %.0fs" % (time.time() - t0), flush=True)
        time.sleep(4)
        stop(a)
    elif not dc.ensure_shell():
        sys.exit("no live shell")

    install_ttytype(dc)

    # part b: Enter (login runs flashfetch), then the command on tty1
    b = capture(prefix + "-b.mkv")
    time.sleep(2)
    run(dc, "%s /dev/tty1" % TT)
    time.sleep(7)
    run(dc, "%s /dev/tty1 '%s'" % (TT, args.cmd), timeout=60)
    time.sleep(args.run_seconds)
    stop(b)

    # clean up: the command must not keep running
    if args.kill:
        run(dc, "killall %s" % args.kill)
        time.sleep(2)
        if run(dc, "pidof %s" % args.kill)[0] == 0:
            print("warning: %s still running" % args.kill, flush=True)
    run(dc, "rm -f %s %s.b64 %s.gz %s.c %s.c.b64" % ((TT,) * 5))
    dc.close()

    cut(prefix, args.trim_a, args.trim_b, out)
    print("captures kept: %s-{a,b}.mkv" % prefix, flush=True)


if __name__ == "__main__":
    main()
