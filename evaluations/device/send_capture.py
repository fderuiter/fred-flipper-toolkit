#!/usr/bin/env python3
"""Send a synthetic FRED hex capture to a serial port (device qualification, issue #9).

Host-side helper for an operator who has authorized the UART test. It writes
only to the serial port named on the command line, at 115200 8N1, using the
standard library (POSIX termios). Each non-comment line of the capture is sent
as one burst; --gap sets the pause between bursts.

  python3 send_capture.py --port /dev/ttyUSB0 ../fixtures/03-uart/captures/burst_hex.txt
  python3 send_capture.py --port /dev/ttyUSB0 --repeat 200 --gap 0 burst_hex.txt   # line-rate stress
"""
import argparse
import os
import sys
import termios
import time


def load(path):
    bursts = []
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            bursts.append(bytes(int(tok, 16) for tok in line.split()))
    return bursts


def open_port(port):
    fd = os.open(port, os.O_WRONLY | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                   # iflag
    attrs[1] = 0                                   # oflag: raw
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # 8N1
    attrs[3] = 0                                   # lflag
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--port", required=True)
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--gap", type=float, default=0.05, help="seconds between bursts")
    args = ap.parse_args(argv)
    bursts = load(args.capture)
    fd = open_port(args.port)
    sent = 0
    try:
        for _ in range(args.repeat):
            for b in bursts:
                os.write(fd, b)
                sent += len(b)
                if args.gap:
                    time.sleep(args.gap)
        termios.tcdrain(fd)
    finally:
        os.close(fd)
    print(f"sent {sent} bytes in {len(bursts) * args.repeat} bursts to {args.port}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
