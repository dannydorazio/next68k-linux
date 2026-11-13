#!/bin/sh
# Test the installer from build-installer.sh in Previous: install Debian on
# a blank disk with every question answered (installer/test-preseed.cfg,
# added to the initrd), on the serial console, then boot the installed
# disk and log in.  Uses out/installer-test/ only: its own target disk
# and netboot directory, never out/disk.img.
#
#   MEDIA=net (default): netboot the installer, the target disk at SCSI ID 0
#   MEDIA=disk: boot the installer's disk (a copy of installer.img) at ID 0,
#     with the target disk at ID 1; the installed system then boots with the
#     target alone, as sda
#
# MACHINE, CPU and FAST are run-previous.sh's (default cube: 64 MB).  SIZE is
# the target disk in MB (default 2048).  TIMEOUT is in seconds for the
# install (default 6 hours).  Leaves the installed disk in
# out/installer-test/target.img, logs in out/installer-test/.
#
# usage: [MEDIA=net|disk] [MACHINE=...] test-installer.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
inst=$top/out/installer
work=$inst/build
test=$top/out/installer-test
media=${MEDIA:-net}
size=${SIZE:-2048}
timeout=${TIMEOUT:-21600}
cmdline="console=ttyPZ0,9600 priority=critical"

[ -f "$work/initrd.gz" ] || { echo "no installer: run build-installer.sh" >&2; exit 1; }
if pid=$(cat "$top/out/previous.pid" 2>/dev/null) && kill -0 "$pid" 2>/dev/null; then
	echo "Previous is running (PID $pid): stop it first" >&2
	exit 1
fi

rm -rf "$test"
mkdir -p "$test/nfsroot/private/tftpboot" "$test/preseed"

# The answers, as /preseed.cfg in a second cpio archive after the
# installer's, in the same gzip stream: the kernel unpacks both.  The
# installer loads it after its own defaults, and with it runs only on the
# serial console.
disk=sda
[ $media = net ] || disk=sdb
sed "s|^d-i next-partitioner/disk select .*|d-i next-partitioner/disk select $disk|" \
	"$top/installer/test-preseed.cfg" > "$test/preseed/preseed.cfg"
docker run --rm -v "$work":/work -v "$test":/test next68k-build sh -ec '
	cd /test/preseed
	{ gzip -dc /work/initrd.gz; echo preseed.cfg | cpio -o -H newc --quiet -R 0:0; } |
		gzip -9 > /test/initrd'


truncate -s ${size}M "$test/target.img" 2>/dev/null ||
	mkfile -n ${size}m "$test/target.img"
case $media in
net)
	python3 "$work/mkimage.py" --loader "$work/loader.elf" \
		--loader-bin "$work/loader.bin" --kernel "$work/kernel/vmlinux" \
		--initrd "$test/initrd" --cmdline "$cmdline" \
		-o "$test/nfsroot/private/tftpboot/boot" > /dev/null
	set -- BOOT=net DISK="$test/target.img"
	;;
disk)
	cp "$inst/installer.img" "$test/installer.img"
	python3 "$top/scripts/nextboot.py" kernel "$test/installer.img" \
		"$work/vmlinuz" --initrd "$test/initrd" --cmdline "$cmdline" > /dev/null
	set -- BOOT=disk DISK="$test/installer.img" DISK2="$test/target.img"
	;;
*)
	echo "MEDIA must be net or disk" >&2
	exit 1
	;;
esac

wait_for() {	# pattern seconds
	t=0
	while ! grep -qaE "$1" "$top/out/serial-a.log" 2>/dev/null; do
		if [ $t -ge $2 ]; then
			echo "timed out after $2 s waiting for: $1" >&2
			return 1
		fi
		if [ $((t % 600)) = 0 ]; then
			"$top/scripts/screenshot.sh" "$test/screen-$t.png" > /dev/null 2>&1 || true
			echo "  $(date +%H:%M) $(tr -c '[:print:]\n' ' ' < "$top/out/serial-a.log" |
				grep -aoE '(Installing|Configuring|Unpacking|Retrieving|Validating|Extracting|Partitioning|Setting up|Downloading|Running)[^ ]*( [^ ]+){0,4}' |
				tail -1)"
		fi
		sleep 10
		t=$((t + 10))
	done
}

start() {
	nohup env "$@" NFSROOT="$test/nfsroot" "$top/scripts/run-previous.sh" \
		> "$test/previous-stdout.log" 2>&1 &
	sleep 5
}

echo "Installing ($media, ${MACHINE:-cube}${CPU:+, $CPU}): started $(date +%H:%M)"
start "$@"
t0=$(date +%s)
wait_for "reboot: Power down|Kernel panic" $timeout || {
	cp "$top/out/serial-a.log" "$test/install-serial.log"; exit 1; }
cp "$top/out/serial-a.log" "$test/install-serial.log"
echo "Install finished in $((($(date +%s) - t0) / 60)) minutes"
"$top/scripts/stop-previous.sh"
grep -qa "Kernel panic" "$test/install-serial.log" && { echo "kernel panic" >&2; exit 1; }

echo "Booting the installed disk"
sleep 5
start BOOT=disk DISK="$test/target.img"
wait_for "login:|Kernel panic" 3600 || exit 1
cp "$top/out/serial-a.log" "$test/boot-serial.log"
python3 "$top/scripts/serial-run.py" --login --timeout 900 \
	"uname -a" "cat /proc/cmdline" "df -h /" "swapon --show" \
	"ip -4 -br addr" "systemctl is-system-running" "systemctl --failed" \
	"fastfetch --pipe" | tee "$test/check.log"
"$top/scripts/screenshot.sh" "$test/installed.png" > /dev/null 2>&1 || true
python3 "$top/scripts/serial-run.py" --login poweroff > /dev/null || true
wait_for "reboot: Power down" 900 || true
"$top/scripts/stop-previous.sh"
echo "Done: logs and screenshots in $test"
