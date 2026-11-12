#!/bin/sh
# Copy debian-root/ into the root file system (partition a) of the existing
# out/disk.img, keeping everything else on it: installed packages, apt's
# package lists and caches, logs.  Also the NeXTdimension's X driver, if
# scripts/build-xdriver.sh has built it and X is installed.  Previous must
# not be running.
#
# usage: update-disk.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)

if [ -s "$top/out/previous.pid" ] && kill -0 "$(cat "$top/out/previous.pid")" 2>/dev/null; then
	echo "Previous is running; power the NeXT off first" >&2
	exit 1
fi

# partition a starts after the 160 KB label and front porch (mkdisk.py)
exec docker run --rm --privileged -v "$top":/work next68k-build sh -ec '
	mkdir /tmp/root
	mount -o loop,offset=163840 /work/out/disk.img /tmp/root
	cp -rv --no-preserve=ownership /work/debian-root/. /tmp/root/ | sed "s|^.* -> .*/tmp/root|  |"
	d=/usr/lib/xorg/modules/drivers
	if [ -f /work/out/xdriver/nextdimension_drv.so ] && [ -d /tmp/root$d ]; then
		install -v -m 644 /work/out/xdriver/nextdimension_drv.so /tmp/root$d/
	fi
	umount /tmp/root
'
