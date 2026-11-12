#!/bin/sh
# Build a Debian package of the kernel (make bindeb-pkg) from the object
# tree that build-kernel.sh uses, and copy it to out/debs/.  The kernel
# has no modules or tools, so the build-dependency check is skipped.
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
linux=${LINUX:-$(cd "$top/../linux" && pwd)}
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}

mkdir -p "$top/out/debs"
exec docker run --rm \
	-v "$linux":/src/linux \
	-v next68k-kbuild:/build \
	-v "$top":/work \
	-w /src/linux \
	next68k-build sh -ec "
		rm -f /build/linux-*.deb /build/linux-*.buildinfo /build/linux-*.changes
		make O=/build/linux ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- \
			-j$jobs DPKG_FLAGS=-d KDEB_CHANGELOG_DIST=unstable bindeb-pkg
		rm -f /work/out/debs/linux-*.deb
		cp /build/linux-image-*.deb /work/out/debs/
		ls -l /work/out/debs
	"
