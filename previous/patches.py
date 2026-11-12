#!/usr/bin/env python3
"""Maintain the Previous patch series in previous/patches.

  patches.py apply [TREE]
      Apply the series, in the order of patches/series, to a Previous
      source tree: SVN trunk r1864, ../trunk by default.  Each patch is
      checked first; the first that doesn't apply stops the series.

  patches.py refresh NAME [TREE]
      Rewrite patches/NAME.patch from what TREE has now, after you changed
      it: the difference between the pristine sources with the patches
      before NAME applied, and TREE with the patches after NAME taken back
      out.  A NAME not in the series yet becomes a new patch at its end.
      The description at the top of the patch (everything before its first
      "---" line) is kept; a new patch gets a placeholder to replace.

The pristine sources come from the SVN working copy (svn cat -r BASE), so
TREE must be one.  The patches use paths relative to TREE (patch -p0).
"""

import argparse
import filecmp
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHES = os.path.join(HERE, "patches")
SERIES = os.path.join(PATCHES, "series")


def read_series():
    names = []
    for line in open(SERIES):
        line = line.split("#", 1)[0].split()
        if line:
            names.append(line[0])
    return names


def patch_files(path):
    """The files a patch touches."""
    return set(re.findall(r"(?m)^\+\+\+ (\S+)", open(path).read()))


def run_patch(tree, path, reverse=False, dry=False):
    cmd = ["patch", "-p0", "-s", "--no-backup-if-mismatch"]
    cmd += ["-R"] if reverse else ["--forward"]
    if dry:
        cmd.append("--dry-run")
    with open(path) as f:
        return subprocess.run(cmd, cwd=tree, stdin=f,
                              capture_output=True, text=True).returncode == 0


def apply(args):
    for name in read_series():
        path = os.path.join(PATCHES, name)
        if not run_patch(args.tree, path, dry=True):
            sys.exit("%s doesn't apply to %s" % (name, args.tree))
        run_patch(args.tree, path)
        print("applied", name)


def pristine(tree, f, dest):
    """f as SVN has it, or nothing if SVN doesn't have it."""
    r = subprocess.run(["svn", "cat", "-r", "BASE", f], cwd=tree,
                       capture_output=True)
    if r.returncode == 0:
        os.makedirs(os.path.dirname(os.path.join(dest, f)), exist_ok=True)
        with open(os.path.join(dest, f), "wb") as out:
            out.write(r.stdout)


def refresh(args):
    name = args.name if args.name.endswith(".patch") else args.name + ".patch"
    series = read_series()
    new = name not in series
    before = series if new else series[:series.index(name)]
    after = [] if new else series[series.index(name) + 1:]
    target = os.path.join(PATCHES, name)

    status = subprocess.run(["svn", "status", "-q"], cwd=args.tree,
                            capture_output=True, text=True, check=True).stdout
    files = {l[8:].strip() for l in status.splitlines() if l[:1] in "MA"}
    for p in series:
        files |= patch_files(os.path.join(PATCHES, p))

    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")      # pristine + the patches before
        b = os.path.join(tmp, "b")      # the tree - the patches after
        os.makedirs(a)
        for f in sorted(files):
            pristine(args.tree, f, a)
            if os.path.exists(os.path.join(args.tree, f)):
                os.makedirs(os.path.dirname(os.path.join(b, f)), exist_ok=True)
                shutil.copy(os.path.join(args.tree, f), os.path.join(b, f))
        for p in before:
            if not run_patch(a, os.path.join(PATCHES, p)):
                sys.exit("%s doesn't apply to the pristine sources" % p)
        for p in reversed(after):
            if not run_patch(b, os.path.join(PATCHES, p), reverse=True):
                sys.exit("%s no longer comes back out of %s: the change "
                         "overlaps it; fold it in by hand" % (p, args.tree))

        diff = ""
        for f in sorted(files):
            fa, fb = os.path.join(a, f), os.path.join(b, f)
            if os.path.exists(fa) and os.path.exists(fb) and \
               filecmp.cmp(fa, fb, shallow=False):
                continue
            r = subprocess.run(["diff", "-uN", "--label", f, "--label", f,
                                fa if os.path.exists(fa) else "/dev/null",
                                fb if os.path.exists(fb) else "/dev/null"],
                               capture_output=True, text=True)
            diff += r.stdout

    if os.path.exists(target):
        text = open(target).read()
        header = text[:text.index("--- ")] if "--- " in text else text
    else:
        header = "One-line summary\n\nWhat this changes in Previous, and why.\n\n"
    if not diff:
        sys.exit("%s: no changes left for it" % name)
    with open(target, "w") as f:
        f.write(header + diff)
    if new:
        with open(SERIES, "a") as f:
            f.write(name + " -p0\n")
    print("%s %s" % ("added" if new else "refreshed", name))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    default = os.path.normpath(os.path.join(HERE, "..", "..", "trunk"))
    p = sub.add_parser("apply")
    p.add_argument("tree", nargs="?", default=default)
    p.set_defaults(func=apply)
    p = sub.add_parser("refresh")
    p.add_argument("name")
    p.add_argument("tree", nargs="?", default=default)
    p.set_defaults(func=refresh)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
