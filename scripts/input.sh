#!/bin/sh
# Type on the emulated NeXT's keyboard, or work its mouse: each argument is
# a command for Previous's host input (PREVIOUS_INPUT, the FIFO out/input
# made by run-previous.sh), as described in previous/previous-linux.patch
# (kms.c):
#
#   type TEXT          US layout; \n is Return, \t Tab, \e Esc, \b Delete
#   key CODE [MODS]    NeXT key code and modifier bits, in hex
#   down CODE MODS, up CODE MODS
#   move DX DY         right and down are positive
#   button left|right down|up
#
# Previous sends the events at typing speed, so they arrive after this
# returns.
#
# usage: input.sh COMMAND...
#   e.g. input.sh 'type root\n'
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
pid=$(cat "$top/out/previous.pid" 2>/dev/null || true)

if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null || [ ! -p "$top/out/input" ]; then
	echo "Previous isn't running (scripts/run-previous.sh)" >&2
	exit 1
fi
# Writing would block until something reads.
if ! lsof -a -p "$pid" -Fn 2>/dev/null | grep -qx "n$top/out/input"; then
	echo "Previous (PID $pid) has no out/input open; is it patched?" >&2
	exit 1
fi
printf '%s\n' "$@" > "$top/out/input"
