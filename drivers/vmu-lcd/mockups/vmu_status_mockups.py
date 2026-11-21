#!/usr/bin/env python3
"""Design mockups for a VMU LCD status screen (IP after DHCP, memory use).

Renders each design as a raw 192-byte VMU framebuffer (48x32, 1 bpp,
6 bytes per row, MSB = leftmost pixel, row 0 at the top, i.e. exactly what
/dev/vmu_lcd0 takes) plus an enlarged PNG preview and a contact sheet.

    vmu_status_mockups.py OUTDIR [--data stats.json]

stats.json (optional) holds live values, e.g. collected from the guest:
    {"ip": "10.0.2.15", "host": "dreamcast", "mem_total": 11520,
     "mem_avail": 4300, "swap_total": 6144, "swap_used": 900,
     "load": 0.42, "uptime": 754, "hist": [55, 57, ...]}   (kB / s / %)
"""
import json
import math
import os
import sys

W, H = 48, 32

# --------------------------------------------------------------------------
# Fonts
# --------------------------------------------------------------------------

# 3x5 "tiny" font, variable width (rows as strings, '#' = pixel on).
F3 = {
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["##.", "..#", ".#.", "#..", "###"],
    "3": ["##.", "..#", ".#.", "..#", "##."],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "##.", "..#", "##."],
    "6": [".##", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", ".#.", ".#.", ".#."],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "##."],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": [".##", "#..", "#..", "#..", ".##"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "F": ["###", "#..", "##.", "#..", "#.."],
    "G": [".##", "#..", "#.#", "#.#", ".##"],
    "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
    "J": ["..#", "..#", "..#", "#.#", ".#."],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "N": ["#..#", "##.#", "#.##", "#..#", "#..#"],
    "O": [".#.", "#.#", "#.#", "#.#", ".#."],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "Q": [".#.", "#.#", "#.#", "##.", ".##"],
    "R": ["##.", "#.#", "##.", "#.#", "#.#"],
    "S": [".##", "#..", ".#.", "..#", "##."],
    "T": ["###", ".#.", ".#.", ".#.", ".#."],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"],
    "V": ["#.#", "#.#", "#.#", "#.#", ".#."],
    "W": ["#...#", "#...#", "#.#.#", "##.##", "#...#"],
    "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "Y": ["#.#", "#.#", ".#.", ".#.", ".#."],
    "Z": ["###", "..#", ".#.", "#..", "###"],
    ".": [".", ".", ".", ".", "#"],
    ":": [".", "#", ".", "#", "."],
    "%": ["#.#", "..#", ".#.", "#..", "#.#"],
    "/": ["..#", "..#", ".#.", "#..", "#.."],
    "-": ["..", "..", "##", "..", ".."],
    " ": ["..", "..", "..", "..", ".."],
}

# 4x7 font from vmu_lcd_text.c (bit3 = leftmost), digits + a few letters,
# first row is always blank there, so keep only the 6 drawn rows.
_F4BITS = {
    "0": [0x06, 0x09, 0x0b, 0x0d, 0x09, 0x06],
    "1": [0x02, 0x06, 0x02, 0x02, 0x02, 0x07],
    "2": [0x06, 0x09, 0x01, 0x06, 0x08, 0x0f],
    "3": [0x0e, 0x01, 0x06, 0x01, 0x09, 0x06],
    "4": [0x02, 0x06, 0x0a, 0x0f, 0x02, 0x02],
    "5": [0x0f, 0x08, 0x0e, 0x01, 0x09, 0x06],
    "6": [0x06, 0x08, 0x0e, 0x09, 0x09, 0x06],
    "7": [0x0f, 0x01, 0x02, 0x04, 0x04, 0x04],
    "8": [0x06, 0x09, 0x06, 0x09, 0x09, 0x06],
    "9": [0x06, 0x09, 0x09, 0x07, 0x01, 0x06],
}
F4 = {k: ["".join("#" if b & (8 >> i) else "." for i in range(4)) for b in v]
      for k, v in _F4BITS.items()}
F4["."] = [".", ".", ".", ".", ".", "#"]
F4["%"] = ["#..#", "...#", "..#.", ".#..", "#...", "#..#"]
F4[" "] = ["..", "..", "..", "..", "..", ".."]

