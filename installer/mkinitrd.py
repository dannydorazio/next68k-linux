#!/usr/bin/env python3
"""Build the NeXT installer's initrd from Debian Ports' udebs.

Downloads the udebs named in installer/packages, and everything they
depend on, from the mirror's main/debian-installer for m68k, checking
InRelease's signature with Debian Ports' keyring and each file's SHA-256.
Unpacks them and our own udebs with dpkg, as debian-installer's build does
(build/Makefile, the tree targets), adds libgcc_s and libatomic, which
have no udebs, and busybox's wget, and optionally a preseed.cfg, and
writes the tree as a gzip-compressed cpio archive: a 25 MHz 68040 takes
over 4 minutes to decompress it from xz.  Every ELF file's libraries are
checked to be in the tree.

Runs as root in the build container (build-installer.sh), which has dpkg,
sqv, Debian Ports' keyring and m68k-linux-gnu-readelf.

usage: mkinitrd.py --packages FILE --local DIR [--preseed FILE] --cache DIR
                   --tree DIR -o initrd.gz [--manifest FILE]
                   [--mirror URL] [--suite SUITE] [--version STRING]
"""

import argparse
import gzip
import hashlib
import lzma
import os
import re
import shutil
import stat
import subprocess
import sys
import urllib.request

ARCH = "m68k"
TRIPLET = "m68k-linux-gnu"
KEYRING = "/usr/share/keyrings/debian-ports-archive-keyring.pgp"
# Files from regular packages, (package, file name prefix, where in the
# tree).  libgcc_s and libatomic have no udebs: debian-installer copies them
# from its build host (build/Makefile, "HACK ALERT").  wget: the installer
# fetches files with wget, and busybox-udeb has no wget applet.  wget-udeb
# would bring 6 MB of OpenSSL for https, which the mirror doesn't need;
# Debian's regular busybox has the applet, and the installer's fetch-url
# knows busybox's wget.
FROM_DEBS = [("libgcc-s2", r"usr/lib/%s/libgcc_s\.so\.2.*" % TRIPLET,
              "usr/lib/%s/" % TRIPLET),
             ("libatomic1", r"usr/lib/%s/libatomic\.so\.1.*" % TRIPLET,
              "usr/lib/%s/" % TRIPLET),
             ("busybox", r"usr/bin/busybox", "usr/bin/wget")]


def fetch(url, path=None):
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                data = r.read()
            break
        except OSError as e:
            if attempt == 4:
                raise
            print("%s: %s; trying again" % (url, e))
    if path:
        with open(path, "wb") as f:
            f.write(data)
    return data


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def release(mirror, suite, cache):
    """The checked contents of the suite's InRelease."""
    signed = os.path.join(cache, "InRelease")
    fetch("%s/dists/%s/InRelease" % (mirror, suite), signed)
    checked = signed + ".checked"
    if os.path.exists(checked):
        os.remove(checked)
    subprocess.run(["sqv", "--keyring", KEYRING, "--cleartext",
                    "--output", checked, signed], check=True,
                   stdout=subprocess.DEVNULL)
    with open(checked) as f:
        text = f.read()
    sums = {}
    m = re.search(r"(?ms)^SHA256:\n(.*?)(?=^\S|\Z)", text)
    for line in m.group(1).splitlines():
        h, size, name = line.split()
        sums[name] = h
    return sums


def packages_index(mirror, suite, sums, path):
    """Parse a Packages.xz, checked against InRelease."""
    name = path + "/Packages.xz"
    data = fetch("%s/dists/%s/%s" % (mirror, suite, name))
    if sums.get(name) != sha256(data):
        sys.exit("%s: SHA-256 doesn't match InRelease" % name)
    index = {}
    for stanza in lzma.decompress(data).decode().split("\n\n"):
        fields = dict(re.findall(r"(?m)^([A-Za-z0-9-]+): (.*)$", stanza))
        if "Package" in fields:
            index[fields["Package"]] = fields
    return index


