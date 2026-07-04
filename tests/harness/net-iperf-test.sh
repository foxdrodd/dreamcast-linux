#!/usr/bin/env bash
# Networking throughput test + regression check for the DC (broadband adapter).
#
# Topology: your HOST runs `iperf3 -s`; the DC (driven over serial by dc.py)
# runs `iperf3 -c <host>` as the client, so DC CPU is the thing under test.
# Measures both directions:
#   TX  = DC  -> host   (iperf3 -c,     sum_sent)
#   RX  = host -> DC    (iperf3 -c -R,  sum_received at the DC)
#
# Compares against a committed baseline and fails on a regression beyond
# THRESH%. First run has no baseline -> use --save-baseline to establish one.
#
# Requirements: DC booted into Linux with a working BBA + IP, cable in,
#               minicom CLOSED, iperf3 on host (have it) and on the DC.
#
# Usage:
#   ./net-iperf-test.sh --host 192.168.1.50               # host IP as the DC sees it
#   ./net-iperf-test.sh --host 192.168.1.50 --save-baseline
#   ./net-iperf-test.sh --host 192.168.1.50 --tag HKT-3020 --dur 10 --thresh 15
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DC="python3 $HERE/dc.py"
BASE_DIR="$HERE/baselines"

HOST="${DC_IPERF_HOST:-}"
DUR=10
PORT=5201
THRESH=10          # % throughput drop vs baseline that counts as a failure
TAG=""
SAVE=0
LOG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --host) HOST="$2"; shift 2 ;;
    --dur)  DUR="$2";  shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --thresh) THRESH="$2"; shift 2 ;;
    --tag)  TAG="$2";  shift 2 ;;
    --save-baseline) SAVE=1; shift ;;
    --log)  LOG="$2";  shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

[ -z "$HOST" ] && { echo "error: need --host <ip the DC uses to reach this PC>" >&2; exit 2; }
command -v iperf3 >/dev/null || { echo "error: iperf3 not on host" >&2; exit 2; }

echo "=== net-iperf-test  (DC <-> host $HOST:$PORT, ${DUR}s each way) ==="
if ! $DC ping >/dev/null 2>&1; then
  echo "FATAL: no live console. Boot the DC into Linux, plug the cable, close minicom." >&2
  exit 2
fi

# Sanity: can the DC reach the host at all?
if ! $DC run "ping -c1 -W2 $HOST" >/dev/null 2>&1; then
  echo "FATAL: DC cannot ping $HOST - check BBA IP / routing before perf testing." >&2
  exit 2
fi

[ -z "$TAG" ] && TAG="$($DC run "uname -n" 2>/dev/null | tr -d '[:space:]')"
[ -z "$TAG" ] && TAG="dreamcast"

# Start a throwaway iperf3 server on the host; kill it on exit.
srv_log="$(mktemp)"
iperf3 -s -p "$PORT" >"$srv_log" 2>&1 &
SRV_PID=$!
trap 'kill "$SRV_PID" 2>/dev/null; rm -f "$srv_log"' EXIT
sleep 1
kill -0 "$SRV_PID" 2>/dev/null || { echo "FATAL: iperf3 server didn't start:"; cat "$srv_log"; exit 2; }

# run_dir <extra iperf args> <jq path to bits_per_second>  -> echoes Mbps
run_dir() {
  local extra="$1" path="$2" out
  out="$($DC run "iperf3 -c $HOST -p $PORT -t $DUR $extra -J" --timeout $((DUR + 25)) 2>/dev/null)"
  # keep only the JSON object, then pull bits/s -> Mbps (1 d.p.)
  printf '%s' "$out" | sed -n '/{/,/^}/p' \
    | jq -r "$path // empty | (. / 1000000 * 10 | round / 10)" 2>/dev/null
}

echo "measuring TX (DC -> host)..."
TX="$(run_dir ""   '.end.sum_sent.bits_per_second')"
echo "measuring RX (host -> DC)..."
RX="$(run_dir "-R" '.end.sum_received.bits_per_second')"

[ -z "$TX" ] && { echo "FATAL: TX measurement failed (no throughput parsed)"; exit 3; }
[ -z "$RX" ] && { echo "FATAL: RX measurement failed (no throughput parsed)"; exit 3; }

printf '\nresult [%s]:  TX %s Mbit/s   RX %s Mbit/s\n' "$TAG" "$TX" "$RX"

# --- baseline compare ---------------------------------------------------------
mkdir -p "$BASE_DIR"
BASELINE="$BASE_DIR/$TAG.iperf"
rc=0

if [ "$SAVE" = 1 ]; then
  { echo "tx_mbps=$TX"; echo "rx_mbps=$RX"; } > "$BASELINE"
  echo "baseline saved: ${BASELINE#$HERE/}  (commit it)"
elif [ -f "$BASELINE" ]; then
  # shellcheck disable=SC1090
  . "$BASELINE"
  cmp_one() { # name cur base
    awk -v n="$1" -v c="$2" -v b="$3" -v t="$THRESH" 'BEGIN{
      if (b<=0){printf "  %s: no baseline value\n",n; exit}
      d=(c-b)/b*100;
      s=(d < -t) ? "FAIL" : "ok";
      printf "  %-3s %7.1f -> %7.1f Mbit/s  (%+.1f%%)  %s\n", n, b, c, d, s;
      exit (d < -t) ? 1 : 0 }'
  }
  echo "compare vs baseline (${BASELINE#$HERE/}, fail if drop > ${THRESH}%):"
  cmp_one "TX" "$TX" "${tx_mbps:-0}" || rc=1
  cmp_one "RX" "$RX" "${rx_mbps:-0}" || rc=1
else
  echo "no baseline for '$TAG' yet. Establish one with --save-baseline"
fi

# --- optional trend log (CSV) -------------------------------------------------
if [ -n "$LOG" ]; then
  [ -f "$LOG" ] || echo "tag,tx_mbps,rx_mbps" > "$LOG"
  echo "$TAG,$TX,$RX" >> "$LOG"
  echo "appended to $LOG"
fi

exit $rc
