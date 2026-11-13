#!/bin/sh
# Netboot out/nfsroot/private/tftpboot/boot in Previous, or with BOOT=disk
# boot from out/disk.img's boot blocks and boot partition
# (scripts/nextboot.py); BOOT=rom stops at the ROM monitor's prompt, where
# scripts/input.sh can type a boot command.  NET=0 unplugs the Ethernet
# cable.  DISK= uses another image as the disk (SCSI ID 0), DISK2= adds a
# second one (ID 1), and NFSROOT= another directory for Previous's
# BOOTP/TFTP and NFS server (default out/nfsroot, which serves
# out/nfsroot/private/tftpboot/boot).
#
# Previous gets its own $HOME (out/previous-home) so this configuration
# does not touch your normal Previous settings.
#
# The first serial port (SCC channel A) is on the pseudo terminal out/ttyA;
# attach to it with scripts/console.py.  Everything sent on it is also
# logged to out/serial-a.log.  The second port (channel B, ttyPZ1) is on
# out/ttyB and logged to out/serial-b.log.  scripts/sysrq.sh sends a magic
# SysRq.  scripts/input.sh types on the NeXT's keyboard and moves its mouse
# (through the FIFO out/input).  scripts/stop-previous.sh stops it; it
# refuses to start while anything has one of its disk images open.
#
# MACHINE picks the emulated machine: cube (default; NeXTcube, 68040,
# 64 MB), station (NeXTstation, 32 MB), color (NeXTstation Color, 32 MB),
# turbo, turbo-color and cube-turbo (33 MHz, 128 MB), or cube030 (the
# original NeXT Computer: 68030 and 68882, 64 MB).  CPU=68060 puts a 68060
# in a Turbo (turbo, turbo-color or cube-turbo), a machine NeXT never made
# (Previous patch 0011; the Linux kernel needs M68060).
#
# ND=1 puts a NeXTdimension board (16 MB, Previous's ROM) in slot 2 of a
# cube and shows its screen instead of the built-in display's, also in
# scripts/screenshot.sh.  ND_BANK=16 gives the board 4 x 16 MB, the most
# Previous allows (default 4: 4 x 4 MB).  ND_SLOTS="4" puts it in slot 4
# instead, ND_SLOTS="2 4" puts one in each; the screen shown is the first's.
#
# I860=N (default 1) lets the NeXTdimension's i860 execute N times as many
# instructions as a 33 MHz one would (PREVIOUS_I860_SPEED), when the host
# can keep up; I860=max removes the limit.
#
# FAST=N (default 8) lets the emulated machine run up to N times as fast as
# real time (PREVIOUS_FAST_FORWARD), when the host can keep up; FAST=max
# removes the limit and FAST=0 keeps it to real time.  Inside the machine
# all timing stays consistent; only the wall clock runs fast, so timings
# measured this way don't match a real NeXT, and an idle machine's clock
# runs ahead of the host's.  The limit keeps that sane for typing and for
# the network.
#
# Needs a Previous built with previous/patches (docs/setup.md).
set -e

color=FALSE
bank=16
turbo=FALSE
cpulevel=4
cpufreq=25
fpu=68040
scsi=TRUE	# NCR53C90A (FALSE: NCR53C90)
rtc=FALSE	# MC68HC68T1 (TRUE: MCS1850); both are booleans to Previous
dspmem=TRUE
nd=FALSE nd0=FALSE nd1=FALSE nd2=FALSE
ndbank=${ND_BANK:-4}
screenslot=0
if [ "${ND:-0}" = 1 ]; then
	for slot in ${ND_SLOTS:-2}; do
		case $slot in
		2)	nd0=TRUE ;;
		4)	nd1=TRUE ;;
		6)	nd2=TRUE ;;
		*)	echo "ND_SLOTS takes slots 2, 4 and 6" >&2; exit 1 ;;
		esac
		[ $screenslot != 0 ] || screenslot=$slot
	done
	nd=TRUE
fi
case $ndbank in
4|16)	;;
*)	echo "ND_BANK must be 4 or 16" >&2; exit 1 ;;
esac
case ${BOOT:-net} in
net)	bootdev=2 ;;
disk)	bootdev=1 ;;
rom)	bootdev=0 ;;
*)	echo "BOOT must be net, disk or rom" >&2; exit 1 ;;
esac
case ${NET:-1} in
0)	ethernet=FALSE ;;
*)	ethernet=TRUE ;;
esac
case ${MACHINE:-cube} in
cube)		machinetype=1; nbic=TRUE ;;
station)	machinetype=2; nbic=FALSE ;;
color)		machinetype=2; nbic=FALSE; color=TRUE; bank=8 ;;
turbo)		machinetype=2; nbic=FALSE; turbo=TRUE ;;
turbo-color)	machinetype=2; nbic=FALSE; turbo=TRUE; color=TRUE ;;
cube-turbo)	machinetype=1; nbic=TRUE; turbo=TRUE ;;
cube030)	machinetype=0; nbic=TRUE; cpulevel=3; fpu=68882; scsi=FALSE
		dspmem=FALSE ;;
