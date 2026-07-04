# AGENTS.md — Dreamcast serial test harness

Operating notes for an AI agent in a future session. This folder lets you drive
the **real Dreamcast** over its serial console: run shell commands, capture
output, assert, and collect pre-release info. Read this before touching the DC.

## What this is

```
you (Bash tool) → dc.py → pyserial → /dev/ttyUSB<N> → mksh shell on the DC
```

The DC runs Linux with a getty on the SH SCIF console, reached over the same
USB coder cable used for `dc-tool-ser`. `dc.py` reproduces the login (press
Enter → wait out flashfetch → mksh prompt) — **no username/password**.

## Before you can talk to the device

1. **The DC must be plugged in AND booted into Linux.** If there's no
   `/dev/ttyUSB*`, it isn't connected — do not fabricate output, tell the user.
2. **minicom (or any terminal) must be CLOSED.** Only one process can hold the
   serial port; `dc.py` opens it exclusively and errors clearly if it can't.
3. **Find the right port.** The DC does NOT always enumerate as `ttyUSB0`.
   On the known host `ttyUSB0` is a different `dialout` device and the DC came
   up as **`/dev/ttyUSB1`** (`plugdev`). Check `ls -l /dev/ttyUSB*` and pick the
   one that appeared when the cable was plugged in. Set `DC_PORT` accordingly.
4. **Baud is 115200** (default, live-confirmed). Only change `DC_BAUD` if the
   user says their minicom uses another rate.

```sh
export DC_PORT=/dev/ttyUSB1     # adjust per `ls -l /dev/ttyUSB*`
python3 dc.py ping              # expect: "ok: console alive"
```

## The tools

| script | purpose | needs a human? |
|--------|---------|----------------|
| `dc.py` | low-level driver: `ping` / `run` / `expect` / `capture` | no |
| `maple-release-test.sh` | maple suite, two tiers | `--auto-only`: no; full: yes |
| `collect-devinfo.sh` | snapshot `dmesg -t`, `lspci -v`, etc. into repo | no |
| `net-iperf-test.sh` | BBA throughput + baseline regression check | no (needs BBA up) |
| `grab.sh` | capture one video frame (VGA dongle) → PNG you can read | no |
| `native-cc-test.sh` | compile & run hello-world on the DC's native toolchain | no |
| `vmufat-test.sh` | VMUFAT mkfs/mount/write/remount round-trip (**erases the VMU**) | no |

**If the DC hangs** (e.g. an OOM from a heavy compile): `python3 dc.py sysrq b
--wait 120` sends a serial BREAK+b (magic SysRq reboot) and waits for the
console — no login needed. Note this box has ~11 MB RAM + no swap, so `gcc -O2`
OOMs; native builds must use `-O0` and are slow (~2 min for hello world).

### dc.py

```sh
python3 dc.py run     "uname -a"              # prints output, exit = remote $?
python3 dc.py expect  "ls /dev/input" event0  # exit 0 iff substring present
python3 dc.py capture "/usr/bin/maple_test" --seconds 6   # stream, then Ctrl-C
```
`run` uses a random sentinel marker (`echo MARK:$?`), so it doesn't depend on
the prompt string. `capture` is for streaming programs that never return.

### maple-release-test.sh

Mirrors `../maple/TESTS.md`. `[AUTO]` tier = enumeration / VMU mtd / dmesg,
run it yourself. `[HAND]` tier prompts the user to press buttons / (un)plug and
asserts the event or `dmesg` line appeared — **you can't press buttons, the
user must**. Runs are teed to `logs/maple-<ts>.log` (git-ignored).
`maple_test` lives at `/usr/bin/maple_test` on the DC (override `MT=`).

```sh
DC_PORT=/dev/ttyUSB1 ./maple-release-test.sh --auto-only   # you can run this
DC_PORT=/dev/ttyUSB1 ./maple-release-test.sh               # needs the user
```
Known caveat: `maple_test` events may not currently fire (see `../maple/TESTS.md`);
if the input tier is silent, that's the bug, not the harness.

### grab.sh — visual verification (you CAN see the screen)

There's a VGA capture dongle on `/dev/video0`. `./grab.sh [out.png]` grabs one
frame; then **Read the PNG** to actually check what's on the monitor. Use this
to verify the things serial can't show: framebuffer console / boot logo, X
running, fbdoom, video-mode selection, console switching, "something on screen
not just serial". Live-confirmed: a grab shows the SuperH Tux logo + getty
"Please press Enter to activate this console" (with analog speckle from the
cheap dongle — normal). Prefer this over asking the user to self-report a visual
check. `maple-release-test.sh GRAB=1 …` auto-saves a shot at each visual step.

### collect-devinfo.sh

Writes to `../../developer-info/device-info/<tag>/`. Commands are
timestamp-free on purpose so committed files only diff on real change. Tag
defaults to the DC hostname; pass `--tag HKT-3020` per physical console.

### net-iperf-test.sh

Host runs `iperf3 -s` (script manages it), DC runs `iperf3 -c`. Needs
`--host <ip the DC uses to reach this PC>`. Compares TX/RX against
`baselines/<tag>.iperf` (committed); establish with `--save-baseline`, fails on
a drop past `--thresh`% (default 10). Pings the host from the DC first and
aborts if the BBA is down.

## Conventions in this repo

- **Commit** baselines and `device-info/` (stable, diff-meaningful).
- **Don't commit** run logs or raw perf numbers (volatile; `logs/` is
  git-ignored, the perf threshold absorbs run-to-run wobble).
- When something can't be verified against hardware (device unplugged), say so
  plainly — never invent device output.

## Verified

First live run (2026-07-04) succeeded at 115200 on `/dev/ttyUSB1`:
`Linux dreamcast 7.1.0-rc6 #18 PREEMPT ... sh4 GNU/Linux`.
