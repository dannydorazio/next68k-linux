#!/bin/sh
# Build the test programs in tests/ for the NeXT: each tests/NAME.c becomes
# a static m68k binary tests/NAME, built in the cross-compile container.
# Those that define their own _start are freestanding (they ran on the
# busybox root, before there was a C library) and get no libc.
# Copy them to the NeXT over HTTP (see time-download.sh) or put them in a
# root file system.
#
# usage: build-tests.sh [NAME...]   (default: all of them)
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top/tests"
[ $# -eq 0 ] && set -- $(ls *.c | sed 's/\.c$//')

exec docker run --rm -v "$top":/work -w /work/tests next68k-build sh -ec '
	for t in "$@"; do
		if grep -q _start "$t.c"; then
			flags="-nostdlib -ffreestanding -fno-builtin"
			libs=-lgcc
		else
			flags=-pthread
			libs=
		fi
		m68k-linux-gnu-gcc -static -O2 -Wall $flags -o "$t" "$t.c" $libs
		echo "tests/$t"
	done' sh "$@"
