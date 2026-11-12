#!/usr/bin/env python3
"""Make an existing NeXT-labelled disk image boot Linux without the network.

  nextboot.py setup DISK BOOTBLOCK [--boot MB]
      Write BOOTBLOCK (the loader built with "make bootblock") into the
      label's boot blocks, and add a partition of type "boot" (default 32
      MB) if there is none: from free space at the end of the disk, or else
      from the end of the last partition if it is swap (its swap header is
      rewritten for the smaller size).  Everything else is kept.

  nextboot.py kernel DISK VMLINUX [--initrd FILE] [--cmdline STRING]
      Write a kernel (its ELF file, e.g. vmlinux or Debian's
      /boot/vmlinuz-*) and optionally an initrd into the boot partition, as
      the Debian kernel hook (debian-root/etc/kernel/postinst.d/zz-next-boot)
      does on the NeXT.

The boot partition starts with a header (struct bootpart in loader/loader.h):
magic "NXBK", then the kernel's offset and size, the initrd's offset and
size, and a 256-byte command line.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkdisk  # noqa: E402

BOOTPART_MAGIC = 0x4E58424B    # "NXBK"
BOOTPART_FMT = ">5I256s"
KERNEL_OFFSET = 4096


def read_label(f):
    f.seek(0)
    label = bytearray(f.read(mkdisk.LABEL_BYTES))
    if struct.unpack_from(">I", label, 0)[0] != mkdisk.LABEL_V3:
        sys.exit("no version 3 NeXT disk label")
    return label


def partitions(label):
    """(slot, offset, nsect, type) of each used partition"""
    for i in range(mkdisk.NPART):
        off = mkdisk.PART_OFFSET + i * mkdisk.PART_SIZE
        start, nsect = struct.unpack_from(">ii", label, off)
        ptype = label[off + 37:off + 45].split(b"\0")[0].decode()
        if nsect > 0:
            yield i, start, nsect, ptype


def geometry(label):
    secsize = struct.unpack_from(">i", label, 92)[0]
    front = struct.unpack_from(">h", label, 112)[0]
    total = struct.unpack_from(">i", label, 8)[0]
    return secsize, front, total


def boot_partition(f, label):
    secsize, front, _ = geometry(label)
    for _, start, nsect, ptype in partitions(label):
        if ptype == "boot":
            return (front + start) * secsize, nsect * secsize
    sys.exit("no boot partition (nextboot.py setup)")


def setup(args):
    with open(args.disk, "r+b") as f:
        label = read_label(f)
        secsize, front, total = geometry(label)
        parts = list(partitions(label))
        if any(p[3] == "boot" for p in parts):
            print("%s: already has a boot partition" % args.disk)
        else:
            want = args.boot * 1024 * 1024 // secsize
            used = max(start + nsect for _, start, nsect, _ in parts)
            free = [i for i in range(mkdisk.NPART)
                    if i not in [p[0] for p in parts]]
            if not free:
                sys.exit("no free partition slot")
            if total - front - used >= want:
                start = used
            else:
                slot, sstart, snsect, stype = max(parts, key=lambda p: p[1])
                if stype != "swap" or snsect < 2 * want:
                    sys.exit("no room for a %d MB boot partition" % args.boot)
                snsect -= want
                start = sstart + snsect
                off = mkdisk.PART_OFFSET + slot * mkdisk.PART_SIZE
                struct.pack_into(">i", label, off + 4, snsect)
                f.seek((front + sstart) * secsize)
                f.write(mkdisk.swap_header(snsect * secsize))
                print("%s: swap (%c) shrunk to %d MB" % (
                    args.disk, ord("a") + slot, snsect * secsize >> 20))
            slot = free[0]
            off = mkdisk.PART_OFFSET + slot * mkdisk.PART_SIZE
            label[off:off + mkdisk.PART_SIZE] = \
                mkdisk.partition(start, want, "boot", "")
            # nothing old in it may pass for a kernel
            f.seek((front + start) * secsize)
            f.write(bytes(KERNEL_OFFSET))
            print("%s: boot partition %c, %d MB" % (
                args.disk, ord("a") + slot, args.boot))

        mkdisk.write_bootblocks(f, label, args.bootblock)
        mkdisk.set_checksum(label)
        f.seek(0)
        f.write(label)
        print("%s: boot blocks from %s" % (args.disk, args.bootblock))


def kernel(args):
    with open(args.vmlinux, "rb") as k:
        kdata = k.read()
    if kdata[:4] != b"\x7fELF":
        sys.exit("%s: not an ELF file" % args.vmlinux)
    idata = b""
    if args.initrd:
        with open(args.initrd, "rb") as i:
            idata = i.read()
    cmdline = args.cmdline.encode()
    if len(cmdline) >= 256:
        sys.exit("command line too long")

    ioff = (KERNEL_OFFSET + len(kdata) + 4095) & ~4095 if idata else 0
    hdr = struct.pack(BOOTPART_FMT, BOOTPART_MAGIC, KERNEL_OFFSET, len(kdata),
                      ioff, len(idata), cmdline)
    with open(args.disk, "r+b") as f:
        start, size = boot_partition(f, read_label(f))
        if (ioff or KERNEL_OFFSET + len(kdata)) + len(idata) > size:
            sys.exit("the boot partition (%d bytes) is too small" % size)
        f.seek(start)
        f.write(hdr + bytes(KERNEL_OFFSET - len(hdr)))
        f.write(kdata)
        if idata:
            f.seek(start + ioff)
            f.write(idata)
    print("%s: kernel %s (%d bytes)%s, command line \"%s\"" % (
        args.disk, args.vmlinux, len(kdata),
        ", initrd %d bytes" % len(idata) if idata else "", args.cmdline))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("setup")
    p.add_argument("disk")
    p.add_argument("bootblock")
    p.add_argument("--boot", type=int, default=32)
    p.set_defaults(func=setup)
    p = sub.add_parser("kernel")
    p.add_argument("disk")
    p.add_argument("vmlinux")
    p.add_argument("--initrd")
    p.add_argument("--cmdline", default="root=/dev/sda1 rootwait rw")
    p.set_defaults(func=kernel)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
