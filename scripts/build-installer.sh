#!/bin/sh
# Build the Debian installer for the NeXT into out/installer/:
#
#   boot              netboot image (loader, kernel and installer initrd),
#                     for a BOOTP/TFTP server: Previous's is
#                     out/nfsroot/private/tftpboot/boot
#   installer.img     a disk to start the installer from (its NeXT label is
#                     named "Debian installer"; the installer won't offer
#                     it as a target): for a SCSI emulator such as
#                     BlueSCSI, or Previous
#   initrd.gz, vmlinuz, bootblock, linux-image-*.deb  the pieces
#   README.txt, MANIFEST, SHA256SUMS
#   next68k-debian-installer-DATE.tar.xz  all of the above, to pass on
#
# The installer is debian-installer, assembled from Debian Ports' current
# udebs (installer/mkinitrd.py, installer/packages) with our kernel and two
# NeXT components of our own (installer/udebs/): next-partitioner, which
# gives the disk a NeXT label with root, swap and boot partitions and the
# boot blocks, and next-installer, which installs our kernel package and
# the NeXT settings from debian-root/.  The base system comes from the
# mirror during the install.
#
# By default the kernel and this tree come from GitHub, as last pushed
# (LINUX_URL/LINUX_REF, TREE_URL/TREE_REF), so the installer can be
# rebuilt from nothing but the network; FROM=local builds them from ../linux
# (LINUX=) and this checkout as they are, uncommitted changes and all.  The
# udebs always come from MIRROR's unstable.  Docker (the next68k-build
# image) does the building; the kernel builds in the Docker volume
# next68k-installer.
#
# CMDLINE sets the installer kernel's command line (default
# "console=ttyPZ0,9600 console=tty0": the installer runs on the screen,
# with the kernel's messages on the serial port too).
#
# usage: [FROM=github|local] build-installer.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
out=$top/out/installer
work=$out/build
from=${FROM:-github}
mirror=${MIRROR:-http://deb.debian.org/debian-ports}
linux_url=${LINUX_URL:-https://github.com/dannydorazio/linux.git}
linux_ref=${LINUX_REF:-next68k-linux}
tree_url=${TREE_URL:-https://github.com/dannydorazio/next68k-linux.git}
tree_ref=${TREE_REF:-main}
cmdline=${CMDLINE:-console=ttyPZ0,9600 console=tty0}
date=$(date -u +%Y%m%d)
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}

mkdir -p "$out" "$work"

# A shallow fetch of one branch into a bare repository (no checkout, so the
# kernel's file names that differ only in case don't matter on a Mac).
fetch() {	# dir url ref
	[ -d "$1" ] || git init -q --bare "$1"
	git --git-dir="$1" fetch -q --depth 1 "$2" "$3"
	git --git-dir="$1" rev-parse FETCH_HEAD
}

case $from in
github)
	echo "Fetching $tree_url $tree_ref and $linux_url $linux_ref"
	tree_commit=$(fetch "$out/src/next68k-linux.git" "$tree_url" "$tree_ref")
	linux_commit=$(fetch "$out/src/linux.git" "$linux_url" "$linux_ref")
	tree=$work/next68k-linux
	rm -rf "$tree"
	mkdir -p "$tree"
	git --git-dir="$out/src/next68k-linux.git" archive "$tree_commit" |
		tar -x -C "$tree"
	# The kernel source goes into the Docker volume, unpacked again only
	# when the commit changes.
	have=$(docker run --rm -v next68k-installer:/v next68k-build \
		cat /v/linux/.commit 2>/dev/null || true)
	if [ "$have" != "$linux_commit" ]; then
		echo "Unpacking the kernel source ($linux_commit)"
		git --git-dir="$out/src/linux.git" archive "$linux_commit" |
			docker run --rm -i -v next68k-installer:/v next68k-build sh -ec "
				rm -rf /v/linux /v/kbuild-github
				mkdir -p /v/linux
				tar -x -C /v/linux
				echo $linux_commit > /v/linux/.commit"
	fi
	linux_mount="-v next68k-installer:/v"
	linux_src=/v/linux
	kbuild=/v/kbuild-github
	tree_desc="$tree_url $tree_ref $tree_commit"
	linux_desc="$linux_url $linux_ref $linux_commit"
	;;
local)
	tree=$top
	linux=${LINUX:-$(cd "$top/../linux" && pwd)}
	linux_mount="-v $linux:/src/linux -v next68k-installer:/v"
	linux_src=/src/linux
	kbuild=/v/kbuild-local
	tree_desc="local $top $(git -C "$top" describe --always --dirty)"
	linux_desc="local $linux $(git -C "$linux" describe --always --dirty)"
	;;
*)
	echo "FROM must be github or local" >&2
	exit 1
	;;
esac

echo "Building the kernel package"
rm -rf "$work/kernel"
mkdir -p "$work/kernel"
docker run --rm $linux_mount -v "$work":/out -w $linux_src next68k-build sh -ec "
	mk='make O=$kbuild ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- -j$jobs'
	\$mk next_defconfig
	rm -f /v/linux-image-*.deb /v/linux-*.buildinfo /v/linux-*.changes \
		/v/linux-libc-dev*.deb /v/linux-headers-*.deb
	\$mk DPKG_FLAGS=-d KDEB_CHANGELOG_DIST=unstable bindeb-pkg > /v/kbuild.log 2>&1 ||
		{ tail -40 /v/kbuild.log; exit 1; }
	cp /v/linux-image-*.deb /out/kernel/
	cp $kbuild/vmlinux /out/kernel/
	cp $kbuild/include/config/kernel.release /out/kernel/release
	"
deb=$(ls "$work"/kernel/linux-image-*.deb)
echo "  $(basename "$deb")"

