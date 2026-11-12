#!/bin/sh
# Build out/debian-root.img: an ext4 image with Debian m68k (Debian Ports,
# unstable), configured for the NeXT: hostname, fstab, DHCP on eth0 via
# systemd-networkd, the files in debian-root/ and our kernel package from
# out/debs/ (build-kernel-deb.sh).  e2scrub_reap, which only cleans up LVM
# snapshots, is disabled: it takes minutes of CPU at every boot.  login gets
# 5 minutes instead of 60 seconds (LOGIN_TIMEOUT): while the rest of the
# system is still starting, loading its PAM modules alone can take longer
# than a minute at 25 MHz.  Passwords are hashed with SHA-512 crypt (5000
# rounds) instead of yescrypt, which takes minutes to check at 25 MHz.  The
# root password is "next".
#
# Packages listed in debian-packages are installed too.  The package lists
# and apt's caches are kept in the image, built by m68k apt so they're in
# its own format: on the NeXT, apt-get update and the first apt install
# take the better part of an hour otherwise.
#
# m68k programs in the new system run through qemu-user; this registers
# qemu-m68k with binfmt_misc in the Docker VM if needed (a system-wide
# setting there that lasts until the VM restarts).
#
# usage: build-debian.sh [image size in MB, default 1536]
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
size=${1:-1536}
mirror=${MIRROR:-http://deb.debian.org/debian-ports}
packages=systemd-sysv,udev,dbus,iproute2,iputils-ping,netbase,procps,less,nano,ca-certificates,debian-ports-archive-keyring,e2fsprogs,kmod
extra=$(grep -v '^#' "$top/debian-packages" | tr -s ' \n' ',' | sed 's/^,//; s/,$//')
packages=$packages${extra:+,$extra}

exec docker run --rm --privileged -v "$top":/work next68k-build sh -ec "
	binfmt=/proc/sys/fs/binfmt_misc
	mountpoint -q \$binfmt || mount -t binfmt_misc binfmt_misc \$binfmt
	[ -e \$binfmt/qemu-m68k ] ||
		grep -v '^#' /usr/lib/binfmt.d/qemu-m68k.conf > \$binfmt/register

	mmdebstrap --arch=m68k --variant=minbase \
		--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
		--include=$packages \
		--customize-hook='cp -a /work/debian-root/. \"\$1\"/' \
		--customize-hook='sed -i \"s/pam_unix.so obscure yescrypt/pam_unix.so obscure sha512 rounds=5000/\" \"\$1\"/etc/pam.d/common-password' \
		--customize-hook='sed -i \"s/^ENCRYPT_METHOD.*/ENCRYPT_METHOD SHA512/\" \"\$1\"/etc/login.defs' \
		--customize-hook='echo root:next | chroot \"\$1\" chpasswd -c SHA512 -s 5000' \
		--customize-hook='chroot \"\$1\" systemctl enable systemd-networkd' \
		--customize-hook='chroot \"\$1\" systemctl disable e2scrub_reap.service' \
		--customize-hook='sed -i \"s/^LOGIN_TIMEOUT.*/LOGIN_TIMEOUT\t\t300/\" \"\$1\"/etc/login.defs' \
		--customize-hook='mkdir -p \"\$1\"/tmp/debs && cp /work/out/debs/linux-image-*.deb \"\$1\"/tmp/debs/' \
		--customize-hook='chroot \"\$1\" sh -c \"dpkg -i /tmp/debs/*.deb && rm -r /tmp/debs\"' \
		--customize-hook='mount --bind /etc/resolv.conf \"\$1\"/etc/resolv.conf; chroot \"\$1\" apt-get update; s=\$?; umount \"\$1\"/etc/resolv.conf; exit \$s' \
		--customize-hook='rm -f \"\$1\"/var/cache/apt/archives/*.deb' \
		--skip=cleanup/apt \
		unstable /tmp/root '$mirror'

	echo 'Debian on NeXT root disk' > /tmp/root/etc/disk-root
	mke2fs -q -t ext4 -L debian -d /tmp/root /work/out/debian-root.img ${size}M
	du -sh /tmp/root
	ls -l /work/out/debian-root.img
"
