#!/usr/bin/env python3
"""Turn kernel addresses into function+offset, and count them.

Reads addresses (hex, with or without 0x, or as "pc=XXXXXXXX") from the
arguments, or from standard input, one or more per line, as printed by a
temporary log in Previous (see docs/debugging.md).  Prints how often each
function+offset occurs, most frequent first.  The symbols come from the
kernel build's System.map in the next68k-kbuild Docker volume, or the file
given with --map.

usage: pc2sym.py [--map System.map] [ADDRESS...]
       grep TEMPLOG out/previous-stdout.log | pc2sym.py
"""

import argparse
import bisect
import collections
import re
import subprocess
import sys

ADDR = re.compile(r"(?<![0-9A-Za-z])(?:pc=|0x)?([0-9a-fA-F]{6,8})(?![0-9A-Za-z])")


def load_map(path):
    if path:
        text = open(path).read()
    else:
        text = subprocess.run(
            ["docker", "run", "--rm", "-v", "next68k-kbuild:/build",
             "next68k-build", "cat", "/build/linux/System.map"],
            check=True, capture_output=True, text=True).stdout
    syms = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1].lower() in "tw":
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map")
    ap.add_argument("addresses", nargs="*")
    args = ap.parse_args()

    syms = load_map(args.map)
    starts = [a for a, _ in syms]
    lines = args.addresses or sys.stdin
    counts = collections.Counter()
    for line in lines:
        for m in ADDR.finditer(line):
            pc = int(m.group(1), 16)
            i = bisect.bisect_right(starts, pc) - 1
            if i < 0:
                counts["%08x (below the kernel)" % pc] += 1
            else:
                counts["%s+0x%x" % (syms[i][1], pc - syms[i][0])] += 1
    for name, n in counts.most_common():
        print("%5d %s" % (n, name))


if __name__ == "__main__":
    main()
