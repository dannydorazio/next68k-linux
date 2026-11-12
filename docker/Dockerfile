# Cross build environment for Linux/m68k on NeXT.
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-m68k-linux-gnu binutils-m68k-linux-gnu libc6-dev-m68k-cross \
        make bc flex bison python3 perl cpio kmod file rsync ca-certificates \
        libelf-dev libssl-dev gcc libc6-dev e2fsprogs \
        dpkg-dev debhelper-compat kmod \
        mmdebstrap arch-test qemu-user-static binfmt-support debian-ports-archive-keyring \
    && rm -rf /var/lib/apt/lists/*

ENV ARCH=m68k CROSS_COMPILE=m68k-linux-gnu-
WORKDIR /work
