#!/usr/bin/env bash
# grab.sh - capture ONE frame of the DC's video output (VGA capture dongle on
# /dev/video0) to a PNG, for visual verification. An agent can then read the
# PNG; a human can eyeball it. Complements the serial harness: serial proves
# the shell works, this proves what's actually on the TV/monitor.
#
# The dongle is a 640x480 MJPEG capture (its only mode - no higher res or raw
# format exists, so no player/tool can get sharper source pixels). Its one
# real quality problem is per-frame analog speckle, which we kill by AVERAGING
# several frames (temporal denoise) - this yields a cleaner still than mpv/ffplay
# ever show, because those only improve *display* scaling, not the noise.
# Averaging also covers warmup (the first frames sync/tear and wash out).
#
# Config via env:
#   VIDEO_DEV   capture device       (default /dev/video0)
#   VIDEO_SIZE  frame size           (default 640x480)
#   VIDEO_AVG   frames to average    (default 16; set 1 to disable denoise)
#
# Usage:
#   ./grab.sh                     # -> logs/screen-<timestamp>.png (path printed)
#   ./grab.sh /path/to/shot.png   # explicit output
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DEV="${VIDEO_DEV:-/dev/video0}"
SIZE="${VIDEO_SIZE:-640x480}"
AVG="${VIDEO_AVG:-16}"
[ "$AVG" -ge 1 ] 2>/dev/null || AVG=16

OUT="${1:-}"
if [ -z "$OUT" ]; then
  mkdir -p "$HERE/logs"
  OUT="$HERE/logs/screen-$(date +%Y%m%d-%H%M%S).png"
fi

[ -e "$DEV" ] || { echo "grab.sh: no capture device at $DEV" >&2; exit 2; }
command -v ffmpeg >/dev/null || { echo "grab.sh: ffmpeg not found" >&2; exit 2; }

# Read AVG+4 frames; tmix outputs the running average of the last AVG frames,
# and -update 1 keeps overwriting so the file ends up holding the denoised,
# fully-synced result. AVG=1 => tmix is a no-op passthrough (single frame).
if ! ffmpeg -hide_banner -loglevel error \
     -f v4l2 -input_format mjpeg -video_size "$SIZE" -i "$DEV" \
     -vf "tmix=frames=$AVG" -frames:v "$((AVG + 4))" -update 1 -y "$OUT" 2>/dev/null; then
  echo "grab.sh: capture failed (is the dongle connected and the DC outputting video?)" >&2
  exit 3
fi

echo "$OUT"
