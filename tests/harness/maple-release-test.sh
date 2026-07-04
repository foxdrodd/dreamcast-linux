#!/usr/bin/env bash
# Maple pre-release test harness, driven over the serial console via dc.py.
#
# Two tiers (see tests/maple/TESTS.md):
#   [AUTO] checks run with no human input - enumeration, VMU mtd, dmesg.
#   [HAND] hands-on checks - the script tells you which button to press and
#          asserts the event actually showed up in maple_test output.
#
# Prereqs:
#   - DC booted into Linux, serial cable plugged in, minicom CLOSED.
#   - maple_test present on the DC (default: /root/maple_test; override w/ MT).
#
# Usage:
#   DC_PORT=/dev/ttyUSB0 DC_BAUD=115200 ./maple-release-test.sh
#   ./maple-release-test.sh --auto-only     # skip the hands-on tier
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"
MT="${MT:-/usr/bin/maple_test}"        # path to maple_test on the DC
AUTO_ONLY=0
[ "${1:-}" = "--auto-only" ] && AUTO_ONLY=1

# Log the whole run to a timestamped file (volatile -> not committed).
LOGDIR="${MAPLE_LOGDIR:-$HERE/logs}"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/maple-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
echo "# maple-release-test  $(date '+%Y-%m-%d %H:%M:%S')"

pass=0; fail=0; skip=0
ok()   { echo "  PASS: $1"; pass=$((pass+1)); }
no()   { echo "  FAIL: $1"; fail=$((fail+1)); }
sk()   { echo "  SKIP: $1"; skip=$((skip+1)); }

# auto_expect "<label>" "<remote cmd>" "<substring that must appear>"
auto_expect() {
  local label="$1" cmd="$2" want="$3" out
  if out="$($DC run "$cmd" 2>/dev/null)"; then :; fi
  if printf '%s' "$out" | grep -qF -- "$want"; then ok "$label"; else
    no "$label (wanted '$want')"; [ -n "$out" ] && echo "        got: $(printf '%s' "$out" | head -1)"
  fi
}

# hands_on "<prompt>" "<ERE maple_test must emit>" [seconds]
hands_on() {
  local prompt="$1" want="$2" secs="${3:-6}" out
  [ "$AUTO_ONLY" = 1 ] && { sk "$prompt (hands-on skipped)"; return; }
  echo
  echo ">> $prompt"
  read -r -p "   Press ENTER, then do it within ${secs}s... " _
  out="$($DC capture "$MT" --seconds "$secs" 2>/dev/null)"
  if printf '%s' "$out" | grep -qiE -- "$want"; then ok "$prompt"; else
    no "$prompt (expected an event matching /$want/)"
    echo "        maple_test output was:"; printf '%s\n' "$out" | sed 's/^/        | /' | head -20
  fi
}

# hands_on_dmesg "<prompt>" "<ERE dmesg must gain>"  - for physical (un)plugging,
# which maple_test's already-open fds never see; the kernel log does.
hands_on_dmesg() {
  local prompt="$1" want="$2" out
  [ "$AUTO_ONLY" = 1 ] && { sk "$prompt (hands-on skipped)"; return; }
  echo
  echo ">> $prompt"
  $DC run "dmesg -c >/dev/null 2>&1 || dmesg >/dev/null" >/dev/null 2>&1
  read -r -p "   Clear the log is done. Press ENTER, do it, wait 3s, ENTER again... " _
  out="$($DC run "dmesg | grep -iE 'maple' | tail -20" 2>/dev/null)"
  if printf '%s' "$out" | grep -qiE -- "$want"; then ok "$prompt"; else
    no "$prompt (expected dmesg line matching /$want/)"
    printf '%s\n' "$out" | sed 's/^/        | /' | head -20
  fi
}

# manual_confirm "<instruction>"  - for visual/interactive checks with no
# serial-measurable output (X, TTY switch, Ctrl+C...). User self-reports.
# With GRAB=1 (and a VGA capture dongle), it also saves a screenshot next to
# the log so the visual state is recorded / an agent can read it back.
manual_confirm() {
  local prompt="$1" ans shot
  [ "$AUTO_ONLY" = 1 ] && { sk "$prompt (manual, skipped)"; return; }
  echo
  echo ">> $prompt"
  if [ "${GRAB:-0}" = 1 ]; then
    shot="$(dirname "$LOG")/shot-$(date +%H%M%S).png"
    if "$HERE/grab.sh" "$shot" >/dev/null 2>&1; then
      echo "   screenshot: $shot"
    else
      echo "   (screenshot failed - capture dongle connected & DC outputting video?)"
    fi
  fi
  read -r -p "   Pass? [y/N] " ans
  case "$ans" in [yY]*) ok "$prompt" ;; *) no "$prompt" ;; esac
}

