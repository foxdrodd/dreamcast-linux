# Dreamcast Linux — Release Verification Runbook

Reusable checklist for verifying a release. Work top to bottom. **Phase 1** is
automated (an agent can run it too — see `AGENTS.md`); **Phase 2** needs a human
at the console; **Phase 3** covers the physical/boot items from
`../release-test.sh` that can't be scripted; **Phase 4** records the results.

Copy this block per release and fill it in:

```
Release: ____________   Date: __________   Tester: __________
Variant:  ☐ base-busybox   ☐ uclibc   ☐ musl   ☐ muslX
DC model / adapter: _______________________   Kernel (uname -a): ______________
Result: ☐ PASS   ☐ PASS w/ notes   ☐ FAIL
```

---

## Variants to verify (from `../../scripts/release.sh`)

Each build produces the images below. The **userland** variants get the full
harness suite (Phases 1–2); **base-busybox** has only a minimal initramfs, so it
gets boot + console checks only. `<ver>` = `LINUX_VERSION` from
`dreamcast/build-dreamcast.sh`.

| Variant | CDI image | Also shipped | Notes |
|---------|-----------|--------------|-------|
| base-busybox | `linux-<ver>-base-busybox.cdi` | `kernel-boot.bin-busybox` (dcload-serial), `1ST_READ.BIN-busybox` (GDEMU) | busybox initramfs, no full userland → boot/console only |
| uclibc | `linux-<ver>-with-userland-uclibc.cdi` | `linux-<ver>-userland-uclibc.tar.zst` | uClibc userland — **no native gcc/cc** (skip native-cc test) |
| musl | `linux-<ver>-with-userland-musl.cdi` | `…-userland-musl.tar.zst` | musl userland |
| muslX | `linux-<ver>-with-userland-muslX.cdi` | `…-userland-muslX.tar.zst` | musl **+ X11** → also run the "mouse under X" check |

> Run Phases 1–2 once **per userland variant** (uclibc, musl, muslX) — reboot
> into each and re-run. A full release = all three pass **and** base-busybox
> boots. Copy the header block once per variant.

### Which variant is booted? (check this first)

The CDIs don't announce themselves — confirm what's actually running before you
decide which phases apply. Quick tells over serial (`dc.py run`):

- **base-busybox** — the giveaway is a *minimal initramfs, no full userland*:
  - `ls /lib` → **`No such file or directory`** (userland variants have `/lib`
    with a libc); `/usr/lib` is also absent.
  - `cat /etc/os-release` → **fails** (file doesn't exist).
  - `mount` shows `rootfs on / type rootfs (... size≈5096k)` and `/bin/sh` is a
    symlink to `busybox`; `ls /` includes `chroot.sh` + `linuxrc`.
  - No `fbdoom`, no native toolchain, no `/newdoom1.wad`.
  - → **boot/console checks only** (skip the fbdoom/native-cc/vmufat/fetch tiers;
    they need the userland and will just report "not found").
- **uclibc vs musl** — both have a real `/lib`; tell them apart by the dynamic
  loader: `ls /lib` shows `ld-uClibc*` (uclibc) vs `ld-musl-sh*.so.1` (musl).
- **muslX** — musl *plus* X11: the framebuffer X server **`Xfbdev`** is present
  (`ls /usr/bin/Xfbdev`). Note it's `Xfbdev`, **not** `/usr/bin/X` (which doesn't
  exist). No `Xfbdev` → plain **musl**, not muslX.

Decide with three *simple* `dc.py run` calls (it mangles `;`/`|`/`&&`, so keep
each command atomic — one program, no shell operators):
```sh
python3 dc.py run "ls /lib"             # fails → base-busybox; else look at the loader
python3 dc.py run "ls /lib"             #   ld-uClibc* → uclibc | ld-musl-sh* → musl/muslX
python3 dc.py run "ls /usr/bin/Xfbdev"  # succeeds → muslX (else plain musl)
```

---

## Phase 0 — Setup

