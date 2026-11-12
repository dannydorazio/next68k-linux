#!/usr/bin/env python3
"""Attach the terminal to an emulated serial port (out/ttyA by default).

Keys go to the NeXT, its output comes back.  Ctrl-] quits.
"""

import os
import select
import sys
import termios
import tty

ESCAPE = b"\x1d"  # Ctrl-]


def main():
    top = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(top, "out", "ttyA")
    try:
        port = os.open(path, os.O_RDWR | os.O_NOCTTY)
    except OSError as e:
        sys.exit("%s: %s (is Previous running?)" % (path, e.strerror))

    stdin = sys.stdin.fileno()
    saved = termios.tcgetattr(stdin) if os.isatty(stdin) else None
    if saved:
        tty.setraw(stdin)
    print("Connected to %s, Ctrl-] quits.\r" % path)
    try:
        while True:
            ready, _, _ = select.select([stdin, port], [], [])
            if port in ready:
                data = os.read(port, 4096)
                if not data:
                    break
                os.write(sys.stdout.fileno(), data)
            if stdin in ready:
                data = os.read(stdin, 1024)
                if not data or ESCAPE in data:
                    break
                os.write(port, data)
    finally:
        if saved:
            termios.tcsetattr(stdin, termios.TCSADRAIN, saved)
        print()


if __name__ == "__main__":
    main()
