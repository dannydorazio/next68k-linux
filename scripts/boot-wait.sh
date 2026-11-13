#!/bin/sh
# Start run-previous.sh in the background with the given settings, wait
# until the NeXT shows a login prompt or the kernel panics, and print the
# interesting boot lines (model, clock, memory, SCSI, Ethernet, displays).
#
# usage: boot-wait.sh [VAR=value...]   e.g. boot-wait.sh MACHINE=turbo BOOT=disk
#   TIMEOUT=seconds (default 600) gives up waiting; Previous keeps running.
top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top" || exit 1
for a in "$@"; do export "$a"; done

# a Previous just stopped may still have the disk open for a moment
i=0
while lsof "${DISK:-out/disk.img}" >/dev/null 2>&1 && [ $i -lt 30 ]; do
	sleep 1
	i=$((i + 1))
done
nohup ./scripts/run-previous.sh > out/previous-stdout.log 2>&1 &
sleep 5
if ! kill -0 "$(cat out/previous.pid)" 2>/dev/null; then
	cat out/previous-stdout.log
	exit 1
fi

t=0
while [ $t -lt "${TIMEOUT:-600}" ]; do
	grep -qaE "login:|Kernel panic|Oops|^error|Unable to handle" \
		out/serial-a.log && break
	sleep 5
	t=$((t + 5))
done
grep -aE "^NeXT|Detected|real-time|esp0: is|Memory:|eth0:|drm\] 1120|login:|panic|Oops|^error|Unable" \
	out/serial-a.log | head -20
echo "[waited ${t}s]"