echo "=== Maple release test  (port=${DC_PORT:-/dev/ttyUSB0} baud=${DC_BAUD:-115200}) ==="
if ! $DC ping >/dev/null 2>&1; then
  echo "FATAL: no live console. Boot the DC into Linux, plug the cable, close minicom."
  exit 2
fi
echo "console: alive"

echo
echo "--- [AUTO] Enumeration ---"
# Check /sys, not dmesg: the kernel ring buffer drops boot messages over time,
# but /sys/bus/maple reflects the live bus state regardless of uptime.
auto_expect "input devices exist"          "ls /dev/input/"                  "event"
auto_expect "maple bus up (drivers bound)" "ls /sys/bus/maple/drivers/ 2>&1" "Dreamcast"
auto_expect "maple peripherals present"    "ls /sys/bus/maple/devices/ 2>&1" ":00."

echo
echo "--- [AUTO] VMU (mtd) ---"
if $DC expect "cat /proc/mtd" "mtd0" >/dev/null 2>&1; then
  auto_expect "mtdinfo works"                 "mtdinfo 2>&1 | head" "mtd0"
  # Verify the read really produced 512 bytes - don't blindly 'echo done'.
  auto_expect "mtd raw read (512B, verified)" \
    "mtd_debug read /dev/mtd0 0 512 /tmp/vmu.bin >/dev/null 2>&1 && wc -c < /tmp/vmu.bin" "512"
else
  sk "VMU mtd tests (no VMU plugged in - insert one to exercise)"
fi

if [ "$AUTO_ONLY" = 0 ]; then
  echo
  echo "########  Guided maple checklist (mirrors ../maple/TESTS.md)  ########"

  # --- Controller (TESTS.md > Controller) ---
  echo
  echo "--- [HAND] Controller ---"
  hands_on "CONTROLLER: press any button / move stick / d-pad / pull a trigger" \
           "(pressed|D-pad (LEFT|RIGHT|UP|DOWN)|stick[0-9]? [XY]=|(L|R)-trigger=)" 6

  # --- Mouse (TESTS.md > Mouse) ---
  echo
  echo "--- [HAND] Mouse ---"
  hands_on "MOUSE: click a button, move it, or spin the wheel" \
           "(M-(LEFT|RIGHT|MIDDLE)|mouse (d[xy]|wheel)=)" 6
  manual_confirm "MOUSE works under X (start X, move the pointer, click)"

  # Keyboard is intentionally NOT tested here: the maple keyboard's keystrokes
  # are captured by the active console shell, so maple_test can't observe them
  # cleanly over serial. Verify the keyboard by hand (type in htop, switch TTYs,
  # Ctrl+C) at the console instead.

  # --- VMU (TESTS.md > VMU) ---
  echo
  echo "--- [HAND] VMU ---"
  echo "   (mtd presence + raw read already checked in the [AUTO] tier above)"
  hands_on_dmesg "VMU: plug in a NEW VMU card - does it get detected?" \
                 "attach|devinfo|new (device|maple)"
  manual_confirm "VMU: 'mtd_debug write' works (run manually - WRITES to flash, use a scratch card)"

  # --- Hot Plugging (TESTS.md > Hot Plugging) ---
  echo
  echo "--- [HAND] Hot Plugging ---"
  manual_confirm "HOTPLUG: with one port left EMPTY, the other devices still work"
  hands_on_dmesg "HOTPLUG: plug a NEW device into an empty port"  "attach|devinfo|new (device|maple)"
  hands_on_dmesg "HOTPLUG: unplug a device from a used port"      "detach|removed|empty"
  manual_confirm "HOTPLUG: all peripherals still work after plugging in/out"
fi

echo
echo "=== summary: $pass passed, $fail failed, $skip skipped ==="
echo "log: $LOG"
[ "$fail" -eq 0 ]
