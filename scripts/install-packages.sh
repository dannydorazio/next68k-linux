#!/bin/sh
# Install Debian packages into the root file system (partition a) of the
# existing out/disk.img on the host, with apt running under qemu-user in
# Docker: much faster than apt on the emulated NeXT.  Previous must not be
# running.  Services aren't started in the chroot (policy-rc.d), and kernel
# packages are refused: their hook (zz-next-boot) would write the boot
# partition of whatever /dev/sda3 is in the container.  Install those on
# the NeXT.
#
# usage: install-packages.sh PACKAGE...
set -e

top=$(cd "$(dirname "$0")/.." && pwd)

[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 1; }
for p in "$@"; do
	case $p in
	linux-image*|*.deb) echo "install kernel packages on the NeXT" >&2; exit 1 ;;
	esac
done
if [ -s "$top/out/previous.pid" ] && kill -0 "$(cat "$top/out/previous.pid")" 2>/dev/null; then
	echo "Previous is running; power the NeXT off first" >&2
	exit 1
fi
if users=$(lsof -t "$top/out/disk.img" 2>/dev/null); then
	echo "out/disk.img is in use by PID $(echo $users)" >&2
	exit 1
fi

# partition a starts after the 160 KB label and front porch (mkdisk.py)
exec docker run --rm --privileged -v "$top":/work next68k-build sh -ec '
	binfmt=/proc/sys/fs/binfmt_misc
	mountpoint -q $binfmt || mount -t binfmt_misc binfmt_misc $binfmt
	[ -e $binfmt/qemu-m68k ] ||
		grep -v "^#" /usr/lib/binfmt.d/qemu-m68k.conf > $binfmt/register

	r=/tmp/root
	mkdir $r
	mount -o loop,offset=163840 /work/out/disk.img $r
	mount -t proc proc $r/proc
	mount --bind /dev $r/dev
	mount --bind /etc/resolv.conf $r/etc/resolv.conf
	printf "#!/bin/sh\nexit 101\n" > $r/usr/sbin/policy-rc.d
	chmod 755 $r/usr/sbin/policy-rc.d
	s=0
	chroot $r sh -c "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $*" || s=$?
	rm -f $r/usr/sbin/policy-rc.d $r/var/cache/apt/archives/*.deb
	umount $r/etc/resolv.conf $r/dev $r/proc
	df -h $r | tail -1
	umount $r
	exit $s
' sh "$@"
