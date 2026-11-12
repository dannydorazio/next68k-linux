#!/bin/sh
# Netboot out/nfsroot/private/tftpboot/boot in Previous, or with BOOT=disk
# boot from out/disk.img's boot blocks and boot partition
# (scripts/nextboot.py).  NET=0 unplugs the Ethernet cable.
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
# refuses to start while anything has out/disk.img open.
#
# MACHINE picks the emulated machine: cube (default; NeXTcube, 68040,
# 64 MB), station (NeXTstation, 32 MB), color (NeXTstation Color, 32 MB),
# turbo, turbo-color and cube-turbo (33 MHz, 128 MB), or cube030 (the
# original NeXT Computer: 68030 and 68882, 64 MB).
#
# ND=1 puts a NeXTdimension board (16 MB, Previous's ROM) in slot 2 of a
# cube and shows its screen instead of the built-in display's, also in
# scripts/screenshot.sh.  ND_BANK=16 gives the board 4 x 16 MB, the most
# Previous allows (default 4: 4 x 4 MB).
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
nd=FALSE
ndbank=${ND_BANK:-4}
screenslot=0
if [ "${ND:-0}" = 1 ]; then
	nd=TRUE screenslot=2
fi
case $ndbank in
4|16)	;;
*)	echo "ND_BANK must be 4 or 16" >&2; exit 1 ;;
esac
case ${BOOT:-net} in
net)	bootdev=2 ;;
disk)	bootdev=1 ;;
*)	echo "BOOT must be net or disk" >&2; exit 1 ;;
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

top=$(cd "$(dirname "$0")/.." && pwd)
previous=${PREVIOUS:-$top/../trunk/build/src/Previous.app/Contents/MacOS/Previous}
home=$top/out/previous-home
cfgdir="$home/Library/Application Support/Previous"

# Two machines on one disk image corrupt it.
if users=$(lsof -t "$top/out/disk.img" 2>/dev/null); then
	echo "out/disk.img is in use by PID $(echo $users)." >&2
	echo "Stop that first (scripts/stop-previous.sh)." >&2
	exit 1
fi

mkdir -p "$cfgdir" "$top/out/nfsroot/private/tftpboot"
sed -e "s|@TOP@|$top|g" -e "s|@MACHINETYPE@|$machinetype|" -e "s|@NBIC@|$nbic|" \
	-e "s|@COLOR@|$color|" -e "s|@BANK@|$bank|" \
	-e "s|@BOOTDEV@|$bootdev|" -e "s|@ETHERNET@|$ethernet|" \
	-e "s|@TURBO@|$turbo|" -e "s|@CPULEVEL@|$cpulevel|" \
	-e "s|@CPUFREQ@|$cpufreq|" -e "s|@FPU@|$fpu|" -e "s|@SCSI@|$scsi|" \
	-e "s|@RTC@|$rtc|" -e "s|@DSPMEM@|$dspmem|" \
	-e "s|@ND@|$nd|" -e "s|@NDBANK@|$ndbank|" \
	-e "s|@SCREENSLOT@|$screenslot|" \
	"$top/previous/previous.cfg.in" > "$cfgdir/previous.cfg"
: > "$top/out/serial-a.log"
: > "$top/out/serial-b.log"
rm -f "$top/out/input"
mkfifo "$top/out/input"
echo $$ > "$top/out/previous.pid"	# exec keeps the PID

export PREVIOUS_FAST_FORWARD=${FAST:-8}

HOME="$home" \
PREVIOUS_SCC_A_LOG="$top/out/serial-a.log" \
PREVIOUS_SCC_A_PTY="$top/out/ttyA" \
PREVIOUS_SCC_B_LOG="$top/out/serial-b.log" \
PREVIOUS_SCC_B_PTY="$top/out/ttyB" \
PREVIOUS_SCREENSHOT="$top/out/screen.png" \
PREVIOUS_INPUT="$top/out/input" \
	exec "$previous"