def names(field):
    """[[alternative, ...], ...] from a Depends field, without versions"""
    out = []
    for group in field.split(","):
        alts = [re.sub(r"[\s(\[:].*", "", a.strip()) for a in group.split("|")]
        out.append([a for a in alts if a])
    return [g for g in out if g]


def read_list(path):
    want, drop = [], set()
    for line in open(path):
        line = line.split("#")[0].strip()
        if line.startswith("-"):
            drop.add(line[1:])
        elif line:
            want.append(line)
    return want, drop


def control(udeb):
    out = subprocess.run(["dpkg-deb", "-f", udeb], check=True,
                         capture_output=True, text=True).stdout
    return dict(re.findall(r"(?m)^([A-Za-z0-9-]+): (.*)$", out))


def resolve(want, drop, index, local):
    """The packages to install: want and their dependencies.  local maps
    our own udebs' names to their control fields; they win over the
    archive's."""
    provides = {}
    for pool in (index, local):
        for name, f in pool.items():
            for group in names(f.get("Provides", "")):
                provides.setdefault(group[0], []).append(name)

    def lookup(name):
        return local.get(name) or index.get(name)

    chosen, todo = {}, list(want)
    while todo:
        name = todo.pop(0)
        if name in chosen:
            continue
        f = lookup(name)
        if f is None:
            sys.exit("%s: not in the archive" % name)
        chosen[name] = f
        for group in names(f.get("Depends", "")) + names(f.get("Pre-Depends", "")):
            if any(a in chosen or a in drop for a in group):
                continue
            if any(p in chosen for a in group for p in provides.get(a, [])):
                continue
            for a in group:
                if lookup(a):
                    todo.append(a)
                    break
                cands = [p for p in provides.get(a, []) if p not in drop]
                # one we were going to install anyway, if any
                cands.sort(key=lambda p: p not in want)
                if cands:
                    todo.append(cands[0])
                    break
            else:
                # kernel module udebs: ours has no modules
                if all(a.endswith("-modules") or "-modules-" in a
                       or a in drop for a in group):
                    continue
                sys.exit("%s: nothing satisfies its dependency on %s"
                         % (name, " | ".join(group)))
    return chosen


def download(mirror, f, cache):
    path = os.path.join(cache, os.path.basename(f["Filename"]))
    if os.path.exists(path):
        with open(path, "rb") as g:
            if sha256(g.read()) == f["SHA256"]:
                return path
    data = fetch("%s/%s" % (mirror, f["Filename"]))
    if sha256(data) != f["SHA256"]:
        sys.exit("%s: SHA-256 doesn't match the index" % f["Filename"])
    with open(path, "wb") as g:
        g.write(data)
    return path


def merge_usr(tree):
    """/bin, /sbin and /lib into /usr, with links in their place, as
    debian-installer's build/util/merge-usr does."""
    def move(rel):
        src, dst = os.path.join(tree, rel), os.path.join(tree, "usr", rel)
        if os.path.islink(dst):
            sys.exit("merge-usr: usr/%s is a symlink" % rel)
        if not os.path.lexists(dst):
            os.rename(src, dst)
            return
        if not os.path.isdir(src) or os.path.islink(src):
            sys.exit("merge-usr: %s and usr/%s both exist" % (rel, rel))
        for e in os.listdir(src):
            move(os.path.join(rel, e))
        os.rmdir(src)

    for d in ("bin", "lib", "sbin"):
        top = os.path.join(tree, d)
        if os.path.lexists(top) and not os.path.islink(top):
            move(d)
        if os.path.exists(os.path.join(tree, "usr", d)) \
                and not os.path.lexists(top):
            os.symlink("usr/" + d, top)


def drop_translations(info):
    """Only English in the templates, as debian-installer's DROP_LANG does
    for small images: the translations are 7 MB of a NeXT's memory."""
    for name in os.listdir(info):
        if not name.endswith(".templates"):
            continue
        path = os.path.join(info, name)
        out, keep = [], True
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            for line in f:
                if line[:1].isupper() or line == "\n":
                    keep = not re.match(
                        r"(Choices|Description|Indices|Default)-(?!C:)", line)
                if keep:
                    out.append(line)
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.writelines(out)


