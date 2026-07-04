#!/usr/bin/env bash
# Collect device info from the DC over the serial console and drop it into the
# repo, ready to commit. Uses timestamp-free / stable commands so git diffs
# show real hardware & driver changes, not boot-to-boot noise.
#
# Requirements: DC booted into Linux, cable plugged in, minicom CLOSED.
#
# Usage:
#   ./collect-devinfo.sh                 # tag = the DC's hostname
#   ./collect-devinfo.sh --tag HKT-3020  # tag per physical console
#   ./collect-devinfo.sh --dest /some/dir --tag HKT-3020
#
# Output: <dest>/<tag>/{dmesg-t.txt, lspci-v.txt, manifest.txt, ...}
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
DC="python3 $HERE/dc.py"

DEST="$REPO/developer-info/device-info"
TAG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --dest) DEST="$2"; shift 2 ;;
    --tag)  TAG="$2";  shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# grab "<file>|<remote command>" - stable, timestamp-free where possible.
COLLECT=(
  # Prefer dmesg -t (untimestamped ring-buffer snapshot); if that's empty, fall
  # back to a short grab of /proc/kmsg (a blocking, draining stream, so bounded
  # by timeout) which can still hold messages when the dmesg buffer looks empty.
  "dmesg-t.txt|dmesg -t 2>/dev/null | grep -q . && dmesg -t 2>/dev/null || timeout 2 cat /proc/kmsg 2>/dev/null; true"
  "lspci-v.txt|lspci -v 2>&1 || echo '(lspci unavailable / no PCI)'"
  "cpuinfo.txt|cat /proc/cpuinfo"
  "meminfo.txt|grep -E 'MemTotal|SwapTotal' /proc/meminfo"
  "mtd.txt|cat /proc/mtd 2>/dev/null; mtdinfo 2>/dev/null"
  "input-devices.txt|for d in /sys/class/input/event*/device/name; do cat \$d; done 2>/dev/null"
  "cmdline.txt|cat /proc/cmdline"
)

echo "=== collect-devinfo  (port=${DC_PORT:-/dev/ttyUSB0} baud=${DC_BAUD:-115200}) ==="
if ! $DC ping >/dev/null 2>&1; then
  echo "FATAL: no live console. Boot the DC into Linux, plug the cable, close minicom." >&2
  exit 2
fi

# Default tag = device hostname, so multiple consoles don't clobber each other.
if [ -z "$TAG" ]; then
  TAG="$($DC run "uname -n" 2>/dev/null | tr -d '[:space:]')"
  [ -z "$TAG" ] && TAG="dreamcast"
fi

OUT="$DEST/$TAG"
mkdir -p "$OUT"
echo "writing to: $OUT"

for entry in "${COLLECT[@]}"; do
  file="${entry%%|*}"
  cmd="${entry#*|}"
  printf '  %-20s <- %s\n' "$file" "$cmd"
  if ! $DC run "$cmd" --timeout 60 > "$OUT/$file" 2>/dev/null; then
    echo "    (warning: '$cmd' returned non-zero; output still saved)"
  fi
done

# Manifest: stable identity only (no collection date -> clean git diffs).
$DC run "uname -a" --timeout 20 > "$OUT/manifest.txt" 2>/dev/null

echo
echo "done. Review and commit:"
echo "  git -C \"$REPO\" add \"${OUT#$REPO/}\" && git -C \"$REPO\" status --short \"${OUT#$REPO/}\""
