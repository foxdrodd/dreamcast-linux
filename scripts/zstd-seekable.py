#!/usr/bin/env python3
"""Compress a disc image into the zstd seekable format.

    zstd-seekable.py [-l LEVEL] [-B KIB] [-j JOBS] disc.cdi disc.cdi.zst

The image is cut into fixed-size pieces, each compressed as an independent
zstd frame, followed by a seek table in a skippable frame (zstd's
contrib/seekable_format).  The browser build's http(s) block driver uses the
table to fetch and decompress single frames on demand.  The result is still
an ordinary .zst file: "zstd -d" unpacks it and ignores the table.

Independent frames compress a little worse than one stream (about 9 % on
the DC-Linux userland CDIs at 256 KiB frames), but random access stays cheap.
Needs the zstd command line tool.
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys

SKIPPABLE_MAGIC = 0x184D2A5E
SEEKABLE_MAGIC = 0x8F92EAB1


def compress(level, data):
    return subprocess.run(
        ["zstd", "-q", "-c", f"-{level}", f"--stream-size={len(data)}"],
        input=data, stdout=subprocess.PIPE, check=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-l", "--level", type=int, default=19)
    ap.add_argument("-B", "--frame-kib", type=int, default=256,
                    help="decompressed frame size in KiB (default 256)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("input")
    ap.add_argument("output")
    a = ap.parse_args()
    if a.level > 19:
        sys.exit("levels above 19 need --ultra; not supported")
    frame = a.frame_kib * 1024
    size = os.path.getsize(a.input)
    n = (size + frame - 1) // frame
    table = []
    tmp = a.output + ".tmp"
    with open(a.input, "rb") as src, open(tmp, "wb") as dst, \
            concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        pending = []

        def drain(keep):
            while len(pending) > keep:
                dlen, fut = pending.pop(0)
                c = fut.result()
                dst.write(c)
                table.append((len(c), dlen))
                if len(table) % 256 == 0 or len(table) == n:
                    print(f"\r{len(table)}/{n} frames, "
                          f"{dst.tell() / 1048576:.0f} MiB", end="",
                          file=sys.stderr, flush=True)

        while True:
            data = src.read(frame)
            if not data:
                break
            pending.append((len(data), pool.submit(compress, a.level, data)))
            drain(4 * a.jobs)
        drain(0)
        print(file=sys.stderr)
        entries = b"".join(struct.pack("<II", c, d) for c, d in table)
        footer = struct.pack("<IBI", len(table), 0, SEEKABLE_MAGIC)
        dst.write(struct.pack("<II", SKIPPABLE_MAGIC,
                              len(entries) + len(footer)))
        dst.write(entries + footer)
    os.replace(tmp, a.output)
    out = os.path.getsize(a.output)
    print(f"{a.output}: {out} bytes, {100 * out / size:.1f} % of {size}, "
          f"{len(table)} frames of {a.frame_kib} KiB", file=sys.stderr)


if __name__ == "__main__":
    main()
