#!/bin/sh
# Build out/initramfs.cpio.gz: a static busybox from Debian Ports plus the
# files under initramfs/ and the test programs in tests/.  The busybox
# package is expected in out/downloads/busybox/ (see README.md).
set -e

top=$(cd "$(dirname "$0")/.." && pwd)

exec docker run --rm -v "$top":/work next68k-build sh -ec '
	/work/scripts/rootfs.sh /tmp/root
	cd /tmp/root
	find . | sort | cpio -o -H newc --owner 0:0 --quiet | gzip -9 \
		> /work/out/initramfs.cpio.gz
	ls -l /work/out/initramfs.cpio.gz
'
