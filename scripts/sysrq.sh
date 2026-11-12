#!/bin/sh
# Send a magic SysRq to the kernel on the emulated NeXT's serial console:
# SIGUSR1 makes Previous send a break on SCC channel A, then KEY follows.
# The kernel needs sysrq_always_enabled (systemd otherwise allows only
# sync).  E.g. t: all task stacks, w: blocked tasks, p: registers,
# l: backtrace, m: memory, s: sync, b: reboot.
#
# usage: sysrq.sh KEY
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
[ $# -eq 1 ] || { echo "usage: sysrq.sh KEY" >&2; exit 1; }

kill -USR1 "$(cat "$top/out/previous.pid")"
sleep 0.5
printf %s "$1" > "$top/out/ttyA"