def check_libraries(tree):
    """Every library an ELF file in the tree needs must be in the tree."""
    libdirs = ["usr/lib", "usr/lib/" + TRIPLET, "lib", "lib/" + TRIPLET]
    have = set()
    for d in libdirs:
        p = os.path.join(tree, d)
        if os.path.isdir(p):
            have.update(os.listdir(p))
    missing = {}
    for root, _, files in os.walk(tree):
        for name in files:
            path = os.path.join(root, name)
            if not stat.S_ISREG(os.lstat(path).st_mode):
                continue	# links, and /dev/console, which would block
            with open(path, "rb") as f:
                if f.read(4) != b"\x7fELF":
                    continue
            out = subprocess.run([TRIPLET + "-readelf", "-d", path],
                                 capture_output=True, text=True).stdout
            here = set()
            for rp in re.findall(r"\((?:RPATH|RUNPATH)\).*\[(.*)\]", out):
                for d in rp.split(":"):
                    d = d.replace("$ORIGIN", os.path.dirname(
                        "/" + os.path.relpath(path, tree)))
                    p = os.path.join(tree, d.lstrip("/"))
                    if os.path.isdir(p):
                        here.update(os.listdir(p))
            for lib in re.findall(r"\(NEEDED\).*\[(.*)\]", out):
                if lib not in have and lib not in here:
                    missing.setdefault(lib, []).append(
                        os.path.relpath(path, tree))
    for lib, users in sorted(missing.items()):
        print("missing %s, needed by %s" % (lib, ", ".join(users)))
    if missing:
        sys.exit("libraries missing from the initrd")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mirror", default="http://deb.debian.org/debian-ports")
    ap.add_argument("--suite", default="unstable")
    ap.add_argument("--packages", required=True)
    ap.add_argument("--local", required=True, help="directory of our udebs")
    ap.add_argument("--preseed")
    ap.add_argument("--cache", required=True)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--version", default="next68k")
    ap.add_argument("--manifest")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    os.makedirs(args.cache, exist_ok=True)
    sums = release(args.mirror, args.suite, args.cache)
    udebs = packages_index(args.mirror, args.suite, sums,
                           "main/debian-installer/binary-" + ARCH)
    debs = packages_index(args.mirror, args.suite, sums,
                          "main/binary-" + ARCH)

    local, local_files = {}, {}
    for name in sorted(os.listdir(args.local)):
        if name.endswith(".udeb"):
            path = os.path.join(args.local, name)
            f = control(path)
            local[f["Package"]] = f
            local_files[f["Package"]] = path

    want, drop = read_list(args.packages)
    chosen = resolve(want, drop, udebs, local)

    if os.path.exists(args.tree):
        shutil.rmtree(args.tree)
    dpkgdir = os.path.join(args.tree, "var/lib/dpkg")
    os.makedirs(os.path.join(dpkgdir, "info"))
    os.makedirs(os.path.join(dpkgdir, "updates"))
    os.makedirs(os.path.join(args.tree, "tmp"))
    for name in ("status", "available"):
        open(os.path.join(dpkgdir, name), "w").close()

    manifest = []
    files = []
    for name in sorted(chosen):
        if name in local_files:
            files.append(local_files[name])
        else:
            files.append(download(args.mirror, chosen[name], args.cache))
        manifest.append("%s %s" % (name, chosen[name]["Version"]))
    print("%d udebs" % len(files))
    r = subprocess.run(["dpkg", "--root=" + args.tree, "--log=/dev/null",
                        "--force-architecture", "--force-depends",
                        "--path-include=*", "--unpack"] + files,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)

    # Prefer kmod's programs to busybox's, as debian-installer does.
    for prog in ("depmod", "insmod", "lsmod", "modinfo", "modprobe", "rmmod"):
        p = os.path.join(args.tree, "sbin", prog)
        if os.path.lexists(p):
            os.remove(p)
    merge_usr(args.tree)

    libdir = os.path.join(args.tree, "usr/lib", TRIPLET)
    os.makedirs(libdir, exist_ok=True)
    for pkg, pattern, where in FROM_DEBS:
        deb = download(args.mirror, debs[pkg], args.cache)
        x = os.path.join(args.cache, "x-" + pkg)
        shutil.rmtree(x, ignore_errors=True)
        subprocess.run(["dpkg-deb", "-x", deb, x], check=True)
        for root, _, names_ in os.walk(x):
            for n in names_:
                src = os.path.join(root, n)
                if re.fullmatch(pattern, os.path.relpath(src, x)):
                    dst = os.path.join(args.tree, where)
                    if where.endswith("/"):
                        dst += n
                    if os.path.islink(src):
                        os.symlink(os.readlink(src), dst)
                    else:
                        shutil.copy2(src, dst)
        manifest.append("%s %s (only %s)" % (pkg, debs[pkg]["Version"], where))
        shutil.rmtree(x)

    shutil.rmtree(os.path.join(dpkgdir, "updates"))
    for name in os.listdir(dpkgdir):
        if name in ("available", "lock") or name.endswith("-old"):
            os.remove(os.path.join(dpkgdir, name))
    shutil.rmtree(os.path.join(dpkgdir, "triggers"), ignore_errors=True)
    info = os.path.join(dpkgdir, "info")
    drop_translations(info)
    for name in os.listdir(info):
        if re.search(r"\.(md5sums|postrm|prerm|preinst|list)$", name):
            os.remove(os.path.join(info, name))

    # Only the status fields the installer uses, and an entry for the
    # installer itself, as debian-installer's build does.
    status = os.path.join(dpkgdir, "status")
    keep = re.compile(r"(?i)^((Status|Provides|Depends|Package|Version|"
                      r"Description|installer-menu-item|Description-..):|$)")
    with open(status) as f:
        lines = [l for l in f if keep.match(l)]
    lines += ["Package: debian-installer\n", "Status: install ok installed\n",
              "Version: %s\n" % args.version,
              "Description: debian installation image\n", "\n"]
    with open(status, "w") as f:
        f.writelines(lines)

    etc = os.path.join(args.tree, "etc")
    os.makedirs(etc, exist_ok=True)
    for name, text in (("default-release", args.suite),
                       ("udebs-source", args.suite)):
        with open(os.path.join(etc, name), "w") as f:
            f.write(text + "\n")
    with open(os.path.join(etc, "lsb-release"), "w") as f:
        f.write('DISTRIB_ID=Debian\nDISTRIB_DESCRIPTION="Debian GNU/Linux '
                'installer for the NeXT"\nDISTRIB_RELEASE="%s"\n'
                'X_INSTALLATION_MEDIUM=netboot\n' % args.suite)
    if args.preseed:
        shutil.copy(args.preseed, os.path.join(args.tree, "preseed.cfg"))
    dev = os.path.join(args.tree, "dev")
    os.makedirs(dev, exist_ok=True)
    os.mknod(os.path.join(dev, "console"), 0o600 | 0o020000, os.makedev(5, 1))
    os.mknod(os.path.join(dev, "null"), 0o666 | 0o020000, os.makedev(1, 3))

    check_libraries(args.tree)

    cpio = subprocess.run("cd '%s' && find . | LC_ALL=C sort | "
                          "cpio -o -H newc --quiet -R 0:0" % args.tree,
                          shell=True, check=True, capture_output=True).stdout
    with open(args.output, "wb") as f:
        f.write(gzip.compress(cpio, compresslevel=9, mtime=0))
    print("%s: %d KB (%d KB unpacked)" % (args.output,
          os.path.getsize(args.output) >> 10, len(cpio) >> 10))
    if args.manifest:
        with open(args.manifest, "w") as f:
            f.write("\n".join(sorted(manifest)) + "\n")


if __name__ == "__main__":
    main()
