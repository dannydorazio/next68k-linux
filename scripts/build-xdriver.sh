#!/bin/sh
# Build the NeXTdimension's X driver (xorg-nextdimension/) for the NeXT,
# into out/xdriver/nextdimension_drv.so, and the X benchmark tests/xbench.c
# into out/xdriver/xbench, in a Debian m68k chroot run through
# qemu-user in Docker (as build-dockapps.sh does).  The chroot, with the
# compiler, xserver-xorg-dev and Xft, is kept in out/m68k-xdev.tar.gz after
# the first run (made from the dockapps' out/m68k-dev.tar.gz if that's
# there).  Its X server must be the version on the NeXT: the driver's ABI is
# checked when X loads it.
#
# usage: build-xdriver.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$top/out/xdriver"

exec docker run --rm --privileged -v "$top":/work \
	-e MIRROR="${MIRROR:-http://deb.debian.org/debian-ports}" \
	next68k-build sh -ec '
	binfmt=/proc/sys/fs/binfmt_misc
	mountpoint -q $binfmt || mount -t binfmt_misc binfmt_misc $binfmt
	[ -e $binfmt/qemu-m68k ] ||
		grep -v "^#" /usr/lib/binfmt.d/qemu-m68k.conf > $binfmt/register

	r=/tmp/root
	mkdir $r
	dev=build-essential,pkg-config,xserver-xorg-dev,libxft-dev
	dev=$dev,debian-ports-archive-keyring
	if [ -f /work/out/m68k-xdev.tar.gz ]; then
		tar -xzf /work/out/m68k-xdev.tar.gz -C $r
	elif [ ! -f /work/out/m68k-dev.tar.gz ]; then
		mmdebstrap --arch=m68k --variant=minbase \
			--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
			--include=$dev unstable /work/out/m68k-xdev.tar.gz "$MIRROR"
		tar -xzf /work/out/m68k-xdev.tar.gz -C $r
	else
		tar -xzf /work/out/m68k-dev.tar.gz -C $r
		cp /etc/resolv.conf $r/etc/resolv.conf
		chroot $r sh -c "apt-get update &&
			DEBIAN_FRONTEND=noninteractive apt-get install -y \
				--no-install-recommends xserver-xorg-dev"
		rm -rf $r/var/lib/apt/lists/* $r/var/cache/apt/archives/*.deb
		tar -czf /work/out/m68k-xdev.tar.gz -C $r .
	fi
	cp -r /work/xorg-nextdimension $r/src
	chroot $r make -C /src clean nextdimension_drv.so
	cp $r/src/nextdimension_drv.so /work/out/xdriver/
	cp /work/tests/xbench.c $r/src/
	chroot $r sh -c "cc -O2 -Wall -o /src/xbench /src/xbench.c \$(pkg-config --cflags --libs xft x11)"
	cp $r/src/xbench /work/out/xdriver/
	chroot $r dpkg-query -W xserver-xorg-dev
	ls -l /work/out/xdriver
'
