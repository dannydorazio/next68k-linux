#!/bin/sh
# Stop the Previous started by run-previous.sh (the PID in out/previous.pid)
# and wait until nothing has out/disk.img open any more.  Previous doesn't
# always exit on SIGTERM; it then gets SIGKILL after 10 seconds, which is
# like switching the machine off, so sync or shut down the guest first.
#
# usage: stop-previous.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
pid=$(cat "$top/out/previous.pid" 2>/dev/null || true)

if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
	kill "$pid"
	i=0
	while kill -0 "$pid" 2>/dev/null && [ $i -lt 10 ]; do
		sleep 1
		i=$((i + 1))
	done
	if kill -0 "$pid" 2>/dev/null; then
		echo "PID $pid ignored SIGTERM, sending SIGKILL" >&2
		kill -9 "$pid"
	fi
fi

if users=$(lsof -t "$top/out/disk.img" 2>/dev/null); then
	echo "out/disk.img is still open in PID $(echo $users)" >&2
	exit 1
fi
