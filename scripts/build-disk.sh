#!/bin/sh
# Build out/disk.img: a SCSI disk with a NeXT disk label.
#
#   busybox (default): partition a is ext4 with the busybox root file
#     system; 128 MB.
#   debian: partition a is out/debian-root.img (build-debian.sh), grown to
#     fill the disk, partition b is swap (SWAP MB, default 224) and c the
#     32 MB boot partition, with the kernel installed in the image and the
#     command line from debian-root/etc/kernel/cmdline; the boot blocks
#     hold out/bootblock (build-image.sh), so the disk boots without the
#     network (run-previous.sh BOOT=disk); 2048 MB.
#
# out/disk.img keeps whatever the NeXT installed and changed, so an existing
# one is only replaced with FORCE=1, and then kept as out/disk.img.old.  To
# copy changes in debian-root/ to it instead, use update-disk.sh.
#
# usage: [FORCE=1] build-disk.sh [busybox|debian] [size in MB]
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
kind=${1:-busybox}

if [ -e "$top/out/disk.img" ]; then
	if [ "$FORCE" != 1 ]; then
		echo "out/disk.img exists; FORCE=1 replaces it (keeping out/disk.img.old)," >&2
		echo "update-disk.sh copies debian-root/ into it" >&2
		exit 1
	fi
	mv "$top/out/disk.img" "$top/out/disk.img.old"
fi

case $kind in
busybox)
	size=${2:-128}
	exec docker run --rm -v "$top":/work next68k-build sh -ec "
		/work/scripts/rootfs.sh /tmp/root
		echo 'Linux on NeXT root disk' > /tmp/root/etc/disk-root
		mke2fs -q -t ext4 -L next-root -d /tmp/root /tmp/root.img $((size - 1))M
		python3 /work/scripts/mkdisk.py -o /work/out/disk.img --size $size \
			/tmp/root.img:ext4
	"
	;;
debian)
	size=${2:-2048}
	swap=${SWAP:-224}
	boot=32
	[ -f "$top/out/debian-root.img" ] ||
		{ echo "no out/debian-root.img; run build-debian.sh" >&2; exit 1; }
	[ -f "$top/out/bootblock" ] ||
		{ echo "no out/bootblock; run build-image.sh" >&2; exit 1; }
	# 1 MB for the label and front porch
	exec docker run --rm -v "$top":/work next68k-build sh -ec "
		cp --sparse=always /work/out/debian-root.img /tmp/root.img
		e2fsck -fy /tmp/root.img > /dev/null
		resize2fs /tmp/root.img $((size - swap - boot - 1))M
		python3 /work/scripts/mkdisk.py -o /work/out/disk.img --size $size \
			--swap $swap --boot $boot --bootblock /work/out/bootblock \
			/tmp/root.img:ext4
		kernel=\$(debugfs -R 'ls -p /boot' /tmp/root.img 2>/dev/null |
			tr / ' ' | awk '\$5 ~ /^vmlinuz-/ { print \$5 }' | sort -V | tail -1)
		debugfs -R \"dump /boot/\$kernel /tmp/vmlinuz\" /tmp/root.img
		python3 /work/scripts/nextboot.py kernel /work/out/disk.img /tmp/vmlinuz \
			--cmdline \"\$(cat /work/debian-root/etc/kernel/cmdline)\"
	"
	;;
*)
	echo "usage: build-disk.sh [busybox|debian] [size in MB]" >&2
	exit 1
	;;
esac
