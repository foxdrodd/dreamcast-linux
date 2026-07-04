#!/usr/bin/env bash
# vmufat-test.sh - VMUFAT round-trip on a real VMU:
#   mkfs.vmufat -> mount -> write a file -> sync -> umount -> remount -> verify.
# The file holds a fresh random token, so a pass proves the data actually
# persisted through the umount/remount cycle (not a stale leftover).
#
# !! DESTRUCTIVE !! mkfs.vmufat ERASES the VMU. Use a scratch card. The script
# asks for confirmation unless you pass --force.
#
# Requirements: DC booted into Linux, cable in, minicom CLOSED, a VMU inserted.
#
# Usage:
#   DC_PORT=/dev/ttyUSB1 ./vmufat-test.sh
#   ./vmufat-test.sh --force                 # no confirmation prompt (suites)
#   ./vmufat-test.sh --dev /dev/mtdblock0 --file TESTFILE
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"

DEV="/dev/mtdblock0"
MNT="/mnt/vmu"
FILE="TESTFILE"          # <=12 chars for SEGA VMU compatibility
FORCE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --dev)  DEV="$2";  shift 2 ;;
    --mnt)  MNT="$2";  shift 2 ;;
    --file) FILE="$2"; shift 2 ;;
    --force) FORCE=1;  shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

pass=0; fail=0
ok() { echo "  PASS: $1"; pass=$((pass+1)); }
no() { echo "  FAIL: $1"; fail=$((fail+1)); }
# step "<label>" "<remote cmd>" "<substring expected in output>"
step() {
  local label="$1" cmd="$2" want="$3" out
  out="$($DC run "$cmd" --timeout 60 2>/dev/null)"
  if printf '%s' "$out" | grep -qF -- "$want"; then ok "$label"; else
    no "$label"; [ -n "$out" ] && printf '%s\n' "$out" | sed 's/^/        | /' | head -5
  fi
}

TOKEN="VMUFAT_OK_$$_$RANDOM"

echo "=== vmufat-test  (mkfs+mount+write+remount+verify on $DEV)  port=${DC_PORT:-/dev/ttyUSB1} ==="
$DC ping >/dev/null 2>&1 || { echo "FATAL: no live console (booted? cable in? minicom closed?)"; exit 2; }

# pre-check: VMU present + vmufat supported --------------------------------------
pre="$($DC run "grep -q '^mtd0' /proc/mtd && echo MTD_OK; grep -qw vmufat /proc/filesystems && echo FS_OK; command -v mkfs.vmufat >/dev/null && echo MKFS_OK" 2>/dev/null)"
if ! printf '%s' "$pre" | grep -q MTD_OK; then
  echo "  SKIP: no VMU detected (mtd0 absent - insert a VMU and re-run)"
  echo "=== summary: 0 passed, 0 failed, skipped (no VMU) ==="; exit 0
fi
printf '%s' "$pre" | grep -q FS_OK   && ok "kernel supports vmufat" || no "vmufat not in /proc/filesystems"
printf '%s' "$pre" | grep -q MKFS_OK && ok "mkfs.vmufat present"    || no "mkfs.vmufat missing"
[ "$fail" -ne 0 ] && { echo "=== summary: $pass passed, $fail failed ==="; exit 1; }

# destructive confirmation ------------------------------------------------------
if [ "$FORCE" != 1 ]; then
  echo
  echo "  !! This will ERASE the inserted VMU ($DEV) with mkfs.vmufat."
  read -r -p "  Continue? [y/N] " ans
  case "$ans" in [yY]*) : ;; *) echo "  aborted (no changes made)."; exit 0 ;; esac
fi

# make sure it's not already mounted --------------------------------------------
$DC run "umount $MNT 2>/dev/null; mkdir -p $MNT" >/dev/null 2>&1

# round-trip --------------------------------------------------------------------
step "mkfs.vmufat"          "mkfs.vmufat -s $DEV >/tmp/mkfs.log 2>&1 && echo MKFS_DONE" "MKFS_DONE"
step "mount vmufat"         "mount -t vmufat $DEV $MNT && echo MOUNTED" "MOUNTED"
step "write file + sync"    "printf '%s\\n' '$TOKEN' > $MNT/$FILE && sync && echo WROTE" "WROTE"
step "file listed on mount" "ls $MNT" "$FILE"
step "umount"               "umount $MNT && echo UMOUNTED" "UMOUNTED"
step "remount"              "mount -t vmufat $DEV $MNT && echo REMOUNTED" "REMOUNTED"
# the key assertion: the token survived the umount/remount cycle.
# tr -d strips the VMUFAT block null-padding so no null bytes cross the serial
# link (otherwise the shell warns "ignored null byte").
step "file persisted w/ correct content after remount" "tr -d '\\000' < $MNT/$FILE" "$TOKEN"

# cleanup -----------------------------------------------------------------------
$DC run "umount $MNT 2>/dev/null; echo cleaned" >/dev/null 2>&1

echo "=== summary: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
