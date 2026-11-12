#!/bin/sh
# Build the wmoiia and wmaapl Window Maker dockapps from
# ../powerbook-debian/tools for the NeXT, into out/dockapps/, in a Debian
# m68k chroot run through qemu-user in Docker (as build-debian.sh does).
# The chroot, with the compiler and the dockapps' -dev packages, is kept
# in out/m68k-dev.tar.gz after the first run.  Also writes
# out/dockapps/runtime-packages: the Debian packages the binaries need on
# the NeXT.
#
# usage: build-dockapps.sh [TOOLS]   (default ../powerbook-debian/tools)
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
tools=$(cd "${1:-$top/../powerbook-debian/tools}" && pwd)
mkdir -p "$top/out/dockapps"

exec docker run --rm --privileged -v "$top":/work -v "$tools":/tools:ro \
	-e MIRROR="${MIRROR:-http://deb.debian.org/debian-ports}" \
	next68k-build sh -ec '
	binfmt=/proc/sys/fs/binfmt_misc
	mountpoint -q $binfmt || mount -t binfmt_misc binfmt_misc $binfmt
	[ -e $binfmt/qemu-m68k ] ||
		grep -v "^#" /usr/lib/binfmt.d/qemu-m68k.conf > $binfmt/register

	dev=build-essential,pkg-config,libdockapp-dev,libxft-dev
	dev=$dev,libcurl4-openssl-dev,libcjson-dev,debian-ports-archive-keyring
	if [ ! -f /work/out/m68k-dev.tar.gz ]; then
		mmdebstrap --arch=m68k --variant=minbase \
			--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
			--include=$dev unstable /work/out/m68k-dev.tar.gz "$MIRROR"
	fi
	r=/tmp/root
	mkdir $r
	tar -xzf /work/out/m68k-dev.tar.gz -C $r
	mkdir $r/src
	cp -r /tools/wmoiia /tools/wmaapl $r/src/
	for t in wmoiia wmaapl; do
		chroot $r make -C /src/$t
		cp $r/src/$t/$t /work/out/dockapps/
	done

	# the packages that hold the libraries they link to
	chroot $r sh -c "
		for t in wmoiia wmaapl; do ldd /src/\$t/\$t; done |
		awk \"/=> \\\\// { print \\\$3 }\" | sort -u |
		while read -r lib; do
			p=\$(readlink -f \$lib)
			dpkg -S \$p 2>/dev/null || dpkg -S \${p#/usr}
		done" | cut -d: -f1 | sort -u > /work/out/dockapps/runtime-packages
	ls -l /work/out/dockapps
	cat /work/out/dockapps/runtime-packages
'