# Big 5x9 digits for "hero" numbers.
F5 = {
    "0": [".###.", "#...#", "#...#", "#..##", "#.#.#", "##..#", "#...#", "#...#", ".###."],
    "1": ["..#..", ".##..", "#.#..", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "2": [".###.", "#...#", "....#", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "3": [".###.", "#...#", "....#", "....#", "..##.", "....#", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#..#.", "#####", "...#.", "...#.", "...#."],
    "5": ["#####", "#....", "#....", "####.", "....#", "....#", "....#", "#...#", ".###."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "...#.", "..#..", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", "#...#", ".###.", "#...#", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", "#...#", ".####", "....#", "....#", "....#", ".###."],
    "%": ["##...", "##..#", "...#.", "...#.", "..#..", ".#...", ".#...", "#..##", "...##"],
    ".": [".", ".", ".", ".", ".", ".", ".", ".", "#"],
    " ": ["..", "..", "..", "..", "..", "..", "..", "..", ".."],
}

# 7x7 icons.
ICONS = {
    # two linked boxes: network
    "net": ["###....", "#.#....", "###....", ".#.....", ".####..", "....###", "....###"],
    # memory chip with legs
    "ram": [".#.#.#.", "#######", "#.....#", "#.###.#", "#.....#", "#######", ".#.#.#."],
    # up/down arrows: swap
    "swap": [".#.....", "###..#.", ".#...#.", ".#...#.", ".#...#.", ".#..###", ".....#."],
    "check": [".......", "......#", ".....##", "#...##.", "##.##..", ".###...", "..#...."],
    "cross": ["#.....#", ".#...#.", "..#.#..", "...#...", "..#.#..", ".#...#.", "#.....#"],
    "clock": [".#####.", "#..#..#", "#..#..#", "#..##.#", "#.....#", "#.....#", ".#####."],
    # little plug: wired link
    "plug": ["..#.#..", "..#.#..", ".#####.", ".#####.", "..###..", "...#...", "...#..."],
}


class Fb:
    def __init__(self):
        self.p = [[0] * W for _ in range(H)]

    def set(self, x, y, v=1):
        if 0 <= x < W and 0 <= y < H:
            self.p[y][x] = v

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < W and 0 <= y < H else 0

    def rect(self, x, y, w, h, v=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, v)

    def frame(self, x, y, w, h, v=1):
        for xx in range(x, x + w):
            self.set(xx, y, v)
            self.set(xx, y + h - 1, v)
        for yy in range(y, y + h):
            self.set(x, yy, v)
            self.set(x + w - 1, yy, v)

    def hline(self, x0, x1, y, v=1, dotted=False):
        for x in range(x0, x1 + 1):
            if not dotted or x % 2 == 0:
                self.set(x, y, v)

    def vline(self, x, y0, y1, v=1):
        for y in range(y0, y1 + 1):
            self.set(x, y, v)

    def invert(self, x, y, w, h):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, 1 - self.get(xx, yy))

    def bitmap(self, rows, x, y, v=1, scale=1):
        for gy, row in enumerate(rows):
            for gx, c in enumerate(row):
                if c == "#":
                    for sy in range(scale):
                        for sx in range(scale):
                            self.set(x + gx * scale + sx, y + gy * scale + sy, v)

    def text(self, s, x, y, font=F3, v=1, gap=1, scale=1):
        for ch in s.upper():
            g = font.get(ch, font[" "])
            self.bitmap(g, x, y, v, scale)
            x += (len(g[0]) + gap) * scale
        return x

    def icon(self, name, x, y, v=1):
        self.bitmap(ICONS[name], x, y, v)

    def to_bytes(self, flip=False):
        p = self.p
        if flip:
            p = [row[::-1] for row in p[::-1]]
        out = bytearray()
        for row in p:
            for b in range(0, W, 8):
                byte = 0
                for i in range(8):
                    byte = (byte << 1) | row[b + i]
                out.append(byte)
        return bytes(out)


def text_w(s, font=F3, gap=1, scale=1):
    return sum((len(font.get(c, font[" "])[0]) + gap) * scale
               for c in s.upper()) - gap * scale


def bar(fb, x, y, w, h, frac, style="solid"):
    """Framed bar, filled to frac (0..1)."""
    fb.frame(x, y, w, h)
    inner = w - 2
    n = int(round(inner * max(0.0, min(1.0, frac))))
    if style == "solid":
        fb.rect(x + 1, y + 1, n, h - 2)
    elif style == "segments":  # 2px segments with 1px gaps
        for xx in range(x + 1, x + 1 + n):
            if (xx - x - 1) % 3 != 2:
                fb.vline(xx, y + 1, y + h - 2)
    elif style == "hatched":
        for xx in range(x + 1, x + 1 + n):
            for yy in range(y + 1, y + h - 1):
                if (xx + yy) % 2 == 0:
                    fb.set(xx, yy)


def fmt_mb(kb):
    mb = kb / 1024.0
    return ("%.1fM" % mb) if mb < 10 else ("%dM" % round(mb))


def fmt_up(s):
    """Uptime as H:MM, or xDyy (days + hours) after a day."""
    if s < 86400:
        return "%d:%02d" % (s // 3600, (s % 3600) // 60)
    return "%dD%02d" % (s // 86400, (s % 86400) // 3600)


def split_ip(ip):
    """Two-line split for the 4x7 font: 'a.b.' / 'c.d'."""
    p = ip.split(".")
    return ".".join(p[:2]) + ".", ".".join(p[2:])


# --------------------------------------------------------------------------
# Designs
# --------------------------------------------------------------------------

def d_netcard(d):
    """Inverted header with link status, IP in tiny font, hostname."""
    fb = Fb()
    fb.rect(0, 0, W, 9)
    fb.icon("plug", 1, 1, 0)
    fb.text("ETH0", 10, 2, v=0)
    fb.icon("check", 40, 1, 0)
    fb.text(d["ip"], (W - text_w(d["ip"])) // 2, 13)
    fb.hline(0, W - 1, 21, dotted=True)
    h = d["host"][:11]
    fb.text(h, (W - text_w(h)) // 2, 25)
    return fb


def d_bigip(d):
    """Readable from the couch: IP in the 4x7 font on two lines."""
    fb = Fb()
    fb.text("IP", 0, 0)
    fb.icon("plug", 41, 0)
    if text_w(d["ip"], F4) <= W:      # short IPs fit on one line
        fb.text(d["ip"], (W - text_w(d["ip"], F4)) // 2, 14, F4)
    else:
        a, b = split_ip(d["ip"])
        fb.text(a, 0, 9, F4)
        fb.text(b, W - text_w(b, F4), 19, F4)
    fb.hline(0, W - 1, 30)
    return fb


def d_bars(d):
    """Three labelled rows: RAM bar, SWAP bar, load + uptime."""
    fb = Fb()
    used = d["mem_total"] - d["mem_avail"]
    ram = used / d["mem_total"]
    swp = d["swap_used"] / d["swap_total"] if d["swap_total"] else 0
    for i, (lbl, frac) in enumerate((("RAM", ram), ("SWP", swp))):
        y = 1 + i * 10
        fb.text(lbl, 0, y + 1)
        bar(fb, 15, y, 21, 7, frac)
        pct = "%d" % round(frac * 100)
        fb.text(pct, W - text_w(pct), y + 1)
    fb.hline(0, W - 1, 21, dotted=True)
    fb.text("%.2f" % d["load"], 0, 25)
    up = fmt_up(d["uptime"])
    fb.text(up, W - text_w(up), 25)
    fb.icon("clock", W - text_w(up) - 9, 24)
    return fb


def d_graph(d):
    """RAM history, one column per sample, newest at the right."""
    fb = Fb()
    used = d["mem_total"] - d["mem_avail"]
    pct = round(100 * used / d["mem_total"])
    fb.text("RAM", 0, 0)
    s = "%d%%" % pct
    fb.text(s, W - text_w(s), 0)
    top, bot = 8, 31
    fb.hline(0, W - 1, top, dotted=True)  # 100% line
    hist = (d["hist"] + [pct])[-W:]
    hist = [hist[0]] * (W - len(hist)) + hist
    for x, v in enumerate(hist):
        hgt = int(round((bot - top - 1) * v / 100.0))
        for y in range(bot - hgt + 1, bot + 1):
            # solid newest quarter, hatched older history
            if x >= W * 3 // 4 or (x + y) % 2 == 0:
                fb.set(x, y)
    return fb


def d_hero(d):
    """One big number: RAM used %, with absolute values below."""
    fb = Fb()
    used = d["mem_total"] - d["mem_avail"]
    pct = "%d%%" % round(100 * used / d["mem_total"])
    fb.icon("ram", 0, 1)
    fb.text("RAM", 0, 10)
    x0 = W - text_w(pct, F5)
    fb.text(pct, x0, 0, F5)
    fb.hline(0, W - 1, 12 + 6, dotted=True)
    s = "%s/%s" % (fmt_mb(used), fmt_mb(d["mem_total"]))
    fb.text(s, (W - text_w(s)) // 2, 21)
    bar(fb, 0, 28, W, 4, used / d["mem_total"], "segments")
    return fb


def d_ring(d):
    """Ring gauge for RAM on the left, numbers on the right."""
    fb = Fb()
    used = d["mem_total"] - d["mem_avail"]
    frac = used / d["mem_total"]
    cx, cy, ro, ri = 15, 15.5, 15.5, 10
    for y in range(H):
        for x in range(32):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy)
            if ri <= r <= ro:
                a = (math.atan2(dx, -dy) / (2 * math.pi)) % 1.0  # 0 at top, cw
                if a <= frac:
                    fb.set(x, y)
                elif r > ro - 1.1 or r < ri + 1.1:
                    fb.set(x, y)  # outline of the empty part
    pct = "%d" % round(frac * 100)
    fb.text(pct, cx - text_w(pct) // 2 + 1, 14 - 2)
    fb.text("%", cx - 1, 18)
    fb.text("RAM", 34, 2)
    fb.text(fmt_mb(used), 34, 9)
    fb.text("SWP", 34, 18)
    fb.text(fmt_mb(d["swap_used"]), 34, 25)
    return fb


def d_combo(d):
    """Everything at once: IP header, RAM + SWAP slim bars, load."""
    fb = Fb()
    used = d["mem_total"] - d["mem_avail"]
    fb.text(d["ip"], (W - text_w(d["ip"])) // 2, 0)
    fb.hline(0, W - 1, 6)
    fb.icon("ram", 0, 9)
    bar(fb, 9, 10, W - 9, 5, used / d["mem_total"])
    fb.icon("swap", 0, 17)
    swp = d["swap_used"] / d["swap_total"] if d["swap_total"] else 0
    bar(fb, 9, 18, W - 9, 5, swp, "hatched")
    fb.text("LD", 0, 27)
    fb.text("%.2f" % d["load"], 9, 27)
    up = fmt_up(d["uptime"])
    fb.text(up, W - text_w(up), 27)
    return fb


def d_dhcp_wait(d, phase=0):
    """While DHCP runs: label plus a spinner of 8 dots."""
    fb = Fb()
    fb.text("DHCP", (W - text_w("DHCP")) // 2, 3)
    cx, cy = 24, 19
    for i in range(8):
        a = i * math.pi / 4
        x, y = cx + round(7 * math.sin(a)), cy - round(7 * math.cos(a))
        k = (i - phase) % 8
        if k == 0:
            fb.rect(x - 1, y - 1, 3, 3)
        elif k < 3:
            fb.rect(x - 1, y - 1, 2, 2)
        else:
            fb.set(x, y)
    return fb


def d_dhcp_fail(d):
    fb = Fb()
    fb.icon("cross", 20, 2)
    s = "NO LINK"
    fb.text(s, (W - text_w(s)) // 2, 13)
    s = "RETRY 30S"
    fb.text(s, (W - text_w(s)) // 2, 22)
    return fb


def d_rotate(d):
    """Pages that cycle every few seconds; page dots at the bottom."""
    fb = d_netcard(d)
    fb.rect(0, 22, W, 10, 0)
    h = d["host"][:11]
    fb.text(h, (W - text_w(h)) // 2, 23)
    for i in range(3):
        x = 19 + i * 4
        if i == 0:
            fb.rect(x, 30, 2, 2)
        else:
            fb.set(x, 31)
    return fb


# --------------------------------------------------------------------------
# Tux designs (the kernel's vmu-lcd splash Tux, stored upright here; the
# driver keeps it rotated 180 degrees for the controller slot)
# --------------------------------------------------------------------------

TUX = [
    "................................................",
    "...........#####................................",
    "..........########..............................",
    "..........########......#...#...................",
    "..........#########.....#.......................",
    ".........##.###.###.....#...#.#.#..#.#..#.#.....",
    ".........#.###.#.##.....#...#.##.#.#.#...#......",
    ".........#.#####.##.....#...#.#..#.#.#..#.#.....",
    "..........#########.....###.#.#..#..#.#.#.#.....",
    "..........##########............................",
    "..........##########............................",
    "..........#####..###............................",
    ".........#........###...........................",
    "........##........####..........................",
    ".......##.........####..........................",
    ".......##..........####.........................",
    "......##...........#####........................",
    "......##............####........................",
    "......#.............#####.......................",
    ".....##.............#####.......................",
    ".....##.............#####.......................",
    "....###.............#####.......................",
    "....###.............#####.......................",
    "....####...........######.......................",
    "..#######.........#######.......................",
    ".##########.......########......................",
    ".##########.......#########.....................",
    ".#########.......###########....................",
    "###########....############.....................",
    "##########################......................",
    "..######################........................",
    "......#####.......####..........................",
]
TUX_W = 28          # Tux itself, the "Linux" lettering sits right of it

MINI_TUX = [        # 11x13 for corners
    "...#####...",
    "..#######..",
    "..#.###.#..",
    "..###.###..",
    "..##...##..",
    ".##.....##.",
    ".#.......#.",
    "##.......##",
    "##.......##",
    ".#.......#.",
    ".##.....##.",
    "###.....###",
    "####...####",
]


def tux_fb(with_text=False):
    fb = Fb()
    for y, row in enumerate(TUX):
        # the "Linux" lettering starts at x=24 in rows 3-8
        cut = TUX_W if y > 8 else 20
        for x, c in enumerate(row if with_text else row[:cut]):
            if c == "#":
                fb.set(x, y)
    return fb


def belly_mask():
    """Flood fill the white belly from inside it."""
    seen, todo = set(), [(13, 20)]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (0 <= x < TUX_W and 0 <= y < H):
            continue
        if TUX[y][x] == "#":
            continue
        seen.add((x, y))
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return seen


BELLY = belly_mask()


def fill_belly(fb, frac):
    """Fill from the bottom so the filled AREA matches frac."""
    cells = sorted((p for p in BELLY if p[1] > 11), key=lambda p: -p[1])
    n = int(round(frac * len(cells)))
    if n == 0:
        return
    level = cells[n - 1][1]
    for x, y in cells:
        if y > level and (x + y) % 2 == 0:
            fb.set(x, y)
        elif y == level:
            fb.set(x, y)                  # crisp "liquid" surface


def face(fb, mood):
    """Repaint the eyes: 'ok' keeps the original, 'blink', 'dizzy'."""
    if mood == "ok":
        return
    fb.rect(10, 4, 9, 4)                  # paint the eye area black
    if mood == "blink":
        fb.hline(11, 12, 6, 0)
        fb.hline(16, 17, 6, 0)
    elif mood == "dizzy":
        for ex in (11, 15):
            for i in range(3):
                fb.set(ex + i, 4 + i, 0)
                fb.set(ex + 2 - i, 4 + i, 0)


def stars(fb):
    for x, y in ((4, 2), (21, 1), (24, 7)):
        fb.bitmap([".#.", "#.#", ".#."], x, y)


def sweat(fb, x=21, y=2):
    fb.bitmap([".#.", "#.#", "#.#", ".#."], x, y)
    fb.set(x + 4, y + 4)
    fb.set(x + 4, y + 5)


def ram_frac(d):
    return (d["mem_total"] - d["mem_avail"]) / d["mem_total"]


def swp_frac(d):
    return d["swap_used"] / d["swap_total"] if d["swap_total"] else 0.0


def right_col(fb, lines, x0=TUX_W + 1, y0=1, step=8):
    for i, s in enumerate(lines):
        fb.text(s, W - text_w(s) if x0 is None else x0, y0 + i * step)


def t_splash(d):
    """The kernel splash as it is today (upright)."""
    return tux_fb(with_text=True)


def t_says_ip(d):
    """Tux with a speech bubble holding the IP, one octet pair per line."""
    fb = tux_fb()
    p = d["ip"].split(".")
    lines = [p[0] + ".", p[1] + ".", p[2] + "." + p[3]]
    bx, by, bw, bh = 24, 0, 24, 29
    font, step = (F4, 9) if max(text_w(t, F4) for t in lines) <= bw - 4 \
        else (F3, 9)
    fb.rect(bx, by, bw, bh, 0)
    fb.frame(bx, by, bw, bh)
    for c in ((bx, by), (bx + bw - 1, by), (bx, by + bh - 1),
              (bx + bw - 1, by + bh - 1)):
        fb.set(c[0], c[1], 0)             # rounded corners
    fb.set(bx, 8, 0)                      # tail toward the beak
    fb.set(bx - 1, 8)
    fb.set(bx - 2, 9)
    fb.set(bx, 9, 0)
    fb.set(bx - 1, 9)
    for i, t in enumerate(lines):
        fb.text(t, bx + bw - 3 - text_w(t, font), by + 2 + i * step, font)
    return fb


def t_belly(d):
    """Belly fills with used RAM; numbers on the right."""
    fb = tux_fb()
    fill_belly(fb, ram_frac(d))
    right_col(fb, ["RAM", "%d%%" % round(100 * ram_frac(d)),
                   "SWP", "%d%%" % round(100 * swp_frac(d))], x0=None)
    return fb


def t_mood(d, ram, swp):
    """Tamagotchi Tux: belly = RAM, sweat = swapping, dizzy = nearly OOM."""
    d = dict(d, mem_avail=d["mem_total"] * (1 - ram),
             swap_used=d["swap_total"] * swp)
    fb = tux_fb()
    fill_belly(fb, ram)
    if ram > 0.95:
        face(fb, "dizzy")
        stars(fb)
    elif swp > 0.3:
        sweat(fb)
    right_col(fb, ["RAM", "%d%%" % round(100 * ram),
                   "SWP", "%d%%" % round(100 * swp)], x0=None)
    return fb


def t_dhcp(d, phase=0):
    """Tux blinks while DHCP runs; dots count up on the right."""
    fb = tux_fb()
    face(fb, "blink" if phase % 4 == 3 else "ok")
    fb.text("DHCP", W - text_w("DHCP"), 10)
    for i in range(3):
        if i <= phase % 3:
            fb.rect(33 + i * 5, 19, 2, 2)
    return fb


def t_mini(d):
    """Mini Tux in the corner of the compact status screen."""
    fb = Fb()
    fb.bitmap(MINI_TUX, 0, 0)
    p = d["ip"].split(".")
    a, b = ".".join(p[:2]) + ".", ".".join(p[2:])
    fb.text(a, W - text_w(a), 0)
    fb.text(b, W - text_w(b), 6)
    fb.text("RAM", 0, 15)
    bar(fb, 14, 14, W - 14, 7, ram_frac(d))
    fb.text("SWP", 0, 24)
    bar(fb, 14, 23, W - 14, 7, swp_frac(d), "hatched")
    return fb


TUX_DESIGNS = [
    ("t0-splash", "Kernel splash today", t_splash),
    ("t1-says-ip", "Tux says the IP", t_says_ip),
    ("t2-belly", "Belly fills with RAM", t_belly),
    ("t3-mood-ok", "Mood: relaxed (RAM 35%)", lambda d: t_mood(d, 0.35, 0.0)),
    ("t4-mood-swap", "Mood: swapping (sweat)", lambda d: t_mood(d, 0.82, 0.6)),
    ("t5-mood-oom", "Mood: nearly OOM (dizzy)", lambda d: t_mood(d, 0.97, 0.9)),
    ("t6-dhcp", "DHCP: Tux blinks", lambda d: t_dhcp(d, 11)),
    ("t7-mini", "Mini Tux + IP + bars", t_mini),
]


DESIGNS = [
    ("1-netcard", "Net card: header, IP, hostname", d_netcard),
    ("2-bigip", "Big IP on two lines (4x7 font)", d_bigip),
    ("3-bars", "RAM / SWAP bars, load, uptime", d_bars),
    ("4-graph", "RAM history graph", d_graph),
    ("5-hero", "Hero number: RAM %", d_hero),
    ("6-ring", "Ring gauge + numbers", d_ring),
    ("7-combo", "Combo: IP + RAM + SWAP + load", d_combo),
    ("8-dhcp", "DHCP in progress (spinner)", lambda d: d_dhcp_wait(d, 0)),
    ("9-nolink", "No lease / no link", d_dhcp_fail),
]

DEFAULT = {
    "ip": "192.168.1.42", "host": "dreamcast",
    "mem_total": 11604, "mem_avail": 4412,
    "swap_total": 6144, "swap_used": 1180,
    "load": 0.42, "uptime": 754,
    "hist": [38, 38, 39, 41, 40, 40, 44, 52, 61, 66, 63, 58, 55, 55, 54,
             54, 57, 60, 64, 71, 79, 84, 86, 81, 72, 66, 64, 62, 61, 61,
             60, 60, 62, 63, 62, 61, 60, 61, 62, 62, 63, 62, 61, 62, 62],
}


# --------------------------------------------------------------------------
# Previews
# --------------------------------------------------------------------------

ON, OFF, BG = (24, 40, 28), (156, 186, 150), (120, 150, 118)


def preview(fb, scale=8):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (W * scale + 2 * scale, H * scale + 2 * scale), BG)
    dr = ImageDraw.Draw(img)
    for y in range(H):
        for x in range(W):
            c = ON if fb.get(x, y) else OFF
            x0, y0 = (x + 1) * scale, (y + 1) * scale
            dr.rectangle([x0, y0, x0 + scale - 2, y0 + scale - 2], fill=c)
    return img


def sheet(items, cols=3, scale=8):
    from PIL import Image, ImageDraw, ImageFont
    tiles = [(t, preview(fb, scale)) for t, fb in items]
    tw, th = tiles[0][1].size
    lab = 26
    rows = (len(tiles) + cols - 1) // cols
    img = Image.new("RGB", (cols * (tw + 16) + 16, rows * (th + lab + 16) + 16),
                    (32, 32, 36))
    dr = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 15)
    except OSError:
        font = ImageFont.load_default()
    for i, (t, tile) in enumerate(tiles):
        x = 16 + (i % cols) * (tw + 16)
        y = 16 + (i // cols) * (th + lab + 16)
        dr.text((x, y), t, fill=(230, 230, 230), font=font)
        img.paste(tile, (x, y + lab))
    return img


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    data = dict(DEFAULT)
    if "--data" in sys.argv:
        with open(sys.argv[sys.argv.index("--data") + 1]) as f:
            data.update(json.load(f))
    os.makedirs(out, exist_ok=True)
    items = []
    tux_items = []
    for name, title, fn in DESIGNS + TUX_DESIGNS:
        fb = fn(data)
        with open(os.path.join(out, name + ".raw"), "wb") as f:
            f.write(fb.to_bytes())
        preview(fb).save(os.path.join(out, name + ".png"))
        lst = tux_items if name.startswith("t") else items
        lst.append(("%s  %s" % (name.split("-")[0], title), fb))
    # spinner animation frames for the DHCP screen
    with open(os.path.join(out, "dhcp-anim.raw"), "wb") as f:
        for ph in range(8):
            f.write(d_dhcp_wait(data, ph).to_bytes())
    with open(os.path.join(out, "tux-dhcp-anim.raw"), "wb") as f:
        for ph in range(12):
            f.write(t_dhcp(data, ph).to_bytes())
    sheet(items).save(os.path.join(out, "sheet.png"))
    sheet(tux_items, cols=4).save(os.path.join(out, "tux-sheet.png"))
    print("wrote %d designs + sheet.png to %s" % (len(items), out))


if __name__ == "__main__":
    main()