echo "Building the loader, nextlabel and the udebs"
version=$(cat "$work/kernel/release" | tr - .)~$date
docker run --rm $linux_mount -v "$tree":/tree -v "$work":/out next68k-build sh -ec "
	make -s -C /tree/loader LINUX=$linux_src bootblock BOOTBLOCK=/out/bootblock
	# for test-installer.sh, which makes its own netboot image
	cp /tree/loader/loader.elf /tree/loader/loader.bin /tree/loader/mkimage.py /out/
	m68k-linux-gnu-gcc -O2 -Wall -o /out/nextlabel /tree/installer/nextlabel.c
	m68k-linux-gnu-strip /out/nextlabel

	rm -rf /out/udebs-src /out/udebs
	mkdir -p /out/udebs
	cp -a /tree/installer/udebs /out/udebs-src
	sed -i 's/@VERSION@/$version/' /out/udebs-src/*/DEBIAN/control

	part=/out/udebs-src/next-partitioner
	mkdir -p \$part/usr/bin
	cp /out/bootblock /tree/installer/preseed.cfg \$part/usr/lib/next-installer/
	cp /out/nextlabel \$part/usr/bin/
	lib=/out/udebs-src/next-installer/usr/lib/next-installer
	mkdir -p \$lib/target
	cp /out/kernel/linux-image-*.deb \$lib/
	# The settings from debian-root/ for any NeXT; the installer writes
	# fstab, hostname, hosts, resolv.conf and the kernel command line
	# itself.
	cd /tree/debian-root
	for f in etc/kernel/postinst.d/zz-next-boot \
		etc/udev/udev.conf.d/next.conf \
		etc/systemd/system.conf.d/next.conf \
		etc/systemd/system/next-early-swap.service \
		etc/systemd/network/20-wired.network \
		etc/systemd/system/systemd-modules-load.service.d/next.conf \
		etc/systemd/system/systemd-journal-flush.service.d/next.conf \
		etc/X11/xorg.conf.d/20-next.conf \
		etc/X11/Xresources/xterm-next \
		etc/xdg/fastfetch/config.jsonc; do
		mkdir -p \$lib/target/\$(dirname \$f)
		cp -p \$f \$lib/target/\$f
	done

	for d in /out/udebs-src/*; do
		dpkg-deb --root-owner-group -Zxz -b \$d \
			/out/udebs/\$(basename \$d)_${version}_m68k.udeb > /dev/null
	done
	ls /out/udebs
	"

echo "Assembling the installer's initrd from $mirror"
docker run --rm -v "$tree":/tree -v "$work":/out -v "$out":/inst next68k-build \
	python3 /tree/installer/mkinitrd.py --mirror "$mirror" \
		--packages /tree/installer/packages --local /out/udebs \
		--cache /inst/cache --tree /tmp/tree \
		--version "next68k-$date" --manifest /out/udebs.list \
		-o /out/initrd.gz

echo "Making the netboot image and the installer disk"
docker run --rm $linux_mount -v "$tree":/tree -v "$work":/out next68k-build sh -ec "
	make -s -C /tree/loader LINUX=$linux_src image KERNEL=/out/kernel/vmlinux \
		INITRD=/out/initrd.gz CMDLINE='$cmdline' OUTPUT=/out/boot
	rm -rf /out/deb && dpkg-deb -x /out/kernel/linux-image-*.deb /out/deb
	cp /out/deb/boot/vmlinuz-* /out/vmlinuz

	# Partition a holds README.txt; the boot partition the kernel and
	# initrd.
	mkdir -p /out/disk-a && cp /tree/installer/README.txt /out/disk-a/
	mke2fs -q -t ext4 -L installer -d /out/disk-a /out/disk-a.img 4M
	k=\$(stat -c %s /out/vmlinuz)
	i=\$(stat -c %s /out/initrd.gz)
	mb=\$(( (k + i) / 1048576 + 2 ))
	python3 /tree/scripts/mkdisk.py -o /out/installer.img --name 'Debian installer' \
		--boot \$mb --bootblock /out/bootblock /out/disk-a.img:ext4 > /dev/null
	python3 /tree/scripts/nextboot.py kernel /out/installer.img /out/vmlinuz \
		--initrd /out/initrd.gz --cmdline '$cmdline' > /dev/null
	"

for f in boot installer.img initrd.gz vmlinuz bootblock; do
	cp "$work/$f" "$out/$f"
done
rm -f "$out"/linux-image-*.deb
cp "$deb" "$out/"
cp "$tree/installer/README.txt" "$out/README.txt"
{
	echo "Debian installer for the NeXT, built $(date -u '+%Y-%m-%d %H:%M UTC')"
	echo
	echo "next68k-linux: $tree_desc"
	echo "Linux:         $linux_desc"
	echo "Kernel:        $(cat "$work/kernel/release"), $(basename "$deb")"
	echo "Command line:  $cmdline"
	echo "Udebs from:    $mirror unstable"
	echo
	sed 's/^/  /' "$work/udebs.list"
} > "$out/MANIFEST"
(cd "$out" && shasum -a 256 boot installer.img initrd.gz vmlinuz bootblock \
	linux-image-*.deb README.txt MANIFEST > SHA256SUMS)
name=next68k-debian-installer-$date
rm -rf "$work/$name" "$out"/next68k-debian-installer-*.tar.xz
mkdir "$work/$name"
(cd "$out" && cp README.txt MANIFEST SHA256SUMS boot installer.img initrd.gz \
	vmlinuz bootblock "$(basename "$deb")" "$work/$name/")
tar -C "$work" -cJf "$out/$name.tar.xz" "$name"
rm -rf "$work/$name"
ls -l "$out"
