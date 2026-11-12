#!/bin/sh
# Save the emulated screen of the Previous started by run-previous.sh (the
# PID in out/previous.pid) as a PNG: SIGHUP makes Previous write it to
# out/screen.png (PREVIOUS_SCREENSHOT), so this works even while the host's
# display is locked.
#
# usage: screenshot.sh [FILE]   (default out/screen.png)
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
shot=$top/out/screen.png
file=${1:-$shot}
pid=$(cat "$top/out/previous.pid")

rm -f "$shot"
kill -HUP "$pid"
for i in $(seq 50); do
	[ -s "$shot" ] && break
	sleep 0.1
done
[ -s "$shot" ] || { echo "Previous (PID $pid) saved no screen" >&2; exit 1; }
sleep 0.2	# let it finish writing
[ "$file" = "$shot" ] || mv "$shot" "$file"
echo "$file"