- [ ] Fresh release image flashed / loaded, DC **booted into Linux**.
- [ ] Serial cable connected. Find the port: `ls -l /dev/ttyUSB*` (it is **not
      always ttyUSB0** — on this host it's `ttyUSB1`). Then:
      ```sh
      cd tests/harness
      export DC_PORT=/dev/ttyUSB1      # adjust to the port you found
      ```
- [ ] **minicom is CLOSED** (it holds the port exclusively; the harness can't
      share it).
- [ ] VGA capture dongle connected on `/dev/video0` (for visual checks).
- [ ] Peripherals plugged in for the maple tests: a controller, a mouse, a
      **VMU**, and a keyboard. Leave one port empty for the hot-plug test.
- [ ] Network adapter fitted and the DC on the LAN (for the iperf phase).
- [ ] Console is reachable: `python3 dc.py ping` → `ok: console alive`.

---

## Phase 1 — Automated checks (harness)

Run each; the expected good result is noted. All exit non-zero on failure.

- [ ] **Maple bus + VMU (hands-off tier)**
      ```sh
      ./maple-release-test.sh --auto-only
      ```
      Expect: input devices, `maple bus up`, `maple peripherals present` PASS;
      VMU either PASS (card in) or SKIP (no card). Exit 0.

- [ ] **fbdoom launches on the framebuffer**
      ```sh
      ./fbdoom-test.sh --wad /newdoom1.wad
      ```
      Expect: 6 PASS (binary/IWAD/fb0, still-running, WAD loaded, no fatal) and
      a screenshot in `logs/` showing the Doom HUD/demo.

- [ ] **Native toolchain — compile & run hello-world on the DC**
      ```sh
      ./native-cc-test.sh
      ```
      Expect: 5 PASS ending in `HELLO_DC_42`. Takes ~2 min (SH4 is slow); uses
      `-O0` (`-O2` OOMs the 11 MB box — recover with `dc.py sysrq b --wait 120`).
      **N/A on uclibc** — that userland ships no native `gcc`/`cc`, so the test
      just reports "no native cc/gcc on the image". Record it as SKIP for uclibc;
      run it on musl / muslX.

- [ ] **Networking throughput — per adapter, vs baseline**
      ```sh
      ./net-baseline-adapters.sh --host 192.168.0.225 --compare
      ```
      Expect: each fitted adapter within threshold of its baseline (no regression).
      (First release / new adapter: run without `--compare` to establish the
      baseline in `baselines/<tag>.iperf` and commit it.)

- [ ] **VMUFAT storage round-trip** (needs a **scratch** VMU inserted — erases it)
      ```sh
      ./vmufat-test.sh          # or --force to skip the wipe prompt
      ```
      Expect: 9 PASS (mkfs → mount → write → umount → remount → token persisted),
      or a clean SKIP if no VMU is inserted.

- [ ] **fastfetch / flashfetch output** (name, logo, sysinfo, maple list)
      ```sh
      ./fetch-test.sh
      ```
      Expect: 9 PASS (both tools show name/logo/sysinfo/Dreamcast Linux;
      flashfetch lists Maple devices).

- [ ] **Visual sanity of the console**
      ```sh
      ./grab.sh          # then open/read the PNG in logs/
      ```
      Expect: framebuffer console (boot logo + getty, or a live tty) on screen.

---

## Phase 2 — Guided / interactive (human at the console)

- [ ] **Full maple checklist** (walks Controller, Mouse, VMU, Hot-Plugging;
      prompts you and saves a screenshot at each visual step):
      ```sh
      GRAB=1 ./maple-release-test.sh
      ```
      Mirrors `../maple/TESTS.md`. Exit 0 = every prompted check confirmed.

- [ ] **Keyboard — by hand** (not in the harness: the console shell eats the
      keystrokes). At the DC console verify:
      - [ ] can type inside `htop`
      - [ ] can switch TTYs (Alt+Fn / `chvt`)
      - [ ] `Ctrl+C` interrupts a running command
      - [ ] mouse works under X (start X, move pointer, click) — **muslX variant only**

---

## Phase 3 — Manual release items (`../release-test.sh`)

Physical / boot / peripheral items the harness can't drive. Tick per release;
some only apply to specific DC/loader setups.

**Booting** (per the variants table above)
- [ ] base-busybox: `kernel-boot.bin-busybox` boots via dcload-serial
- [ ] base-busybox: `1ST_READ.BIN-busybox` boots via GDEMU
- [ ] base-busybox: `linux-<ver>-base-busybox.cdi` boots (GDEMU / CD-R)
- [ ] uclibc: `linux-<ver>-with-userland-uclibc.cdi` boots
- [ ] musl: `linux-<ver>-with-userland-musl.cdi` boots
- [ ] muslX: `linux-<ver>-with-userland-muslX.cdi` boots
- [ ] at least one CDI burns to a 700 MB CD-R and boots

**Consoles / video**
- [ ] video-mode selection works; something shows on the TV, not just serial
      (verify with `./grab.sh`)
- [ ] tiny-initrd boots
- [ ] switching consoles works
- [ ] a getty starts on serial **and** on the framebuffer console
- [ ] a getty still starts when serial is **not** plugged in

**Networking / tools** (console reachable, so drive with `dc.py run`/`expect`)
- [ ] NFS mount works; swap over NFS works
- [ ] `iperf3`, `netcat` present and work (throughput: Phase 1)
- [ ] `htop`, `btop`, `ircii` start
- [ ] `ssh` / `telnet` start
- [ ] `fbdoom` works in uclibc **and** musl (launch: Phase 1)
- [ ] `fastfetch` shows the custom logo and lists maple devices
- [ ] GD-ROM access works

**Audio / VMU**
- [ ] audio plays
- [ ] VMU hot-plug after boot; `mtd` info / raw read / raw write; default logo on
      the LCD; `text2lcd` works (mtd read is in Phase 1; write is manual)

---

## Phase 4 — Record & commit

- [ ] **Snapshot device info** into the repo:
      ```sh
      ./collect-devinfo.sh --tag <DC-model>      # e.g. HKT-3020
      ```
      Review the diff (timestamp-free, so it only changes on real change):
      `git -C ../.. status --short developer-info/device-info`
- [ ] **Commit** the device-info snapshot and any new/updated
      `baselines/*.iperf`. (Run logs and raw perf numbers stay uncommitted —
      `logs/` is git-ignored.)
- [ ] Fill in the header block at the top; note any FAILs / caveats.

---

## Notes for this setup (update if hardware changes)

- Serial console: **115200 baud**, port varies (`ttyUSB1` here). No login —
  press Enter, flashfetch, then mksh. `dc.py` handles it.
- iperf host (this PC): **192.168.0.225**. Adapters + tags: LAN Adapter →
  `lan-hkt-300`, BBA → `bba-hkt-400`.
- fbdoom is at `/usr/bin/fbdoom` (reports as "FDoom 0.1"), IWAD `/newdoom1.wad`,
  configs/saves under `/mnt/.fdoom.tar/`.
- `maple_test` on the DC: `/usr/bin/maple_test`.
- Kernel currently has no loadable modules (all built-in) and a working dmesg.
