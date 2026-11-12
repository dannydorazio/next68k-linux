#!/bin/sh
# Build the loader and wrap the kernel from build-kernel.sh into a netboot
# image at out/nfsroot/private/tftpboot/boot, where Previous's built-in
# BOOTP/TFTP server looks for it.  Also builds out/bootblock, the loader
# alone for a disk's boot blocks (build-disk.sh, nextboot.py).
#
# usage: build-image.sh [CMDLINE="..."] [INITRD=/work/path]
#
# out/initramfs.cpio.gz (from build-initramfs.sh) is used as the initrd
# unless INITRD is given.
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
linux=${LINUX:-$(cd "$top/../linux" && pwd)}

mkdir -p "$top/out/nfsroot/private/tftpboot"

initrd=
if [ -f "$top/out/initramfs.cpio.gz" ]; then
	initrd=INITRD=/work/out/initramfs.cpio.gz
fi

exec docker run --rm \
	-v "$top":/work \
	-v "$linux":/src/linux:ro \
	-v next68k-kbuild:/build:ro \
	next68k-build \
	make -C /work/loader LINUX=/src/linux image bootblock \
		KERNEL=/build/linux/vmlinux \
		OUTPUT=/work/out/nfsroot/private/tftpboot/boot \
		BOOTBLOCK=/work/out/bootblock $initrd "$@"
