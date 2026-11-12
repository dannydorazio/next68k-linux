#!/usr/bin/env python3
"""Run shell commands on the emulated NeXT over its serial console.

Waits for a shell prompt in out/serial-a.log, then types each command into
out/ttyA and waits for the next prompt.  Prints the transcript.  --port b
uses the second serial port (out/serial-b.log, out/ttyB) instead.  With
--login, a "login:" prompt is answered as root with the Debian image's
password first.

usage: serial-run.py [--timeout SECONDS] [--port a|b] [--login] COMMAND...
"""

import argparse
import os
import re
import sys
import time

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = None
TTY = None
PROMPT = re.compile(rb"# $")
NOISE = re.compile(rb"\x1b\[6n|\r")


def log_text():
    try:
        with open(LOG, "rb") as f:
            return NOISE.sub(b"", f.read())
    except FileNotFoundError:
        return b""


def wait_prompt(start, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = log_text()
        if len(text) > start and PROMPT.search(text):
            return text
        time.sleep(0.5)
    return None


def wait_for_shell(timeout):
    """Wait until a shell has started at all (not necessarily idle)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if b"# " in log_text():
            return True
        time.sleep(0.5)
    return False


def wait_end(pattern, timeout):
    """Wait until the log ends with pattern (a prompt waiting for input)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log_text().rstrip().endswith(pattern):
            return True
        time.sleep(0.5)
    return False


def login(timeout):
    """Log in as root if the console is at a login prompt."""
    fd = os.open(TTY, os.O_RDWR | os.O_NOCTTY)
    if wait_end(b"login:", timeout):
        os.write(fd, b"root\r")
        if not wait_end(b"Password:", timeout):
            sys.exit("no password prompt in %s" % LOG)
        os.write(fd, b"next\r")
    os.close(fd)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--port", choices=("a", "b"), default="a")
    ap.add_argument("--login", action="store_true")
    ap.add_argument("commands", nargs="+")
    args = ap.parse_args()

    global LOG, TTY
    LOG = os.path.join(TOP, "out", "serial-%s.log" % args.port)
    TTY = os.path.join(TOP, "out", "tty" + args.port.upper())

    if args.login and not PROMPT.search(log_text()):
        login(args.timeout * 3)
    if not wait_for_shell(args.timeout * 3):
        sys.exit("no shell prompt in %s" % LOG)

    fd = os.open(TTY, os.O_RDWR | os.O_NOCTTY)
    # Kernel messages may have been printed after the last prompt.
    if not PROMPT.search(log_text()):
        before = len(log_text())
        os.write(fd, b"\r")
        if wait_prompt(before, args.timeout) is None:
            sys.exit("no fresh shell prompt in %s" % LOG)
    start = len(log_text())
    for cmd in args.commands:
        before = len(log_text())
        os.write(fd, cmd.encode() + b"\r")
        if wait_prompt(before + len(cmd) + 1, args.timeout) is None:
            print("(timed out waiting for the prompt after: %s)" % cmd)
            break
    os.close(fd)
    sys.stdout.write(log_text()[start:].decode(errors="replace") + "\n")


if __name__ == "__main__":
    main()
