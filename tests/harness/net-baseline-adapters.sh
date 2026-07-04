#!/usr/bin/env bash
# Collect (or compare) iperf baselines for each Dreamcast network adapter,
# guiding you through the manual swap between them. Swapping adapters needs a
# reboot, so this pauses before each and you press ENTER once the DC is booted
# and on the network.
#
# Each adapter gets its own baseline file (baselines/<tag>.iperf) via the
# existing net-iperf-test.sh --tag machinery.
#
# Usage:
#   ./net-baseline-adapters.sh --host 192.168.0.225                # save baselines
#   ./net-baseline-adapters.sh --host 192.168.0.225 --compare      # compare vs saved
#   ./net-baseline-adapters.sh --host 192.168.0.225 --only bba-hkt-400
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
IPERF="$HERE/net-iperf-test.sh"

HOST="${DC_IPERF_HOST:-}"
MODE_ARG="--save-baseline"     # default: establish baselines
ONLY=""
EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    --host) HOST="$2"; shift 2 ;;
    --compare) MODE_ARG=""; shift ;;      # omit --save-baseline -> compare mode
    --only) ONLY="$2"; shift 2 ;;         # run just one adapter by tag
    *) EXTRA+=("$1"); shift ;;            # pass through --dur/--thresh/--log etc.
  esac
done
[ -z "$HOST" ] && { echo "error: need --host <ip the DC uses to reach this PC>" >&2; exit 2; }

# label|tag  - one entry per physical adapter you own.
ADAPTERS=(
  "LAN Adapter (HKT-300)|lan-hkt-300"
  "Broadband Adapter / BBA (HKT-400)|bba-hkt-400"
)

[ -n "$MODE_ARG" ] && echo "=== collecting baselines ===" || echo "=== comparing against baselines ==="
rc=0
for entry in "${ADAPTERS[@]}"; do
  label="${entry%%|*}"; tag="${entry#*|}"
  [ -n "$ONLY" ] && [ "$ONLY" != "$tag" ] && continue

  echo
  echo "########################################################################"
  echo "##  $label   (tag: $tag)"
  echo "########################################################################"
  echo "  1) Power off the DC, fit the $label."
  echo "  2) Boot it, make sure it's on the network (can reach $HOST)."
  read -r -p "  Press ENTER when ready, or 's'+ENTER to skip this adapter: " ans
  [ "$ans" = s ] && { echo "  skipped $tag"; continue; }

  "$IPERF" --host "$HOST" --tag "$tag" $MODE_ARG "${EXTRA[@]}" || rc=1
done

echo
[ "$rc" -eq 0 ] && echo "=== all done ===" || echo "=== done, with failures (see above) ==="
exit $rc
