#!/usr/bin/env python3
"""Make a SCSI disk image with a NeXT disk label (version 3, "dlV3").

Each partition image given on the command line becomes one partition, in
order (a, b, ...).  --swap adds a Linux swap partition after them, with a
swap header for 4 KB pages, and --boot an empty partition of type "boot"
for the kernel (see nextboot.py).  --bootblock writes the boot program the
ROM loads from disk (the loader, as a.out) into the two boot block areas
the label names.  Layout of the label as in NetBSD's <sys/bootblock.h>.

usage: mkdisk.py -o disk.img [--size MB] [--secsize N] [--swap MB]
                 [--boot MB] [--bootblock FILE] part.img[:type]...
"""

import argparse
import os
import struct
import sys
import uuid

LABEL_V3 = 0x646C5633          # "dlV3"
LABEL_BYTES = 7240
PART_OFFSET = 190              # cd_partitions
PART_SIZE = 46
V3_CHECKSUM = 558              # cd_un.CD_v3_checksum
BOOT_BLKNO = 124               # cd_boot_blkno[2]
BOOTFILE = 132                 # cd_kernel: the ROM won't boot without one
BOOTBLOCKS = (32 * 1024, 96 * 1024)  # where NeXT and NetBSD put them
BOOTBLOCK_MAX = 64 * 1024
NPART = 8
PAGE_SIZE = 4096               # m68k, except Sun 3


def checksum(buf):
    s = sum(struct.unpack(">%dH" % (len(buf) // 2), buf))
    s += s >> 16
    return s & 0xFFFF


def partition(offset, size, fstype, mountpt):
    p = struct.pack(">iihhcchhbb16sb8sx", offset, size, 8192, 1024, b"t",
                    b"\0", 16, 4096, 5, 0, mountpt.encode(), 1,
                    fstype.encode())
    assert len(p) == PART_SIZE
    return p


def swap_header(nbytes):
    """A version 1 swap header ("SWAPSPACE2"), big-endian like the m68k."""
    page = bytearray(PAGE_SIZE)
    struct.pack_into(">III16s", page, 1024, 1, nbytes // PAGE_SIZE - 1, 0,
                     uuid.uuid4().bytes)
    page[PAGE_SIZE - 10:] = b"SWAPSPACE2"
    return bytes(page)


def set_checksum(label):
    struct.pack_into(">H", label, V3_CHECKSUM,
                     checksum(bytes(label[:V3_CHECKSUM])))


def write_bootblocks(f, label, path):
    """Write the boot program at both boot block offsets, and name them in
    the label (in label sectors)."""
    with open(path, "rb") as b:
        prog = b.read()
    if len(prog) > BOOTBLOCK_MAX:
        sys.exit("%s: %d bytes, more than %d" % (path, len(prog), BOOTBLOCK_MAX))
    secsize = struct.unpack_from(">i", label, 92)[0]
    for off in BOOTBLOCKS:
        f.seek(off)
        f.write(prog + bytes(BOOTBLOCK_MAX - len(prog)))
    struct.pack_into(">ii", label, BOOT_BLKNO,
                     BOOTBLOCKS[0] // secsize, BOOTBLOCKS[1] // secsize)
    # passed to the boot program, which doesn't look at it
    struct.pack_into("24s", label, BOOTFILE, b"linux")


def copy_sparse(src, dst):
    """Copy src into dst at its current position, skipping zero blocks."""
    zero = bytes(1 << 20)
    while True:
        chunk = src.read(1 << 20)
        if not chunk:
            break
        if chunk == zero[:len(chunk)]:
            dst.seek(len(chunk), os.SEEK_CUR)
        else:
            dst.write(chunk)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--size", type=int, default=0, help="disk size in MB")
    ap.add_argument("--secsize", type=int, default=1024,
                    help="label sector size (default 1024)")
    ap.add_argument("--front", type=int, default=160,
                    help="front porch in label sectors (default 160)")
    ap.add_argument("--name", default="linux")
    ap.add_argument("--swap", type=int, default=0,
                    help="add a swap partition of this many MB")
    ap.add_argument("--boot", type=int, default=0,
                    help="add a boot partition of this many MB")
    ap.add_argument("--bootblock", help="boot program for the boot blocks")
    ap.add_argument("partitions", nargs="+")
    args = ap.parse_args()

    ss = args.secsize
    parts = []
    pos = 0
    for spec in args.partitions:
        path, _, fstype = spec.partition(":")
        size = os.path.getsize(path)
        nsec = (size + ss - 1) // ss
        parts.append((path, pos, nsec, fstype or "ext4"))
        pos += nsec
    if args.swap:
        nsec = args.swap * 1024 * 1024 // ss
        parts.append((None, pos, nsec, "swap"))
        pos += nsec
    if args.boot:
        nsec = args.boot * 1024 * 1024 // ss
        parts.append((None, pos, nsec, "boot"))
        pos += nsec
    if len(parts) > NPART:
        sys.exit("at most %d partitions" % NPART)

    used = (args.front + pos) * ss
    total = max(args.size * 1024 * 1024, used)
    total = (total + ss - 1) // ss * ss

    label = bytearray(LABEL_BYTES)
    struct.pack_into(">iii24sII24s24siiiiihhhhhhii24s32scc", label, 0,
                     LABEL_V3, 0, total // ss, args.name.encode(), 0, 0,
                     b"Linux disk", b"fixed_rw_scsi", ss, 1, 32,
                     total // ss // 32, 3600, args.front, 0, 0, 0, 0, 0,
                     -1, -1, b"", b"", b"a", b"b")
    for i in range(NPART):
        if i < len(parts):
            _, off, nsec, fstype = parts[i]
            p = partition(off, nsec, fstype, "/" if i == 0 else "")
        else:
            p = partition(-1, -1, "", "")
        label[PART_OFFSET + i * PART_SIZE:PART_OFFSET + (i + 1) * PART_SIZE] = p
    if args.front * ss < BOOTBLOCKS[1] + BOOTBLOCK_MAX:
        sys.exit("the front porch is too small for the boot blocks")

    with open(args.output, "wb") as f:
        f.truncate(total)
        if args.bootblock:
            write_bootblocks(f, label, args.bootblock)
        set_checksum(label)
        f.seek(0)
        f.write(label)
        for path, off, nsec, fstype in parts:
            f.seek((args.front + off) * ss)
            if fstype == "swap":
                f.write(swap_header(nsec * ss))
                continue
            if path is None:
                continue
            with open(path, "rb") as p:
                copy_sparse(p, f)

    print("%s: %d MB, %d-byte label sectors" % (args.output, total >> 20, ss))
    for i, (path, off, nsec, fstype) in enumerate(parts):
        start = (args.front + off) * ss // 512
        end = start + nsec * ss // 512 - 1
        print("  %c: %-6s 512-byte sectors %d-%d  (%s)"
              % (ord("a") + i, fstype, start, end, path or fstype))


if __name__ == "__main__":
    main()
