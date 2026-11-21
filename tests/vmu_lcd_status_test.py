#!/usr/bin/env python3
"""End-to-end test of vmu_lcd_status on QEMU's emulated VMU LCD.

Boots a self-booting CDI with slirp network and the VMU LCD, fetches the
SH4 vmu_lcd_status binary from a host HTTP server (guest sees the host as
10.0.2.2), runs it through all states and screendumps the VMU each time:

  dhcp   waiting for an address (-i ppp0, which never gets one)
  mood   after the wait expires
  exit   SIGTERM -> plain splash
  ip     fresh start on eth0 -> IP bubble
  mood   after the IP page
  dizzy  tmpfs filled until MemAvailable < 5%
  calm   tmpfs freed again
  exit   SIGTERM -> plain splash

    vmu_lcd_status_test.py CDI BINARY OUTDIR
"""
import functools
import http.server
import os
import re
import socketserver
import sys
import threading
import time

sys.path.insert(0, "/home/flo/.claude/skills/dreamcast-linux/scripts")
from dcboot import DCBoot  # noqa: E402

CDI, BIN, OUT = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3]
os.makedirs(OUT, exist_ok=True)
vmu = os.path.join(OUT, "vmu.bin")
with open(vmu, "wb") as f:
    f.write(b"\0" * 131072)

handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                            directory=os.path.dirname(BIN))
handler.log_message = lambda *a: None
httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
port = httpd.server_address[1]
threading.Thread(target=httpd.serve_forever, daemon=True).start()

shots = []


def shot(dc, name, wait=0.0):
    time.sleep(wait)
    path = os.path.join(OUT, name + ".ppm")
    dc.screendump(path, "vmu")
    shots.append(name)
    print("shot", name, flush=True)


def step(dc, cmd, t=60):
    out = dc.run(cmd, timeout=t)
    print("$ %s\n%s" % (cmd, out), flush=True)
    return out


with DCBoot(drives=[CDI], vmu=vmu, vmu_lcd=True, net=True) as dc:
    dc.boot(timeout=300)
    time.sleep(5)
    for _ in range(5):
        if "ok" in dc.run("echo ok"):
            break
        time.sleep(2)
    step(dc, "wget -q -O /tmp/vls http://10.0.2.2:%d/%s"
         % (port, os.path.basename(BIN)))
    step(dc, "chmod +x /tmp/vls")
    step(dc, "/tmp/vls -h")

    step(dc, "/tmp/vls -i ppp0 -w 6 -t 1 &")
    step(dc, "echo $! > /tmp/pid")
    shot(dc, "1-dhcp", 2)
    shot(dc, "2-mood-after-wait", 7)
    step(dc, "kill $(cat /tmp/pid)")
    shot(dc, "3-exit-splash", 2)

    step(dc, "/tmp/vls -t 1 -s 4 &")
    step(dc, "echo $! > /tmp/pid")
    shot(dc, "4-ip", 1.5)
    shot(dc, "5-mood", 6)

    mi = step(dc, "grep MemAvailable /proc/meminfo")
    avail = int(re.search(r"(\d+) kB", mi).group(1))
    step(dc, "mkdir -p /tmp/fill")
    step(dc, "mount -t tmpfs -o size=16m tmpfs /tmp/fill")
    # leave ~3% of RAM available: below the 5% "dizzy" threshold
    step(dc, "dd if=/dev/zero of=/tmp/fill/x bs=1k count=%d 2>&1 | tail -1"
         % (avail - 360), t=120)
    step(dc, "grep MemAvailable /proc/meminfo")
    shot(dc, "6-dizzy", 3)
    step(dc, "rm /tmp/fill/x")
    shot(dc, "7-calm", 3)
    step(dc, "kill $(cat /tmp/pid)")
    shot(dc, "8-exit-splash", 2)
    step(dc, "dmesg | tail -3")

httpd.shutdown()

from PIL import Image, ImageDraw, ImageFont  # noqa: E402
ims = [(n, Image.open(os.path.join(OUT, n + ".ppm")).convert("RGB"))
       for n in shots]
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
sheet.save(os.path.join(OUT, "sheet.png"))
print("sheet:", os.path.join(OUT, "sheet.png"))
