#!/usr/bin/env bash
# native-cc-test.sh - verify the DC's NATIVE toolchain: compile a hello-world
# on the device and run it. The program prints a COMPUTED value (6*7), so a
# pass proves the compiler actually built working code and it executed - not
# that a string got echoed.
#
# Source is shipped over serial as base64 (avoids all shell-quoting issues).
#
# Requirements: DC booted into Linux, cable in, minicom CLOSED, a native
# cc/gcc + libc headers on the image.
#
# Usage:
#   DC_PORT=/dev/ttyUSB1 ./native-cc-test.sh
#   ./native-cc-test.sh --cc gcc
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"
CC=""
CC_TIMEOUT=240        # native gcc on the SH4 takes ~2 min for hello world
while [ $# -gt 0 ]; do
  case "$1" in
    --cc) CC="$2"; shift 2 ;;
    --timeout) CC_TIMEOUT="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

pass=0; fail=0
ok() { echo "  PASS: $1"; pass=$((pass+1)); }
no() { echo "  FAIL: $1"; fail=$((fail+1)); }

WANT="HELLO_DC_42"
SRC='#include <stdio.h>
int main(void){ printf("HELLO_DC_%d\n", 6*7); return 0; }'
B64="$(printf '%s\n' "$SRC" | base64 -w0)"

echo "=== native-cc-test  (compile+run hello.c on the DC)  port=${DC_PORT:-/dev/ttyUSB1} ==="
$DC ping >/dev/null 2>&1 || { echo "FATAL: no live console (booted? cable in? minicom closed?)"; exit 2; }

# 1) find a native compiler ----------------------------------------------------
if [ -z "$CC" ]; then
  CC="$($DC run "command -v cc || command -v gcc" 2>/dev/null | tr -d '[:space:]')"
fi
if [ -n "$CC" ]; then ok "native compiler present ($CC)"; else
  no "no native cc/gcc on the image"
  echo "=== summary: $pass passed, $fail failed (can't compile) ==="; exit 1
fi

# 2) write the source on the device (base64 -> file) ---------------------------
if $DC run "echo $B64 | base64 -d > /tmp/hello.c && grep -q 'int main' /tmp/hello.c && echo WROTE" 2>/dev/null | grep -q WROTE; then
  ok "wrote /tmp/hello.c"
else
  no "failed to write source on device"; echo "=== summary: $pass passed, $fail failed ==="; exit 1
fi

# 3) compile -------------------------------------------------------------------
# No -O2: optimization is memory-hungry and the DC only has ~11 MB RAM + no
# swap, so -O2 OOMs and hangs the machine. Default (-O0) builds fine but is
# SLOW on the SH4 - a hello world takes ~2 minutes - so allow generous time.
echo "  ... compiling on the SH4 (this takes ~2 min, be patient) ..."
out="$($DC run "rm -f /tmp/hello; $CC -o /tmp/hello /tmp/hello.c 2>/tmp/cc.err; echo rc=\$?; [ -x /tmp/hello ] && echo BUILT; echo '----ERR----'; cat /tmp/cc.err" --timeout "$CC_TIMEOUT" 2>/dev/null)"
if printf '%s' "$out" | grep -q BUILT; then
  ok "compiled to /tmp/hello"
else
  no "compilation failed"
  printf '%s\n' "${out#*----ERR----}" | sed 's/^/        | /' | head -10
  echo "=== summary: $pass passed, $fail failed ==="; exit 1
fi

# 4) run it and check the computed output --------------------------------------
run="$($DC run "/tmp/hello; echo rc=\$?" 2>/dev/null)"
if printf '%s' "$run" | grep -q "$WANT"; then
  ok "ran and printed $WANT (compile+execute verified)"
else
  no "binary ran but output wrong (wanted $WANT)"
  printf '%s\n' "$run" | sed 's/^/        | /' | head -5
fi
if printf '%s' "$run" | grep -q "rc=0"; then ok "exited 0"; else no "non-zero exit"; fi

# cleanup ----------------------------------------------------------------------
$DC run "rm -f /tmp/hello /tmp/hello.c /tmp/cc.err" >/dev/null 2>&1

echo "=== summary: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
