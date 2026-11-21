#!/usr/bin/env python3
"""Show the VMU status mockups on QEMU's emulated VMU LCD.

Boots a self-booting CDI with slirp network and the VMU LCD, reads live
values (IP, meminfo, swap, load, uptime) from the guest, renders the designs
with them, writes each 192-byte frame to /dev/vmu_lcd0 and screendumps the
VMU console.

    qemu_show.py CDI OUTDIR [all|status|tux]
"""
import base64
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/home/flo/.claude/skills/dreamcast-linux/scripts")
from dcboot import DCBoot  # noqa: E402
import vmu_status_mockups as M  # noqa: E402

CDI, OUT = sys.argv[1], sys.argv[2]
WHICH = sys.argv[3] if len(sys.argv) > 3 else "all"
SETS = {"status": M.DESIGNS, "tux": M.TUX_DESIGNS,
        "all": M.DESIGNS + M.TUX_DESIGNS}[WHICH]
os.makedirs(OUT, exist_ok=True)
vmu = os.path.join(OUT, "vmu.bin")
with open(vmu, "wb") as f:
    f.write(b"\0" * 131072)


def kb(mi, key):
    m = re.search(r"^%s:\s+(\d+) kB" % key, mi, re.M)
    return int(m.group(1)) if m else 0


def push(dc, raw, name):
    """Send one frame as base64 in short lines, decode it into the LCD."""
    b = base64.b64encode(raw).decode()
    dc.run("rm -f /tmp/f.b64")
    for i in range(0, len(b), 64):
        dc.run("echo %s >> /tmp/f.b64" % b[i:i + 64])
    base64.b64decode(b)  # sanity
    r = dc.run("sh /tmp/put.sh")
    print(name, r.strip().splitlines()[-1:] )
    time.sleep(0.8)
    ppm = os.path.join(OUT, name + ".ppm")
    dc.screendump(ppm, "vmu")
    subprocess.run(["python3", "-c",
                    "from PIL import Image;import sys;"
                    "Image.open(sys.argv[1]).save(sys.argv[2])",
                    ppm, ppm[:-4] + ".png"], check=True)


with DCBoot(drives=[CDI], vmu=vmu, vmu_lcd=True, net=True) as dc:
    dc.boot(timeout=300)
    time.sleep(5)                       # let the login banner finish
    for _ in range(5):
        if "ok" in dc.run("echo ok"):
            break
        time.sleep(2)
    print(dc.run("ls -l /dev/vmu_lcd0; which base64"))
    # The driver takes only whole 192-byte writes (base64 -d may split its
    # output -> EINVAL), so go through dd; retry transient -ENXIO (VMU still
    # painting the previous frame).
    for ln in ("base64 -d /tmp/f.b64 > /tmp/f.raw",
               "for i in 1 2 3 4 5; do",
               " dd if=/tmp/f.raw of=/dev/vmu_lcd0 bs=192 2>/dev/null && break",
               " sleep 0.2",
               "done",
               "echo try=$i"):
        dc.run("echo '%s' >> /tmp/put.sh" % ln)
    ip = dc.run("ifconfig eth0")
    if not re.search(r"inet (addr:)?\d", ip):
        print(dc.run("udhcpc -i eth0 -n -q -t 5 2>&1 || dhcpcd -1 eth0 2>&1",
                     timeout=60))
        ip = dc.run("ifconfig eth0")
    print(ip)
    mi = dc.run("cat /proc/meminfo")
    la = dc.run("cat /proc/loadavg")
    up = dc.run("cat /proc/uptime")
    host = (dc.run("uname -n").strip().splitlines() or ["dreamcast"])[-1]
    m = re.search(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", ip)
    data = {
        "ip": m.group(1) if m else "0.0.0.0",
        "host": host,
        "mem_total": kb(mi, "MemTotal"),
        "mem_avail": kb(mi, "MemAvailable"),
        "swap_total": kb(mi, "SwapTotal"),
        "swap_used": kb(mi, "SwapTotal") - kb(mi, "SwapFree"),
        "load": float(re.search(r"(\d+\.\d+)", la).group(1)),
        "uptime": int(float(re.search(r"(\d+\.\d+)", up).group(1))),
    }
    if not data["swap_total"]:  # no swap in this kernel/boot: fake 6M VRAM
        data["swap_total"], data["swap_used"] = 6144, 1180
    print(json.dumps(data))
    with open(os.path.join(OUT, "stats.json"), "w") as f:
        json.dump(data, f)
    full = dict(M.DEFAULT)
    full.update(data)

    # what the existing tool does today, for comparison
    dc.run("vmu_lcd_text 'IP %s' > /dev/vmu_lcd0" % data["ip"])
    time.sleep(0.8)
    dc.screendump(os.path.join(OUT, "0-vmu_lcd_text.ppm"), "vmu")

    for name, title, fn in SETS:
        push(dc, fn(full).to_bytes(), name)
        print("shown", name)

# contact sheet of the VMU screendumps
from PIL import Image, ImageDraw, ImageFont  # noqa: E402
names = ["0-vmu_lcd_text"] + [n for n, _, _ in SETS]
ims = [(n, Image.open(os.path.join(OUT, n + ".ppm")).convert("RGB"))
       for n in names]
w, h = ims[0][1].size
cols, lab = 4, 22
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 12) + 12, rows * (h + lab + 12) + 12),
                  (32, 32, 36))
dr = ImageDraw.Draw(sheet)
font = ImageFont.truetype("DejaVuSans.ttf", 14)
for i, (n, im) in enumerate(ims):
    x, y = 12 + (i % cols) * (w + 12), 12 + (i // cols) * (h + lab + 12)
    dr.text((x, y), n, fill=(230, 230, 230), font=font)
    sheet.paste(im, (x, y + lab))
sheet.save(os.path.join(OUT, "qemu-sheet-%s.png" % WHICH))
print("sheet:", os.path.join(OUT, "qemu-sheet-%s.png" % WHICH))
