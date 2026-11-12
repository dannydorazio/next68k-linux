#!/bin/sh
# Raise an NMI on the emulated NeXT, as the keyboard's NMI key combination
# does: Previous turns SIGUSR2 into one.  The kernel prints the registers
# of whatever was running, even with interrupts off, so this also works
# when the machine is stuck where magic SysRq can't help.
#
# usage: nmi.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
kill -USR2 "$(cat "$top/out/previous.pid")"
