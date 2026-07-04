#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
dc.py - scriptable serial-console driver for the Dreamcast Linux console.

Talks to the mksh shell that busybox-getty starts on the DC serial console,
so tests (and Claude) can run commands non-interactively and assert on output.

Transport: same USB coder cable you use with minicom. Close minicom first -
only one process can hold the serial port.

Login model: the DC waits for you to press Enter, runs flashfetch, then shows
the mksh prompt. This driver reproduces that (sends Enter, waits for a live
shell) and does NOT need credentials.

Config via env (or flags):
  DC_PORT   serial device      (default /dev/ttyUSB0)
  DC_BAUD   console baud rate   (default 115200)

Subcommands:
  dc.py run     "<cmd>"                 run cmd, print output, exit = remote $?
  dc.py expect  "<cmd>" "<substring>"   run cmd, exit 0 iff substring in output
  dc.py capture "<cmd>" --seconds N     run a streaming cmd for N s, print output
  dc.py ping                            check the console is alive (exit 0/1)

Examples:
  DC_PORT=/dev/ttyUSB0 ./dc.py run "cat /proc/version"
  ./dc.py expect "ls /dev/input" "event0"
  ./dc.py capture "./maple_test" --seconds 6   # while you press buttons
"""
import argparse
import os
import re
import sys
import time
import uuid

try:
    import serial  # pyserial
except ImportError:
    sys.exit("dc.py: pyserial not found. Install with: pip install pyserial")


class DC:
    def __init__(self, port, baud, verbose=False):
        self.verbose = verbose
        # exclusive=True -> fail fast with a clear message if minicom holds it
        try:
            self.ser = serial.Serial(port, baud, timeout=0.2, exclusive=True)
        except serial.SerialException as e:
            sys.exit(f"dc.py: cannot open {port} @ {baud}: {e}\n"
                     f"       Is minicom (or another terminal) still connected?")

    def _log(self, msg):
        if self.verbose:
            print(f"[dc] {msg}", file=sys.stderr)

    def _read_until(self, pattern, timeout):
        """Read until regex `pattern` matches accumulated output, or timeout."""
        deadline = time.time() + timeout
        buf = ""
        rx = re.compile(pattern)
        while time.time() < deadline:
            chunk = self.ser.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                m = rx.search(buf)
                if m:
                    return buf, m
        return buf, None

    def ensure_shell(self, timeout=25):
        """Wake the getty (Enter) and confirm a live mksh shell is present.

        Robust against the flashfetch delay: we keep poking with a unique
        token echo until we see the token come back as command *output*
        (i.e. the token appears twice: the input echo + the result line).
        """
        token = "DCRDY_" + uuid.uuid4().hex[:8]
        self.ser.reset_input_buffer()
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.ser.write(("\recho " + token + "\r").encode())
            buf, _ = self._read_until(re.escape(token), 2.0)
            if buf.count(token) >= 2:
                self._log("shell is live")
                return True
        return False

    def run(self, cmd, timeout=30):
        """Run cmd, return (exit_code, output). Uses a sentinel marker so we
        don't need to know the exact prompt string."""
        marker = "DCEND_" + uuid.uuid4().hex[:8]
        self.ser.reset_input_buffer()
        # `$?` stays literal in the input echo, so only the real result line
        # matches MARKER:<digits> - the command echo never false-matches.
        line = f"{cmd}; echo {marker}:$?\r"
        self.ser.write(line.encode())
        buf, m = self._read_until(re.escape(marker) + r":(\d+)", timeout)
        if not m:
            raise TimeoutError(
                f"no response to {cmd!r} within {timeout}s. "
                f"Last bytes seen:\n{buf[-400:]}")
        rc = int(m.group(1))
        raw = buf[: m.start()]
        # Drop the first line (the echoed command we typed).
        lines = raw.replace("\r", "").split("\n")
        body = "\n".join(lines[1:]).strip("\n")
        return rc, body

    def capture(self, cmd, seconds):
        """Run a streaming command for a fixed window, then Ctrl-C it.
        Returns collected output. Used for the interactive maple event tests."""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\r").encode())
        buf = ""
        deadline = time.time() + seconds
        while time.time() < deadline:
            chunk = self.ser.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
        self.ser.write(b"\x03")  # Ctrl-C to stop the program
        time.sleep(0.3)
        self.ser.read(4096)      # drain
        # Drop the echoed command line.
        lines = buf.replace("\r", "").split("\n")
        return "\n".join(lines[1:])

    def sysrq(self, key):
        """Trigger a magic SysRq over serial: a BREAK condition (the SysRq
        trigger on a serial console) followed by the command key. Needs NO live
        shell - use it to recover a hung/OOM'd box. e.g. key='b' reboots
        immediately, 's'=sync, 'e'=SIGTERM tasks, 'c'=crash."""
        self.ser.send_break(duration=0.3)
        time.sleep(0.1)
        self.ser.write(key.encode())
        self.ser.flush()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser(description="Dreamcast serial-console driver")
    ap.add_argument("--port", default=os.environ.get("DC_PORT", "/dev/ttyUSB0"))
    ap.add_argument("--baud", type=int,
                    default=int(os.environ.get("DC_BAUD", "115200")))
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="action", required=True)

    p_run = sub.add_parser("run", help="run cmd, print output, exit=remote $?")
    p_run.add_argument("cmd")
    p_run.add_argument("--timeout", type=int, default=30)

    p_exp = sub.add_parser("expect", help="assert substring in cmd output")
    p_exp.add_argument("cmd")
    p_exp.add_argument("substring")
    p_exp.add_argument("--timeout", type=int, default=30)

    p_cap = sub.add_parser("capture", help="run a streaming cmd for N seconds")
    p_cap.add_argument("cmd")
    p_cap.add_argument("--seconds", type=int, default=6)

    sub.add_parser("ping", help="check the console is alive")

    p_rq = sub.add_parser("sysrq",
                          help="magic SysRq over serial (BREAK+key); no login. e.g. b=reboot")
    p_rq.add_argument("key", nargs="?", default="b")
    p_rq.add_argument("--wait", type=int, default=0,
                      help="after sending, wait up to N s for the shell to return")

    args = ap.parse_args()
    dc = DC(args.port, args.baud, verbose=args.verbose)
    try:
        # sysrq must NOT try to log in first - the shell may be hung.
        if args.action == "sysrq":
            dc.sysrq(args.key)
            print(f"sent magic SysRq: BREAK + {args.key!r}", file=sys.stderr)
            if args.wait:
                if dc.ensure_shell(timeout=args.wait):
                    print("ok: shell is back")
                    return 0
                sys.exit(f"dc.py: shell did not return within {args.wait}s")
            return 0

        if not dc.ensure_shell():
            sys.exit("dc.py: no live shell on the console "
                     "(is the DC booted into Linux and the cable plugged in?)")

        if args.action == "ping":
            print("ok: console alive")
            return 0

        if args.action == "run":
            rc, out = dc.run(args.cmd, timeout=args.timeout)
            if out:
                print(out)
            return rc

        if args.action == "expect":
            rc, out = dc.run(args.cmd, timeout=args.timeout)
            ok = args.substring in out
            print(out)
            if ok:
                print(f"PASS: found {args.substring!r}", file=sys.stderr)
                return 0
            print(f"FAIL: {args.substring!r} not in output (cmd exit {rc})",
                  file=sys.stderr)
            return 1

        if args.action == "capture":
            print(dc.capture(args.cmd, args.seconds))
            return 0
    except TimeoutError as e:
        sys.exit(f"dc.py: {e}")
    finally:
        dc.close()


if __name__ == "__main__":
    sys.exit(main())
