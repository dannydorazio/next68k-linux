#!/bin/sh
# Assemble the busybox root file system in the directory given as $1.
# Runs inside the build container with the project at /work.
set -e

root=$1
deb=$(ls /work/out/downloads/busybox/busybox-static_*_m68k.deb | tail -1)
pkg=$(mktemp -d)

dpkg-deb -x "$deb" "$pkg"
mkdir -p "$root"
cd "$root"
mkdir -p bin sbin usr/bin usr/sbin dev proc sys tmp root mnt
cp "$pkg/usr/bin/busybox" bin/
ln -sf busybox bin/sh
ln -sf ../bin/busybox sbin/init
ln -sf bin/busybox init
cp -a /work/initramfs/. .
for f in /work/tests/*; do
	[ -f "$f" ] && [ -x "$f" ] && cp "$f" usr/bin/
done
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
rm -rf "$pkg"
