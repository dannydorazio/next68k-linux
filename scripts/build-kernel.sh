#!/bin/sh
# Build the NeXT kernel in the cross-compile container.
#
# The Linux source tree is bind-mounted read-mostly from the host; the object
# tree lives on a Docker volume because the host filesystem is
# case-insensitive.
#
# usage: build-kernel.sh [make targets...]   (default: next_defconfig vmlinux)
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
linux=${LINUX:-$(cd "$top/../linux" && pwd)}
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}

[ $# -eq 0 ] && set -- next_defconfig vmlinux

exec docker run --rm \
	-v "$linux":/src/linux \
	-v next68k-kbuild:/build \
	-w /src/linux \
	next68k-build \
	make O=/build/linux ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- -j"$jobs" "$@"