*)		echo "MACHINE must be cube, station, color, turbo, turbo-color," \
			"cube-turbo or cube030" >&2; exit 1 ;;
esac
if [ $nd = TRUE ] && [ $nbic = FALSE ]; then
	echo "ND=1 needs a cube (MACHINE=cube, cube-turbo or cube030)" >&2
	exit 1
fi
if [ $turbo = TRUE ]; then
	bank=32 cpufreq=33 rtc=TRUE
fi
case ${CPU:-default} in
default)	;;
68060)	if [ $turbo != TRUE ]; then
		echo "CPU=68060 needs a Turbo's ROM (MACHINE=turbo," \
			"turbo-color or cube-turbo)" >&2
		exit 1
	fi
	cpulevel=5 ;;
*)	echo "CPU must be 68060" >&2; exit 1 ;;
esac

top=$(cd "$(dirname "$0")/.." && pwd)
previous=${PREVIOUS:-$top/../trunk/build/src/Previous.app/Contents/MacOS/Previous}
home=$top/out/previous-home
cfgdir="$home/Library/Application Support/Previous"
abspath() {
	echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
}
disk0=$(abspath "${DISK:-$top/out/disk.img}")
disk1= disk1type=0 disk1in=FALSE
if [ "$DISK2" ]; then
	disk1=$(abspath "$DISK2") disk1type=1 disk1in=TRUE
fi
nfsroot=$(abspath "${NFSROOT:-$top/out/nfsroot}")

# Two machines on one disk image corrupt it.
for disk in "$disk0" $disk1; do
	if users=$(lsof -t "$disk" 2>/dev/null); then
		echo "$disk is in use by PID $(echo $users)." >&2
		echo "Stop that first (scripts/stop-previous.sh)." >&2
		exit 1
	fi
done

mkdir -p "$cfgdir" "$nfsroot/private/tftpboot"
sed -e "s|@TOP@|$top|g" -e "s|@MACHINETYPE@|$machinetype|" -e "s|@NBIC@|$nbic|" \
	-e "s|@COLOR@|$color|" -e "s|@BANK@|$bank|" \
	-e "s|@BOOTDEV@|$bootdev|" -e "s|@ETHERNET@|$ethernet|" \
	-e "s|@TURBO@|$turbo|" -e "s|@CPULEVEL@|$cpulevel|" \
	-e "s|@CPUFREQ@|$cpufreq|" -e "s|@FPU@|$fpu|" -e "s|@SCSI@|$scsi|" \
	-e "s|@RTC@|$rtc|" -e "s|@DSPMEM@|$dspmem|" \
	-e "s|@ND0@|$nd0|" -e "s|@ND1@|$nd1|" -e "s|@ND2@|$nd2|" \
	-e "s|@NDBANK@|$ndbank|" \
	-e "s|@DISK0@|$disk0|" -e "s|@DISK1@|$disk1|" \
	-e "s|@DISK1TYPE@|$disk1type|" -e "s|@DISK1IN@|$disk1in|" \
	-e "s|@NFSROOT@|$nfsroot|" \
	-e "s|@SCREENSLOT@|$screenslot|" \
	"$top/previous/previous.cfg.in" > "$cfgdir/previous.cfg"
: > "$top/out/serial-a.log"
: > "$top/out/serial-b.log"
rm -f "$top/out/input"
mkfifo "$top/out/input"
echo $$ > "$top/out/previous.pid"	# exec keeps the PID
printf '%s\n' "$disk0" $disk1 > "$top/out/previous.disks"	# for stop-previous.sh

export PREVIOUS_FAST_FORWARD=${FAST:-8}
export PREVIOUS_I860_SPEED=${I860:-1}

HOME="$home" \
PREVIOUS_SCC_A_LOG="$top/out/serial-a.log" \
PREVIOUS_SCC_A_PTY="$top/out/ttyA" \
PREVIOUS_SCC_B_LOG="$top/out/serial-b.log" \
PREVIOUS_SCC_B_PTY="$top/out/ttyB" \
PREVIOUS_SCREENSHOT="$top/out/screen.png" \
PREVIOUS_INPUT="$top/out/input" \
	exec "$previous"
