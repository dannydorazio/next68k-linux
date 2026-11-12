# Previous

The emulator is Previous, SVN trunk r1864
(https://svn.code.sf.net/p/previous/code/trunk), checked out as `../trunk`
and changed by the patches in `patches/`, one per change, in the order
listed in `patches/series`. Each patch starts with a description: what
it changes and why. `previous.cfg.in` is the configuration
`scripts/run-previous.sh` fills in.

| Patch | What | Upstream? |
|---|---|---|
| `0001-scc-reset-external-status-interrupt` | The SCC's reset-external/status-interrupts command releases the interrupt | Yes |
| `0002-esp-pio-data-out` | ESP transfers without DMA: data-out implemented, data-in reports bus service | Yes |
| `0003-m68040-locked-rmw-fault` | A fault on the read cycle of a 68040 locked read-modify-write (CAS, TAS) has no pending writeback | Yes (the code comes from WinUAE: check it too) |
| `0004-ethernet-wire-speed` | Received Ethernet frames at 10 Mbit/s, not 2.4 times faster | Yes |
| `0005-dimension-ordered-dram` | NeXTdimension DRAM accesses ordered between the host and the i860 thread | Yes |
| `0006-host-interface` | Serial ports on ptys and logs, break and NMI on signals, screenshots, keyboard and mouse input from a FIFO: what the scripts drive Previous with | Maybe, as an option |
| `0007-cpu-setjmp` | `_setjmp`/`_longjmp` in the CPU core's exception macros | Maybe |
| `0008-idle-stop` | A CPU stopped by STOP skips to the next event | Maybe |
| `0009-fast-forward` | `PREVIOUS_FAST_FORWARD=N` runs up to N times real time | Maybe |

Not patched yet: the built-in NFS server drops pipelined requests over TCP
(use UDP), to be reported.

## Working on it

```sh
cd ~/git/trunk && python3 ../next68k-linux/previous/patches.py apply   # on a clean r1864 checkout
make -C build                                                             # see docs/setup.md
# change ../trunk, then rewrite the patch the change belongs to:
python3 previous/patches.py refresh 0004-ethernet-wire-speed
# or start a new one, added at the end of the series:
python3 previous/patches.py refresh 0010-something-new
```

`refresh` works out the patch from the pristine sources and the other
patches, so the patches before and after it stay as they are; if a change
overlaps a later patch, it says so. Give a new patch its description in
place of the placeholder at its top. The series also works with quilt
(`series` uses quilt's `-p0`), though that hasn't been tried here.

To move to a newer Previous: update `../trunk` (`svn revert -R src && svn
update`), apply the series, and fix what no longer applies with `refresh`.
