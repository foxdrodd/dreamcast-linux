# Serial-console test harness

Lets a script drive the Dreamcast Linux console over the serial
cable non-interactively: send a command, capture output, assert on it.

```
me / a script  →  dc.py  →  pyserial  →  /dev/ttyUSB0  →  mksh on the DC
```

## Requirements

- The DC booted **into Linux** with the serial cable plugged in.
- **minicom closed.** Only one process can hold the serial port; the harness
  opens it exclusively and will tell you if minicom still has it.
- `pyserial` on the host (you already have 3.5).
- For the maple tier: `maple_test` built for SH4 and copied onto the DC
  (default path `/root/maple_test`, override with `MT=`).

## Config

| var       | default          | meaning                         |
|-----------|------------------|---------------------------------|
| `DC_PORT` | `/dev/ttyUSB0`   | serial device                   |
| `DC_BAUD` | `115200`         | console baud (confirm vs minicom) |

> If your minicom session uses a different baud than 115200, set `DC_BAUD` to
> match — the getty won't answer at the wrong rate.

## dc.py — the low-level driver

```sh
./dc.py ping                              # is the console alive?
./dc.py run    "cat /proc/version"        # prints output, exit = remote $?
./dc.py expect "ls /dev/input" "event0"   # exit 0 iff substring present
./dc.py capture "./maple_test" --seconds 6   # stream a program, then Ctrl-C it
```

No login/password needed: it presses Enter, waits out flashfetch, and confirms
a live mksh shell before running anything. Command capture uses a random
sentinel marker, so it doesn't depend on the exact prompt string.

**Recovery:** if the DC hangs (e.g. an OOM), `dc.py sysrq` sends a serial BREAK
+ key (magic SysRq) — no login needed — to get it back:
```sh
python3 dc.py sysrq b --wait 120   # BREAK+b = reboot, then wait for the console
```

## maple-release-test.sh — the two-tier maple suite

Mirrors `tests/maple/TESTS.md`.

```sh
DC_PORT=/dev/ttyUSB0 ./maple-release-test.sh     # full run
./maple-release-test.sh --auto-only              # hands-off checks only
```

- **[AUTO]** — no human input: input-device enumeration, maple driver bind,
  VMU `mtd` presence + raw read (timeout check), hotplug traces in `dmesg`.
