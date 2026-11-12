# NeXTdimension i860 firmware

`ndfw.s` is the program Linux's NeXTdimension driver
(`drivers/gpu/drm/tiny/nextdimension.c`) runs on the board's i860 to draw
the console: fills, copies (scrolling) and glyphs, taken from a command
ring in the board's DRAM. The comment at the top of `ndfw.s` describes the
ring and the commands.

`build.sh` assembles it with binutils 2.30 for the i860 (built into the
`next68k-i860` Docker volume by `scripts/build-i860-binutils.sh`) and
writes the result into the kernel tree as
`drivers/gpu/drm/tiny/nextdimension_fw.h`, an array of instruction words.
The kernel build doesn't need the i860 tools; rerun `build.sh` after
changing `ndfw.s`.

How it starts: the driver writes the CSR0 reset bit, which restarts the
i860 into its ROM. It then stores each instruction word at its address
XOR 4, because the i860 runs big-endian but fetches instructions
little-endian, a doubleword at a time. Finally it sets the CSR0 i860
interrupt bit. The ROM waits for that interrupt and then jumps to the start
of DRAM (see `NextDimension-21/ROM/romcode/locore.s` in NeXT's sources).
The program runs uncached and without paging, and announces itself by
writing `NDFL` into the shared area.

The 68040 and the i860 share the ring. In Previous the i860 runs in its
own thread, so the shared memory needs ordered accesses; Previous's patch
(`previous/previous-linux.patch`, `nd_mem.cpp`) makes them so.
