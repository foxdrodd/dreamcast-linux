# Dreamcast Linux — Release Verification Runbook

Reusable checklist for verifying a release. Work top to bottom. **Phase 1** is
automated (an agent can run it too — see `AGENTS.md`); **Phase 2** needs a human
at the console; **Phase 3** covers the physical/boot items from
`../release-test.sh` that can't be scripted; **Phase 4** records the results.

Copy this block per release and fill it in:

```
Release: ____________   Date: __________   Tester: __________
DC model / adapter: _______________________   Kernel (uname -a): ______________
Result: ☐ PASS   ☐ PASS w/ notes   ☐ FAIL
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

- [ ] **Networking throughput — per adapter, vs baseline**
      ```sh
      ./net-baseline-adapters.sh --host 192.168.0.225 --compare
      ```
      Expect: each fitted adapter within threshold of its baseline (no regression).
      (First release / new adapter: run without `--compare` to establish the
      baseline in `baselines/<tag>.iperf` and commit it.)

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
      - [ ] mouse works under X (start X, move pointer, click)

---

## Phase 3 — Manual release items (`../release-test.sh`)

Physical / boot / peripheral items the harness can't drive. Tick per release;
some only apply to specific DC/loader setups.

**Booting**
- [ ] `kernel-boot.bin` boots via dcload-serial / GDEMU
- [ ] busybox variant boots via dcload-serial / GDEMU
- [ ] all CDI variants boot (uclibc, musl, busybox)
- [ ] burns to a 700 MB CD-R and boots

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
