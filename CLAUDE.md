# CLAUDE.md

Linux (and Debian m68k) on the 68030/68040 NeXT computers, developed and
tested in the Previous emulator. This repository holds everything but the
kernel: the boot loader, build and emulator scripts, the Debian image
setup, tests, the NeXTdimension's i860 firmware, the Previous patch and
all documentation. Start every session by reading the status at the top
of `ROADMAP.md`.

## Where things are

Sibling checkouts next to this one (`~/git/`):

| Path | What |
|---|---|
| `../linux` | The kernel: local branch `next68k`, tracking `origin/next68k-linux` on github.com/dannydorazio/linux; the Torvalds tree is the `upstream` remote. The NeXT code is `arch/m68k/next/`, `arch/m68k/include/asm/nexthw.h` and the drivers listed under Status in `README.md`. |
| `../trunk` | Previous, SVN trunk r1864 (https://svn.code.sf.net/p/previous/code/trunk), with the series in `previous/patches/` applied (`previous/patches.py apply`), built in `../trunk/build`. |
| `../NeXTDimension` | NeXT's NeXTdimension sources (github.com/johnsonjh/NeXTDimension): i860 ROM, kernel, host driver. Unlicensed and marked confidential by NeXT: never copy from it, and cite public sources (data sheets, Previous, experiment) rather than it. |
| `../src/sys/arch/next68k` | NetBSD's next68k port. Reference only; some files carry the 4-clause BSD license, which the GPL can't take. |

In this repository: `README.md` (usage, layout, status), `ROADMAP.md` (plan,
what was done and learned in each step, and the open issues: known
problems go in its Open issues section), `docs/setup.md` (building
everything on a new machine), `docs/hardware.md` (what we know about the
hardware), `docs/debugging.md` (how to find out what's wrong).
`out/` holds everything built, downloaded or logged, and isn't tracked.

## Ground rules

- **Commit or push only when asked.** Never add `Signed-off-by` (in either
  repository): only the human submitter may. Commit messages say the work
  was developed with AI assistance and end with the Co-Authored-By and
  session trailers. Kernel commits follow kernel style and are split by
  subsystem (`m68k:`, `scsi:`, `net: ethernet:`, `drm/tiny:`, ...).
- **No binary data in git.** Everything in `out/` and every build product
  must be regenerable from what is tracked; `.gitignore` keeps it out. The
  one exception is documentation screenshots, kept small, such as
  `docs/screenshot.png` at the top of `README.md`.
- **Keep the documentation current as work lands**, not at the end: the
  status at the top of `ROADMAP.md`, a Done section per step with what was
  found and how it was tested, and `README.md` for anything about using the
  tools. Knowledge that a later session needs goes into these files (or
  `docs/`), not into anything outside the repository.
- Temporary files go in `out/` (e.g. `out/www` for files served to the
  NeXT), not in the source tree.

## The emulator: rules that protect the disk image

- Start Previous only through `scripts/run-previous.sh` or
  `scripts/boot-wait.sh`, in its own background command. A command that
  gets stopped later takes the emulator with it.
- Stop it only with `scripts/stop-previous.sh`. **Never `pkill Previous`**:
  the user runs Previous instances of their own. SIGTERM often leaves
  Previous hung with `out/disk.img` open, which is why the script falls back
  to SIGKILL.
- **Power the guest off first** (`python3 scripts/serial-run.py --login
  poweroff`, wait for `reboot: Power down` in `out/serial-a.log`). If it
  hangs: `scripts/sysrq.sh s`, then `u`, then `o`.
- **Never two emulators on one `out/disk.img`**, and nothing that writes
  it (`update-disk.sh`, `nextboot.py`, `build-disk.sh`) while
  `lsof out/disk.img` shows a user. `run-previous.sh` refuses to start
  while the image is open; a just-stopped Previous can hold it a few
  seconds longer.

## Everyday workflows

```sh
./scripts/build-kernel.sh                 # kernel from ../linux (Docker volume next68k-kbuild)
./scripts/build-image.sh INITRD= CMDLINE="earlyprintk console=ttyPZ0,9600 root=/dev/sda1 rootwait rw sysrq_always_enabled"
./scripts/boot-wait.sh MACHINE=cube       # also station, color, turbo, turbo-color, cube-turbo, cube030; ND=1, BOOT=disk, NET=0, FAST=0
python3 scripts/serial-run.py --login "uname -a" "dmesg | tail"
./scripts/screenshot.sh out/screen.png    # then look at it
./scripts/time-download.sh                # network check: time and MD5
./scripts/build-kernel-deb.sh             # then dpkg -i it on the NeXT to update the disk's kernel
python3 previous/patches.py refresh NAME # after changing Previous: rewrite that patch from ../trunk (a new NAME adds one)
make -C ../trunk/build                    # rebuild Previous
./nd-firmware/build.sh                    # after changing ndfw.s
./scripts/build-xdriver.sh                # NeXTdimension X driver + tests/xbench into out/xdriver
```

Check the end of `build-kernel.sh`'s output, not just for `error:`: a
failed link (an `undefined reference`) leaves the old `vmlinux`, which
`build-image.sh` then wraps without complaint.

## Traps learned the hard way

- **Previous's bool settings** (`nSCSI`, `nRTC`, `bTurbo`, ...) accept only
  `TRUE`/`FALSE` and silently ignore anything else.
- **Fast-forward distorts timing.** Under `FAST=8` an idle guest's clock
  races ahead while Previous's network and i860 run on host time, so
  throughput and timeouts measured in the guest are wrong. Measure with
  `FAST=0`. Kernel timeouts that wait for the emulator's own threads must
  be generous.
- **`login` gives up after 300 emulated seconds**, which fast-forward
  turns into about 40 real ones, and the 68030 needs longer than that while
  it's still booting.
- **Spotlight opens `out/disk.img`** for a few seconds after Previous has
  written it (`mdworker_shared` in `lsof`), and `run-previous.sh` then
  refuses to start: wait for it rather than stopping it.
- **`out/serial-a.log` is cleared when Previous starts**: anything that
  waits on it right after a start may see the previous run's lines.
  `serial-run.py` without `--login` types into a login prompt.
- **`readl`/`writel` are little-endian on m68k**; NeXT registers are
  big-endian: use `__raw_readl`/`__raw_writel` or the `next_dma_*` helpers.
- **The Mac's disk is case-insensitive.** 13 kernel files whose names
  differ only in case are marked `skip-worktree` in `../linux`
  (`git ls-files -v | grep ^S` lists them); they aren't changes. The kernel
  builds in a Docker volume for the same reason.
- Previous runs the NeXTdimension's i860 in its own thread; memory shared
  between it and the 68040 needs the ordered accesses the patch adds.
