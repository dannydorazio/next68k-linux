#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Build a NeXT netboot image: a.out header + loader + payload.

The NeXT ROM loads an a.out OMAGIC file (machine ID 0x87) at its entry
address and calls it.  The payload header, kernel image and optional initrd
are appended to the loader binary; see struct payload in loader.h.

Without --kernel, the image is the loader alone, for a disk's boot blocks:
the loader then reads the kernel from the disk (disk.c).
"""

import argparse
import struct
import sys

PT_LOAD = 1
SHT_SYMTAB = 2
PAYLOAD_MAGIC = 0x4E58424C  # "NXBL"
PAYLOAD_CMDLINE_MAX = 256
PAYLOAD_FMT = ">8I%ds" % PAYLOAD_CMDLINE_MAX
AOUT_MIDMAG = 0x00870107    # mid 0x87 (m68k), OMAGIC
BOOTBLOCK_MAX = 64 * 1024   # the two copies are 64 KB apart


def align(n, a):
    return (n + a - 1) & ~(a - 1)


class Elf32BE:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2:
            sys.exit("%s: not a 32-bit big-endian ELF file" % path)
        (self.entry, self.phoff, self.shoff) = struct.unpack_from(">III", d, 24)
        (self.phentsize, self.phnum, self.shentsize, self.shnum) = \
            struct.unpack_from(">HHHH", d, 42)

    def segments(self):
        for i in range(self.phnum):
            fields = struct.unpack_from(">8I", self.data, self.phoff + i * self.phentsize)
            p_type, p_offset, p_vaddr, _, p_filesz, p_memsz = fields[:6]
            if p_type == PT_LOAD:
                yield p_offset, p_vaddr, p_filesz, p_memsz

    def symbols(self):
        d = self.data
        sections = [struct.unpack_from(">10I", d, self.shoff + i * self.shentsize)
                    for i in range(self.shnum)]
        syms = {}
        for sh in sections:
            if sh[1] != SHT_SYMTAB:
                continue
            strtab = sections[sh[6]]
            for off in range(sh[4], sh[4] + sh[5], sh[9]):
                name, value = struct.unpack_from(">II", d, off)
                end = d.index(b"\0", strtab[4] + name)
                syms[d[strtab[4] + name:end].decode()] = value
        return syms


def flatten_kernel(path):
    """Return (image, vaddr, memsize) for vmlinux, starting at _stext."""
    elf = Elf32BE(path)
    syms = elf.symbols()
    base, end = syms["_stext"], syms["_end"]

    top = max(vaddr + filesz for _, vaddr, filesz, _ in elf.segments())
    image = bytearray(top - base)
    for offset, vaddr, filesz, _ in elf.segments():
        skip = max(0, base - vaddr)
        if skip >= filesz:
            continue
        start = vaddr + skip - base
        image[start:start + filesz - skip] = \
            elf.data[offset + skip:offset + filesz]

    image += bytes(align(len(image), 4) - len(image))
    if image[0] != 0x60 or struct.unpack_from(">I", image, 2)[0] != 0x4249561A:
        sys.exit("%s: no bootinfo version table at _stext" % path)
    return bytes(image), base, end - base


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--loader", required=True, help="loader ELF (for the entry address)")
    ap.add_argument("--loader-bin", required=True, help="flat loader binary")
    ap.add_argument("--kernel", help="vmlinux (none: a boot block)")
    ap.add_argument("--initrd")
    ap.add_argument("--cmdline", default="")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    entry = Elf32BE(args.loader).entry
    with open(args.loader_bin, "rb") as f:
        loader = f.read()
    if len(loader) % 16:
        sys.exit("loader binary size %d is not a multiple of 16" % len(loader))

    if not args.kernel:
        if len(loader) + 32 > BOOTBLOCK_MAX:
            sys.exit("loader too big for a boot block: %d bytes" % len(loader))
        aout = struct.pack(">8I", AOUT_MIDMAG, len(loader), 0, 0, 0, entry, 0, 0)
        with open(args.output, "wb") as f:
            f.write(aout)
            f.write(loader)
        print("%s: %d bytes, loads at 0x%08x" % (args.output, 32 + len(loader), entry))
        return

    kernel, vaddr, memsize = flatten_kernel(args.kernel)
    initrd = b""
    if args.initrd:
        with open(args.initrd, "rb") as f:
            initrd = f.read()

    cmdline = args.cmdline.encode()
    if len(cmdline) >= PAYLOAD_CMDLINE_MAX:
        sys.exit("command line too long")

    hdr_size = align(struct.calcsize(PAYLOAD_FMT), 16)
    kernel_offset = hdr_size
    initrd_offset = align(kernel_offset + len(kernel), 16) if initrd else 0
    payload_size = (initrd_offset + len(initrd)) if initrd else kernel_offset + len(kernel)
    image_size = len(loader) + payload_size

    hdr = struct.pack(PAYLOAD_FMT, PAYLOAD_MAGIC, image_size, kernel_offset,
                      len(kernel), memsize, vaddr, initrd_offset, len(initrd),
                      cmdline)
    payload = bytearray(payload_size)
    payload[0:len(hdr)] = hdr
    payload[kernel_offset:kernel_offset + len(kernel)] = kernel
    if initrd:
        payload[initrd_offset:initrd_offset + len(initrd)] = initrd

    aout = struct.pack(">8I", AOUT_MIDMAG, image_size, 0, 0, 0, entry, 0, 0)
    with open(args.output, "wb") as f:
        f.write(aout)
        f.write(loader)
        f.write(payload)

    print("%s: %d bytes, loads at 0x%08x; kernel %d bytes (+%d bss) at vaddr 0x%x%s"
          % (args.output, 32 + image_size, entry, len(kernel), memsize - len(kernel),
             vaddr, ", initrd %d bytes" % len(initrd) if initrd else ""))


if __name__ == "__main__":
    main()