- **[HAND]** — a guided checklist that walks every `../maple/TESTS.md` section
  (Controller, Mouse, Keyboard, VMU, Hot Plugging) in order. Three kinds of step:
  - **asserted event** — you do a thing, harness captures `maple_test` output and
    checks the event fired (controller/mouse/keyboard).
  - **asserted dmesg** — physical (un)plugging, watched via `dmesg` (not
    `maple_test`, whose fds are already open).
  - **manual confirm** — visual/interactive checks with no serial-measurable
    output (mouse under X, `mtd_debug write`, "other devices still work with a
    port empty"). You self-report `y/N`; empty answer = fail (safe default).

  Keyboard is intentionally **not** covered here — the maple keyboard's
  keystrokes are captured by the active console shell, so `maple_test` can't
  observe them over serial. Test it by hand at the console (type in htop, switch
  TTYs, Ctrl+C).

Every run is teed to `logs/maple-<timestamp>.log` (path printed at the end).
Logs are git-ignored; set `MAPLE_LOGDIR=…` to collect them elsewhere.

> Caveat: `tests/maple/TESTS.md` notes events may not currently fire inside
> `maple_test`. If the [HAND] input tier reports nothing, that's the known
> issue, not the harness — verify with `evtest`/`cat /dev/input/eventN` too.

## grab.sh — see the DC's screen (VGA capture)

Serial proves the *shell* works; a VGA capture dongle on `/dev/video0` proves
what's actually **on the monitor**. `grab.sh` captures one frame to a PNG that
an agent can read back or a human can eyeball — so visual checks (framebuffer
console, boot logo, X, fbdoom, video-mode selection, console switching) can be
verified, not just self-reported.

```sh
./grab.sh                     # -> logs/screen-<timestamp>.png (prints the path)
./grab.sh /tmp/x.png          # explicit output
VIDEO_DEV=/dev/video1 ./grab.sh
```

640x480 MJPEG is the dongle's only mode, so that's the resolution ceiling for
any tool (mpv only *looked* sharper via display scaling — it can't add source
pixels). What actually helps a still is killing the analog speckle, so `grab.sh`
**averages 16 frames** by default (temporal denoise) — cleaner than any live
player shows. Config: `VIDEO_DEV` (`/dev/video0`), `VIDEO_SIZE` (`640x480`),
`VIDEO_AVG` (`16`; set `1` to disable denoise). ~1.5 s per grab.

Run `maple-release-test.sh` with **`GRAB=1`** to auto-save a screenshot beside
the log at each manual visual check.

## fbdoom-test.sh — verify fbdoom launches on the framebuffer

Checks `fbdoom -iwad /newdoom1.wad` actually starts, four ways (strongest last):
pre-flight (binary/IWAD/`/dev/fb0` exist) → startup log (WAD loaded, no fatal) →
**liveness** (process still running a few seconds later, i.e. didn't crash) →
**visual** (grabs the framebuffer so you/an agent can see the title or demo).

```sh
DC_PORT=/dev/ttyUSB1 ./fbdoom-test.sh
./fbdoom-test.sh --wad /newdoom1.wad --secs 6
```

Launches fbdoom backgrounded (stdin from `/dev/null`, log on the DC), inspects,
then kills it to restore the console. Screenshot lands in `logs/`.

## native-cc-test.sh — compile & run hello-world on the DC

Verifies the DC's **native** toolchain: writes a hello.c on the device (shipped
as base64 to dodge serial-quoting), compiles it with `cc`/`gcc`, runs it, and
checks it printed a **computed** value (`6*7` → `HELLO_DC_42`) — so a pass means
real compile + execute, not an echoed string.

```sh
DC_PORT=/dev/ttyUSB1 ./native-cc-test.sh
./native-cc-test.sh --cc gcc --timeout 300
```

Two facts about this hardware baked in:
- **No `-O2`** — the DC has ~11 MB RAM and no swap, so optimization OOMs and
  hangs the box (recover with `dc.py sysrq b --wait 120`). Default `-O0` is fine.
- **Slow** — a hello world takes **~2 min** on the SH4, so the compile step
  allows 240 s by default (`--timeout` to change).

## vmufat-test.sh — VMUFAT filesystem round-trip on a VMU

Verifies the full VMU storage path: `mkfs.vmufat -s` → mount → write a file
containing a fresh random token → `sync` → umount → **remount → confirm the
token survived**. A pass proves the data really persisted across the
mount cycle, not that a stale file was left behind.

```sh
DC_PORT=/dev/ttyUSB1 ./vmufat-test.sh          # prompts before wiping
./vmufat-test.sh --force                        # no prompt (suites)
./vmufat-test.sh --dev /dev/mtdblock0 --file TESTFILE
```

**DESTRUCTIVE** — `mkfs.vmufat` erases the inserted VMU, so it asks for
confirmation unless `--force`. Use a **scratch card**. SKIPs cleanly if no VMU
is inserted (`mtd0` absent).

## collect-devinfo.sh — snapshot device info into the repo

Pulls `dmesg -t`, `lspci -v`, and a few other stable facts off the DC and
writes them under `developer-info/device-info/<tag>/`, ready to commit.

```sh
./collect-devinfo.sh                 # tag = the DC's hostname
./collect-devinfo.sh --tag HKT-3020  # one folder per physical console
```

Commands are deliberately **timestamp-free** (`dmesg -t`, not `dmesg`), so a
re-collect only diffs when the hardware/driver picture actually changed — the
git history becomes a meaningful log of driver behaviour across releases.
`dmesg-t.txt` falls back to a short (`timeout`-bounded) `/proc/kmsg` grab when
the `dmesg` ring buffer is empty — note `/proc/kmsg` is a draining stream, so it
only yields messages not already consumed by a logger, not the full history.
Files: `dmesg-t.txt`, `lspci-v.txt`, `cpuinfo.txt`, `meminfo.txt`,
`mtd.txt`, `input-devices.txt`, `cmdline.txt`, `manifest.txt`.

Wire it into a release (after the DC is booted with the new build):

```sh
# in release.sh, once the freshly built image is running on the DC:
tests/harness/collect-devinfo.sh --tag HKT-3020
git add developer-info/device-info && git commit -m "devinfo: <release>"
```

> `lspci` on the SH4 DC will usually report nothing (no PCI bus) — the script
> keeps the file with a clear placeholder rather than failing.

## net-iperf-test.sh — throughput + regression check

Measures broadband-adapter throughput both ways and compares against a
committed baseline, so a release that regresses networking fails loudly.

Topology: your **host runs `iperf3 -s`** (the script starts/stops it for you);
the **DC runs `iperf3 -c`** as the client over its BBA, so the DC's CPU is what's
being measured.

```sh
./net-iperf-test.sh --host 192.168.1.50                 # host IP as the DC sees it
./net-iperf-test.sh --host 192.168.1.50 --save-baseline # establish the baseline
./net-iperf-test.sh --host 192.168.1.50 --thresh 15 --log net-history.csv
```

- `TX` = DC → host, `RX` = host → DC.
- Baselines live in `tests/harness/baselines/<tag>.iperf` (commit them). First
  run has none → use `--save-baseline`.
- Fails (exit 1) if either direction drops more than `--thresh`% (default 10%)
  below baseline. `--log FILE` appends a CSV row for trend tracking.
- Pre-flight: it pings the host from the DC first and aborts with a clear
  message if the BBA/routing isn't up, so a network fault doesn't masquerade
  as a perf regression.

> Only the *baseline* is meant to be committed. Run-to-run numbers wobble a few
> %, so don't commit raw results — that's what the threshold absorbs.

### Multiple adapters (LAN Adapter vs BBA)

`net-baseline-adapters.sh` walks you through each physical network adapter,
pausing so you can swap hardware + reboot, then baselines each under its own tag
(`lan-hkt-300`, `bba-hkt-400`). Edit the `ADAPTERS=(...)` list to match what you
own.

```sh
./net-baseline-adapters.sh --host 192.168.0.225            # establish both baselines
./net-baseline-adapters.sh --host 192.168.0.225 --compare  # release-time: compare both
./net-baseline-adapters.sh --host 192.168.0.225 --only bba-hkt-400
```

Extra args (`--dur`, `--thresh`, `--log`) pass through to `net-iperf-test.sh`.

## Extending

Write more `.sh` suites against `dc.py` for the rest of `release-test.sh`
(networking, GDROM, audio, consoles). Pattern:

```sh
DC="python3 $(dirname "$0")/dc.py"
$DC expect "iperf3 -v" "iperf 3" && echo "iperf present"
```
