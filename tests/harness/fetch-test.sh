#!/usr/bin/env bash
# fetch-test.sh - verify fastfetch and flashfetch output the expected pieces:
#   - name  (the user@host title, e.g. root@dreamcast)
#   - logo  (the custom ASCII-art logo)
#   - system info (OS / Kernel / CPU / Memory ...)
# flashfetch additionally lists the Maple peripherals.
#
# ANSI colour codes are stripped on the device before matching.
#
# Requirements: DC booted into Linux, cable in, minicom CLOSED.
#
# Usage:  DC_PORT=/dev/ttyUSB1 ./fetch-test.sh
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"

pass=0; fail=0; skip=0
ok() { echo "  PASS: $1"; pass=$((pass+1)); }
no() { echo "  FAIL: $1"; fail=$((fail+1)); }
sk() { echo "  SKIP: $1"; skip=$((skip+1)); }

echo "=== fetch-test  (fastfetch / flashfetch output)  port=${DC_PORT:-/dev/ttyUSB1} ==="
$DC ping >/dev/null 2>&1 || { echo "FATAL: no live console (booted? cable in? minicom closed?)"; exit 2; }

# check_fetch <tool> <want-maple:0|1>
check_fetch() {
  local tool="$1" want_maple="$2" out nf f
  if ! $DC run "command -v $tool" >/dev/null 2>&1; then
    sk "$tool not installed"; return
  fi
  echo "--- $tool ---"
  # capture once, ANSI stripped on the device
  out="$($DC run "$tool 2>&1 | sed 's/\\x1b\\[[0-9;]*m//g'" --timeout 60 2>/dev/null)"

  # name: the hostname appears in the user@host title line
  if grep -qi 'dreamcast' <<<"$out"; then ok "$tool: shows name (dreamcast)"; else
    no "$tool: name/hostname not found"; fi

  # logo: the custom ASCII art (runs of the swirl chars)
  if grep -qE '\*{3}|\+\*\+' <<<"$out"; then ok "$tool: shows logo (ASCII art)"; else
    no "$tool: no logo art detected"; fi

  # system info: require most of the key fields
  nf=0
  for f in 'OS:' 'Kernel' 'CPU' 'Memory'; do grep -q "$f" <<<"$out" && nf=$((nf+1)); done
  if [ "$nf" -ge 3 ]; then ok "$tool: shows system info ($nf/4 fields)"; else
    no "$tool: system info incomplete ($nf/4 fields)"; fi

  # it really is this distro on this arch
  if grep -qi 'Dreamcast Linux' <<<"$out"; then ok "$tool: OS = Dreamcast Linux"; else
    no "$tool: OS line missing 'Dreamcast Linux'"; fi

  # flashfetch: lists maple peripherals
  if [ "$want_maple" = 1 ]; then
    if grep -qi 'Maple' <<<"$out"; then ok "$tool: lists Maple devices"; else
      no "$tool: no Maple device listing"; fi
  fi
}

check_fetch fastfetch 0
echo
check_fetch flashfetch 1

echo "=== summary: $pass passed, $fail failed, $skip skipped ==="
[ "$fail" -eq 0 ]
