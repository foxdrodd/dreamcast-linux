#!/usr/bin/env bash
# fbdoom-test.sh - verify fbdoom launches and runs on the DC framebuffer.
#
# "Started correctly" is checked four ways, strongest last:
#   1. pre-flight: fbdoom binary, the IWAD, and /dev/fb0 all exist.
#   2. startup log: WAD gets loaded, no fatal (I_Error / couldn't open ...).
#   3. liveness: the process is STILL RUNNING a few seconds after launch
#      (i.e. it didn't crash out during init) - the authoritative signal.
#   4. visual: grab the framebuffer so you (or an agent) can see the title/menu.
#
# It launches fbdoom backgrounded with stdin from /dev/null (so the shell job
# control doesn't SIGTTIN-stop it) and its output to a log on the DC, then
# inspects and cleans up.
#
# Requirements: DC booted into Linux, cable in, minicom CLOSED.
#
# Usage:
#   DC_PORT=/dev/ttyUSB1 ./fbdoom-test.sh
#   ./fbdoom-test.sh --wad /newdoom1.wad --secs 6
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"

WAD="/newdoom1.wad"
BIN="fbdoom"
SECS=6
while [ $# -gt 0 ]; do
  case "$1" in
    --wad)  WAD="$2";  shift 2 ;;
    --bin)  BIN="$2";  shift 2 ;;
    --secs) SECS="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

pass=0; fail=0
ok() { echo "  PASS: $1"; pass=$((pass+1)); }
no() { echo "  FAIL: $1"; fail=$((fail+1)); }

echo "=== fbdoom-test  ($BIN -iwad $WAD, run ${SECS}s)  port=${DC_PORT:-/dev/ttyUSB1} ==="
$DC ping >/dev/null 2>&1 || { echo "FATAL: no live console (booted? cable in? minicom closed?)"; exit 2; }

# 1) pre-flight -----------------------------------------------------------------
pre="$($DC run "command -v $BIN >/dev/null && echo BIN_OK; [ -f $WAD ] && echo WAD_OK; [ -e /dev/fb0 ] && echo FB_OK" 2>/dev/null)"
printf '%s' "$pre" | grep -q BIN_OK && ok "$BIN present" || { no "$BIN not found in PATH"; }
printf '%s' "$pre" | grep -q WAD_OK && ok "IWAD present ($WAD)" || no "IWAD missing: $WAD"
printf '%s' "$pre" | grep -q FB_OK  && ok "/dev/fb0 present" || no "/dev/fb0 missing"
if [ "$fail" -ne 0 ]; then
  echo "=== summary: $pass passed, $fail failed (aborting before launch) ==="
  exit 1
fi

# 2) launch backgrounded, log on the device ------------------------------------
LOGF="/tmp/fbdoom-test.log"
$DC run "pkill -9 $BIN 2>/dev/null; rm -f $LOGF; $BIN -iwad $WAD </dev/null >$LOGF 2>&1 & echo \$! >/tmp/fbdoom.pid; sleep 0.2; echo launched" >/dev/null 2>&1

# 3) liveness + startup log after SECS -----------------------------------------
info="$($DC run "sleep $SECS; if kill -0 \$(cat /tmp/fbdoom.pid 2>/dev/null) 2>/dev/null; then echo STILL_ALIVE; else echo EXITED; fi; echo '----LOG----'; head -60 $LOGF" --timeout $((SECS + 25)) 2>/dev/null)"

log="${info#*----LOG----}"
if printf '%s' "$info" | grep -q STILL_ALIVE; then
  ok "process still running after ${SECS}s (didn't crash during init)"
else
  no "process exited within ${SECS}s (crashed / failed to start)"
fi
if printf '%s' "$log" | grep -qiE 'W_Init|adding .*\.wad|IWAD'; then
  ok "WAD loaded (W_Init / adding $WAD)"
else
  no "no WAD-load evidence in startup log"
fi
if printf '%s' "$log" | grep -qiE 'I_Error|couldn.?t open|no such file|error:'; then
  no "fatal error in startup log"
  printf '%s\n' "$log" | grep -iE 'I_Error|couldn.?t|no such|error' | sed 's/^/        | /' | head -5
else
  ok "no fatal error in startup log"
fi

# 4) visual proof ---------------------------------------------------------------
if [ -x "$HERE/grab.sh" ]; then
  shot="$HERE/logs/fbdoom-$(date +%Y%m%d-%H%M%S).png"
  if "$HERE/grab.sh" "$shot" >/dev/null 2>&1; then
    echo "  screenshot saved: $shot  (open it / read it to confirm the title or demo is on screen)"
  else
    echo "  (screenshot failed - VGA capture connected & DC outputting video?)"
  fi
fi

# cleanup -----------------------------------------------------------------------
$DC run "kill \$(cat /tmp/fbdoom.pid 2>/dev/null) 2>/dev/null; sleep 1; pkill -9 $BIN 2>/dev/null; echo cleaned" >/dev/null 2>&1

echo "=== summary: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
