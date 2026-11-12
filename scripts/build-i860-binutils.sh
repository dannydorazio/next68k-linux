#!/bin/sh
# Build an assembler and linker for the NeXTdimension's i860 into the
# Docker volume next68k-i860 (/opt/i860), for nd-firmware/build.sh.
#
# binutils 2.30 (January 2018) is the last release with the i860; it needs
# --enable-obsolete.  GCC dropped the i860 in 2005, so the firmware is
# assembly.  The tarball's signature is checked against the GNU keyring.
#
# usage: build-i860-binutils.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
dl=$top/out/downloads/binutils
mkdir -p "$dl"
cd "$dl"
for f in binutils-2.30.tar.xz binutils-2.30.tar.xz.sig gnu-keyring.gpg; do
	[ -f "$f" ] && continue
	case $f in
	gnu-keyring.gpg) url=https://ftp.gnu.org/gnu/$f ;;
	*) url=https://ftp.gnu.org/gnu/binutils/$f ;;
	esac
	curl -sSfLo "$f" "$url"
done
gpg --no-default-keyring --keyring ./gnu-keyring.gpg \
	--verify binutils-2.30.tar.xz.sig binutils-2.30.tar.xz

exec docker run --rm -v "$dl":/dl:ro -v next68k-i860:/opt/i860 next68k-build sh -ec '
	cd /tmp
	tar xf /dl/binutils-2.30.tar.xz
	mkdir build
	cd build
	../binutils-2.30/configure --target=i860-stardent-elf --enable-obsolete \
		--prefix=/opt/i860 --disable-nls --disable-werror \
		--disable-gdb --disable-sim > configure.log
	make -j"$(nproc)" CFLAGS="-O2 -fcommon -Wno-error" > make.log
	make install > install.log
	ls /opt/i860/bin'
