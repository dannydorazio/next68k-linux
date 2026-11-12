# Linux on NeXT: roadmap

Goal: boot Debian m68k (Debian Ports, sid) on the NeXT computers with our
kernel, first in the Previous emulator, then on real hardware, and get the
work upstream.

Kernel work lives on the `next68k` branch of `../linux` (uncommitted so far);
tooling in this directory; emulator patches in `previous/`.

## Where things stand (2026-11-11)

- Part 1, steps 1–5: done. Step 6: done except the overnight soak, which
  is postponed (run `scripts/soak-server.sh` on the host and
  `tests/soak.sh` on the NeXT, at `FAST=0`).
- Part 2, step 7 (framebuffer): done (Done, step 11). Monochrome and
  Color displays, the framebuffer console and userspace DRM, all checked
  on screenshots of the emulated screen. Debian on a 32 MB machine used to
  thrash at boot: fixed in step 10.5.
- Part 2, step 8 (keyboard and mouse): done (Done, step 12). Logging in
  on the screen with the NeXT keyboard works; tested through a host input
  FIFO added to Previous.
- Part 2, step 10.6 (NeXTdimension): done (Done, step 16). A DRM driver
  for the board's 32-bit display, whose console the board's i860 draws
  with our own firmware: scrolling is a copy on the board.
- Part 2, step 10.5 (polish in the emulator): done (Done, step 15). The
  run script had given every machine the wrong SCSI chip and the Turbos the
  wrong clock; the Ethernet driver recovers the frames it used to drop; an
  8 MB disk read-ahead made the 32 MB NeXTstation boot for hours and now
  it boots in 7 minutes; and the 68030 NeXT Computer
  now boots Debian fully, under systemd, to a serial login with no failed
  units.
- Part 2, step 10 (Turbo and 68030): done (Done, step 14). The Turbo boots
  Debian with SCSI DMA and Ethernet (an Ethernet receive race found on the
  way is fixed for all machines); the 68030 needed loader and memory-map
  fixes, and is tested with `init=/bin/bash` since systemd doesn't settle
  at its speed.
- Part 2, step 9 (boot from disk): done (Done, step 13). Previous boots
  Debian from `out/disk.img` with the Ethernet cable out, and installing a
  kernel package updates the kernel it boots. `out/disk.img` got its boot
  partition from the end of swap (now 224 MB); the image before that is
  `out/disk.img.pre-bootpart`.
- Leftover Previous instances (SIGTERM doesn't reliably stop Previous)
  shared `out/disk.img`: one ran Debian from 16:54 on 2026-11-10 alongside
  every later session. They were killed on 2026-11-11. After a journal
  replay, `e2fsck` found only wrong free block and inode counts and fixed
  them; the unrepaired image is `out/disk.img.before-fsck`.
  `out/disk.img.pre-upgrade` checks clean. `scripts/stop-previous.sh` and
  a check in `run-previous.sh` now prevent this.
- Retry the full upgrade (`apt-get update && apt-get -y dist-upgrade`) once
  Debian sid has newer packages; `out/disk.img.pre-upgrade` is the
  rollback.
- Committed on 2026-11-11: the kernel as 11 commits by subsystem on
  `next68k-linux` of https://github.com/dannydorazio/linux (local branch
  `next68k`; the Torvalds tree is the `upstream` remote), and this tree on
  `main` of https://github.com/dannydorazio/next68k-linux. Neither has
  Signed-off-by lines yet. On the Mac's case-insensitive disk, 13 kernel
  files whose names differ only in case (netfilter's `xt_DSCP.c` and
  `xt_dscp.c`, ...) are marked `skip-worktree`, since they can't both
  exist there.

## Done

Steps 1–4 below were tested in Previous on a non-Turbo NeXTstation (68040,
25 MHz, 32 MB); step 5 moved to a NeXTcube (68040, 64 MB).

### 1. Boot loader, early boot, early console

- `loader/`: the ROM netboots it from Previous's TFTP server as
  `/private/tftpboot/boot` (a.out OMAGIC, loads at `0x04380000`). It:
  - sizes RAM from the ROM's SIMM table;
  - builds the Linux/m68k bootinfo (`MACH_NEXT` = 15, `BI_NEXT_*` records);
  - moves the kernel and initrd into place with a trampoline;
  - switches off the caches and the ROM's TT registers, then starts the
    kernel.
- `head.S`: maps the device space at `0xf0000000` and writes the early
  console to SCC channel A.
- `arch/m68k/next/config.c`: parses the bootinfo and reports the model.

### 2. Interrupts, timer, serial shell

- `next/ints.c`: demultiplexes the 32-source interrupt controller.
  Non-Turbo machines can't mask some sources in hardware, so the kernel
  keeps its own enable mask.
- `next/time.c`: the system timer is the clockevent and the 20-bit event
  counter is the clocksource.
- Serial ports via `pmac_zilog` (`ttyPZ0`/`ttyPZ1`). The driver now
  accepts a separate data-register resource; the Mac path is unchanged.
- Busybox initramfs, root shell on `ttyPZ0`.
- Bug found and fixed: the ROM leaves the Ethernet receive DMA running after
  a netboot, and incoming frames overwrote kernel memory. The kernel and the
  loader now stop the Ethernet controller and every DMA channel except
  video.

### 3. SCSI, disk labels, RTC, power-off, reboot

- `drivers/scsi/next_esp.c`: an NCR53C90A front end for the ESP core.
- `block/partitions/next.c`: NeXT disk labels v1–v3 with checksums.
  Partitions a–h appear as 1–8.
- `next/rtc.c`: bit-banged MC68HC68T1/MCS1850 clock through SCR2, used for
  `mach_hwclk`, `rtc-generic` and power-off.
- Reboot: switches the MMU off and restarts the ROM from its reset vector.
- Root file system on a SCSI disk (ext4 inside a NeXT label) works.

### 4. DMA, Ethernet, NFS

- The DMA channel registers are defined in `asm/nexthw.h`. Only non-Turbo
  machines are supported; `next_is_turbo()` lets drivers check.
- SCSI DMA: a 64 KB coherent bounce buffer is used for transfers whose
  length is a multiple of 16 bytes; anything else uses PIO.
- `drivers/net/ethernet/next/mb8795.c`:
  - receive uses two chained buffers, and the saved-limit register
    identifies the frame;
  - transmit sends one frame at a time with the EOP/`+15` limit encoding.
- DHCP, TCP to the host, and NFSv2 over UDP to Previous's built-in server.
- Measured in Previous: about 600 KB/s receive and 360 KB/s transmit over
  10 Mbit, about 0.5 MB/s SCSI. Concurrent disk and network stress shows
  no errors.

### 5. Toward Debian (Part 1, steps 1–3)

- **64 MB:** `run-previous.sh` now emulates a NeXTcube (68040) by default.
  Previous gives it four 16 MB banks; `MACHINE=station` selects the 32 MB
  NeXTstation. Disk, network and the busybox system work unchanged; the
  kernel reports 65528K.
- **Kernel configuration:** `next_defconfig` adds what systemd needs on top
  of what was already on (inotify, signalfd, timerfd, epoll, fhandle,
  seccomp, swap, IPv6): `CGROUPS`, `MEMCG`, `CGROUP_PIDS`, `AUTOFS_FS`,
  `TMPFS_POSIX_ACL`, `TMPFS_XATTR`, `EXT4_FS_POSIX_ACL`, `POSIX_MQUEUE`,
  `FANOTIFY`. Everything is built in (no modules). `build-kernel-deb.sh`
  packages it as `linux-image-7.4.0-rc2-next`.
- **Debian root file system:** `build-debian.sh` runs `mmdebstrap` from
  Debian Ports `unstable` and builds `out/debian-root.img` (1.5 GB ext4,
  188 MB used, 122 packages) in about 4 minutes:
  - systemd 262 as init;
  - DHCP on eth0 via systemd-networkd;
  - hostname `next`, root password `next`;
  - our kernel package installed.

  The m68k half runs under `qemu-user-static`, registered with
  `binfmt_misc` in the Docker VM.

### 6. Debian disk with swap (Part 1, step 4)

- `build-disk.sh debian` builds a 2 GB `out/disk.img`:
  - partition a is `out/debian-root.img`, grown with `resize2fs` to
    1.75 GB;
  - partition b is 256 MB of swap (`SWAP=` to change).

  `build-disk.sh` with no argument still builds the 128 MB busybox disk.
- `mkdisk.py --swap MB` writes the swap header itself, big-endian, for
  4 KB pages. It skips zero blocks, so the 2 GB image takes about 200 MB on
  the host. The Debian `fstab` has `/dev/sda2 none swap sw,nofail`.
- Tested from the busybox initramfs with the Debian disk attached:
  - the kernel finds `sda: [NeXT linux] sda1 sda2` (1.75 GB and 256 MB),
    and `swapon /dev/sda2` adds 262140K;
  - 96 MB of random data in a tmpfs, against 59 MB of usable RAM, sent
    36744 pages to swap and read 25087 back, and all six checksums
    matched;
  - no SCSI errors. Swap ran at about 0.2 MB/s, and the test took about
    20 minutes.

### 7. First Debian boot (Part 1, step 5)

Debian forky/sid with systemd 262 boots from the SCSI disk to `login:` on
the serial console. DHCP works through systemd-networkd, and swap is on. In
the emulator a first boot takes about 7 minutes to `login:` at 25 MHz, and
logging in another minute and a half; later boots about 6 minutes, and 7½ to
a shell.

- **`apt-get update` works, but is slow.** On the NeXT it took 50 minutes,
  33 of them CPU time, mostly unpacking the 16 MB m68k index into 82 MB and
  building apt's cache. `apt install strace` works too.

  `build-debian.sh` now does that work on the host instead. It keeps the
  package lists and m68k apt's caches in the image, and installs the
  packages listed in `debian-packages` there. apt on the NeXT is still
  slow: any command spends a minute or so loading its 37 MB cache, and
  `apt-get install -s hello` took 18 minutes, mostly paging through the
  cache and index in 64 MB of RAM. Adding packages to `debian-packages` is
  the fast way.
- **The disk is persistent.** `build-disk.sh` only replaces `out/disk.img`
  with `FORCE=1`, keeping the old one as `out/disk.img.old`.
  `update-disk.sh` copies `debian-root/` into the existing disk instead.

Four problems turned up on the way:

1. **systemd hung after "Hostname set".** Previous's 68040 MMU emulation was
   wrong: the read cycle of a locked read-modify-write (`cas`, `tas`) to a
   write-protected page produced a fault frame with a valid pending writeback
   of 0. Linux performed that write, then the CPU restarted the `cas`, which
   now compared against the bogus value.

   glibc's `fork()` releases an internal rwlock (`__abort_lock`) with `cas`
   right after the `clone` syscall, while the page is copy-on-write. So the
   lock ended up with a reader count of −1 in both processes. The next
   `sigaction(SIGABRT)` (systemd's `reset_all_signal_handlers()` in every
   forked child) then waited for it forever.

   Fixed in Previous (`cpummu.c`): such a fault now reports LK and RW with no
   pending writeback, as the 68040 does. `tests/cascheck.c` reproduces the bug
   in a few lines, and `tests/forkabrt.c` with glibc. qemu-user doesn't model
   the MMU, so it never showed the problem.
2. **No serial getty, and no swap, on the first boot.** udev's coldplug takes
   minutes at 25 MHz, longer than systemd's 90-second default for devices to
   appear, so `dev-ttyPZ0.device` and `dev-sda2.device` timed out.
   `debian-root/` raises `DefaultDeviceTimeoutSec` to 10 minutes.
3. **`login` timed out before asking for the password.** The `login:`
   prompt appears while the rest of the system is still starting. `login`
   then needed over a minute of CPU just to load its PAM modules, longer
   than its 60-second `LOGIN_TIMEOUT`. (`su`, with the same PAM stack, takes
   5 seconds on an idle system.) An strace showed no waiting, only slow
   work; carrier detect, which looked likely at first, wasn't it.
   `build-debian.sh` sets `LOGIN_TIMEOUT` to 300.

   After that, checking the password timed out instead. Debian hashes
   passwords with yescrypt, which is deliberately expensive and takes
   minutes at 25 MHz. `build-debian.sh` uses SHA-512 crypt (5000 rounds),
   for root's password and in PAM and `login.defs` for new ones.
4. **Slow boots.**
   - `e2scrub_reap` took 6–12 minutes of CPU at every boot, and it only
     cleans up LVM snapshots. `build-debian.sh` disables it.
   - udev processed 651 devices at boot, 581 of them ttys: 512 legacy BSD
     pseudo-terminals and the virtual consoles, neither of which is any use
     here. `next_defconfig` turns off `CONFIG_LEGACY_PTYS` and `CONFIG_VT`
     (VT needs `CONFIG_EXPERT`). Bring VT back with the framebuffer
     (step 7).

New debugging aids:

- **Magic SysRq over the serial console:** `scripts/sysrq.sh`. Previous turns
  SIGUSR1 into a break on SCC channel A.
- **Kernel watchdogs:** `CONFIG_SOFTLOCKUP_DETECTOR` and
  `CONFIG_DETECT_HUNG_TASK` in `next_defconfig`.
- **`tests/ptdump.c`:** ptrace-based dumps of user registers, stack and
  memory.
- **A root shell without `login`:** `systemd.debug_shell=/dev/ttyPZ0` with
  `systemd.mask=serial-getty@ttyPZ0.service`.

### 8. Hardware details (`/proc/hardware`, fastfetch)

- **Kernel:** `next_get_hardware_list()` in `arch/m68k/next/config.c` adds
  these lines to `/proc/hardware`, after the model and memory size:
  - from the bootinfo: board revision and Ethernet address;
  - from the memory map: the memory banks;
  - detected at boot: the clock chip (`next_rtc_chip()` in `rtc.c`);
  - from the model: the nominal CPU clock (25 MHz, or 33 MHz on Turbo), the
    display (1120x832, 2-bit grayscale or 12-bit color) and the on-board
    chips (NCR53C90A, MB8795, Z85C30, DSP56001).

  Each memory bank is printed in whole MB, so the NeXTcube's 65528K shows
  as "63 MB"; printing KB would be better.
- **Debian:** fastfetch finds the host and CPU through DMI or the device
  tree, which m68k doesn't have. `debian-root/etc/xdg/fastfetch/config.jsonc`
  adds Host, CPU, FPU, DSP, Display, SCSI, Ethernet and Clock chip lines from
  `/proc/hardware` and `/proc/cpuinfo`. `fastfetch` is in `debian-packages`.
  On the NeXT it takes about 8 seconds. Installing it with apt there took 21
  minutes.
- **Second serial port:** `run-previous.sh` puts SCC channel B (`ttyPZ1`) on
  `out/ttyB`, and `serial-run.py --port b` drives it, e.g. for a
  `systemd.debug_shell=/dev/ttyPZ1` while the login stays on `ttyPZ0`.

### 9. Hardening (Part 1, step 6, in progress)

- **The "unhandled level 3/5 interrupt" messages are gone.** They were
  ordinary interrupt nesting, not lost interrupts. The sequence:
  1. interrupt X (say the SCSI interrupt, level 3) is taken;
  2. before X's handler has called `irq_enter()`, a level 6 timer interrupt
     preempts it;
  3. on its way out, the timer handler runs softirqs and, since nothing
     else counts as in interrupt, m68k's `local_irq_enable()` lowers the
     interrupt mask to 0;
  4. a second level 3 interrupt then services the SCSI request, and X finds
     nothing left.

  This can happen on real hardware too. Mac's VIA demultiplexers likewise
  return quietly when nothing is pending. `next_irq_demux()` now does the
  same, and only warns (and counts in `irq_err_count`) when a source on the
  level is asserted with no handler, which won't go away by itself. Under
  the load that produced 21 messages before: none, `ERR: 0`.

  Traced with temporary instrumentation in Previous: an event history of
  interrupt acknowledges, exceptions, interrupt status reads and source
  clears, dumped when a handler found its level empty. It isn't in the
  patch; put it back from this description if needed.
- **NMI:** the keyboard's NMI key combination raises it, and on non-Turbo
  machines it can't be masked. Level 7 is edge-triggered, so an uncleared
  NMI doesn't storm, but every later one is lost. The handler now clears it
  at the keyboard/mouse controller (`NEXT_KM_CTRL_OFFSET`,
  `NEXT_KM_NMI_RECEIVED`). Previous raises an NMI on SIGUSR2
  (`scripts/nmi.sh`), which also makes the kernel print the interrupted
  registers when nothing else gets through. Tested: two NMIs, two reports,
  system fine.
- **Ethernet:**
  - when both receive buffers fill before the interrupt is serviced, one
    frame is lost; it's now counted (`rx_missed_errors`) instead of
    vanishing;
  - transmit timeout already stops the DMA channel and wakes the queue;
  - the MB8795 has no link indication, so operstate `UNKNOWN` is correct
    and nothing to fix.

  A 20 MB download took 29 s (about 690 KB/s) with no errors, drops or
  missed frames.
- **SCSI partial bursts: deferred.** Only transfers that aren't a multiple
  of 16 bytes use PIO, and in practice that's small control commands
  (INQUIRY, REQUEST SENSE, MODE SENSE, READ CAPACITY), a few dozen bytes
  each. Block I/O is in 512-byte multiples, and the 65520-byte DMA limit
  keeps the pieces of a 64 KB transfer aligned, so it always uses DMA.
- **PID 1's CPU use: not a problem.** At idle it used 1 second in 2
  minutes; its time goes on booting (1 min 39 s) and on reacting to
  apt/dpkg. The earlier "fifth of the CPU" was measured during apt work.

### 10. A faster emulator

Profiling Previous (macOS `sample`) at real speed: its CPU thread slept
about half the time to stay in step with real time, and of the rest about
29% went on MMU address translation, 15% on system calls from `setjmp()`
and 15% on the guest's idle `stop` loop. Three changes, in the patch:

- **Fast-forward:** `PREVIOUS_FAST_FORWARD=N` lets emulated time run up to
  N times as fast as real time, measured per sync interval. Unlimited
  (`max`) made an idle guest's clock race ahead (login's timeout passing
  while typing, a 20 MB download taking 7 minutes of guest time), hence the
  cap. `run-previous.sh` uses `FAST=8` by default.
- **`_setjmp`/`_longjmp`** for the CPU core's `TRY`/`THROW`: plain
  `setjmp()` saves the signal mask, which costs system calls on macOS.
- **Idle skipping:** while the CPU is stopped, `run_other_MPUs()` adds the
  cycles up to the next event at once instead of executing `stop` every 4
  cycles. Events still fire at the same emulated times.

Debian boot to a shell: 7½ minutes at real speed; 128 s with fast-forward
alone; 96 s with `_setjmp`; 85 s with idle skipping (uncapped); 94 s with
the 8x cap. `cascheck` and `regcheck` still pass. Networking works with
fast-forward: DHCP, a 20 MB download, and `apt-get update` (41½ minutes of
guest time, about 9½ minutes of wall time) followed by `apt-get
dist-upgrade` (nothing to upgrade the same day the image was built). Try a
real upgrade once sid has moved on; `out/disk.img.pre-upgrade` is an APFS
clone of the disk from before.

Not done: Previous's MMU translation caches (`MMU_DPAGECACHE`,
`MMU_IPAGECACHE`) are compiled out. Turning them on could save part of the
29%, but the data cache's lookup for a locked RMW read ignores write
protection, which would bring back the `cas` copy-on-write bug (Done, step
7) unless fixed there too.

### 11. Framebuffer (Part 2, step 7)

`drivers/gpu/drm/tiny/nextdisplay.c`, a DRM driver for the 1120×832
display. `config.c` registers `next-display` on monochrome machines (video
memory at `0x0b000000`, 1152 pixels per line) and `next-color-display` on
Color ones (`0x2c000000`, 1152 pixels of RGBA4444, plus the video control
register at `0x02118180`); on Turbo machines both are at `0x0c000000` with
1120-pixel lines (untested). Framebuffers live in system memory (GEM shmem)
and changed areas are copied to the video memory a 32-bit word at a time.

- **Monochrome:** 2-bit pixels, leftmost in the high bits, 0 white and 3
  black. The native format is `DRM_FORMAT_C2`, whose 4-entry gamma LUT is
  the palette, mapped to the nearest grey through a 256-byte table.
- **Color:** 16-bit RGBA4444 in the CPU's byte order; the display ignores
  alpha. fbcon uses `DRM_FORMAT_C4` with a 16-entry palette, two pixels per
  byte through a table of 256 words: a quarter of the memory of a 16-bit
  shadow, and cell-aligned for fbcon (an 8-pixel cell is one 32-bit word).
  `HOST_RGB565` (what X asks for at depth 16) is converted with shifts.
  The control register's unblank bit follows the CRTC.
- **Both:** XRGB8888, and BGRX8888 (what legacy `ADDFB` hands big-endian
  drivers with `quirk_addfb_prefer_host_byte_order`, without which `ADDFB2`
  fails on big-endian) are converted, with shifts: a division per pixel
  made a full screen take 10 s.
- A palette change commits only the CRTC, so the driver's atomic check
  adds the plane to such commits and the plane update redraws with the new
  palette.
- **fbcon fix** (`drivers/video/fbdev/core/fbcon.c`): below 16 colours,
  `fbcon_set_palette()` copies the 4-entry default palette but leaves its
  colour map 16 entries long, and DRM's `setcmap` refuses more entries
  than the gamma LUT has. So fbcon's palette never reached a monochrome
  NeXT: text was dark grey (level 1) instead of fbcon's light grey. It now
  sets the length to match the depth. Worth sending upstream on its own.
- **DRM fbdev fix** (`drivers/gpu/drm/clients/drm_fbdev_helper.c`): when a
  DRM master exits, the fbdev restore commits the mode but not the palette,
  so the console kept the program's palette until a VT switch. The restore
  now re-applies fbdev's colour map for pseudo-colour visuals, clamped to
  2^bpp entries (`info->cmap` has 256, and `setcmap` rejects more than the
  gamma LUT holds). Also for upstream; it depends on the fbcon fix, without
  which `info->cmap` still holds the default VGA colours on monochrome
  (all near black in grey).
- **Checked on screen** (Previous saves its screen on SIGHUP,
  `scripts/screenshot.sh`): fbcon on both; `tests/drmtest.c` in C2 and
  XRGB8888 on monochrome, C4, XRGB8888 and RGB565 on Color, with palettes
  through `DRM_IOCTL_MODE_SETGAMMA`; the console palette after drmtest
  exits; kernel messages on the screen with `console=tty0` (a full Debian
  boot, no journald timeouts any more).
- **Pitfall:** with the emulator's fast-forward, a guest that sleeps runs
  up to 8 times faster than the wall clock. drmtest's "show for 10
  seconds" was over before a screenshot; that, not the driver, was the
  "image doesn't appear" of the first tests. Sleep long and kill it.
- `next_defconfig`: `CONFIG_VT` back on, `DRM`, `DRM_NEXTDISPLAY`,
  `DRM_FBDEV_EMULATION`, `FB_DEVICE`, `FRAMEBUFFER_CONSOLE` and
  `FRAMEBUFFER_CONSOLE_LEGACY_ACCELERATION`. Without the last one fbcon
  redraws every character on each scroll; with it, it moves runs of
  characters with `copyarea`.
- **Console speed** (`tests/scrollbench.sh`, guest time, 25 MHz): 52 ms
  per scrolled line of full-width text and 31 ms for short lines, the same
  on monochrome (C2) and Color (C4). A profile (`profile=2`): fbcon's
  character drawing (`sys_imageblit`) 22%, its cell-by-cell comparison
  (`fbcon_redraw_blit`) 20%, the generic `copyarea` about 13%, the copy to
  video memory 6%. Tried and dropped: moving the whole text area at once
  in `SCROLL_MOVE` (4–7 times slower: the generic `copyarea` moves about
  2 MB/s), and C4 on monochrome (no faster than C2). What's left is
  generic fbcon and fbdev code; good enough for a console.
- **32 MB machines:** Debian's first boot on the NeXTstation Color (26 MB
  usable) thrashes for half an hour of guest time: until systemd enables
  swap, the page cache keeps rereading programs from disk. Not the
  display: the tests above ran with `init=/bin/bash`. Fix in Part 1 terms
  (swap earlier, fewer early services, or zram).

### 12. Keyboard and mouse (Part 2, step 8)

`drivers/input/keyboard/nextkbd.c` (`CONFIG_KEYBOARD_NEXT`). `config.c`
registers `next-kbd` with the sound and keyboard/mouse interface's
registers (`0x0200e000`, 16 bytes) and its interrupt (bit 3, level 3).

- **Interface** (from Previous's `kms.c`; NetBSD's `nextkbd.c` reads the
  same registers): the interface polls the keyboard, and the mouse plugged
  into it, by itself. Each event sets "received" in the keyboard/mouse
  status byte (`0x0200e001`) and interrupts. Reading the data register
  (`0x0200e008`) takes the event and clears the interrupt. An event that
  arrives before the last was read sets "overrun", cleared by writing that
  bit. The data's top byte is the device address (bit 24 set: mouse);
  answers to commands (bits 29, 30) are skipped. The ROM has already
  enabled the interface and set the poll mask; the driver relies on that.
- **Keyboard:** a 7-bit key code with an up bit, plus the state of all
  seven modifiers (Control, both Shifts, Commands and Alternates) in every
  event; a modifier change alone comes as an event with key 0. The driver
  reports each modifier that changed, then the key. Command maps to
  `KEY_LEFTMETA`/`KEY_RIGHTMETA`, Alternate to `KEY_LEFTALT`/`KEY_RIGHTALT`,
  Delete to `KEY_BACKSPACE`, the brightness and volume keys to their
  `KEY_` codes. The table is the driver's keymap (`EV_MSC`/`MSC_SCAN` and
  `EVIOCSKEYCODE` work). The keyboard doesn't repeat; the input core does
  (`EV_REP`).
- **Mouse:** a separate input device. 7-bit two's complement counts that
  run the other way (negative is right and down) and active-low buttons.
- **Overrun:** a lost event may be a key release, so the driver releases
  every key still down rather than leave one repeating. Untested: the host
  input below never overruns.
- **NumLock:** NeXT keyboards have no NumLock key, and with the console's
  default NumLock off the keypad digits are cursor keys. As parisc does for
  HIL keyboards, `arch/m68k/include/asm/kbdleds.h` makes NumLock start on
  when the NeXT driver is built and the machine is a NeXT (and
  `drivers/tty/vt/keyboard.c` includes it on m68k).
- **Testing:** Previous reads keyboard and mouse commands from the FIFO
  `out/input` (`PREVIOUS_INPUT`, `scripts/input.sh`), and sends them one
  event at a time, 10 ms apart and only once the guest has taken the last
  one. Checked on the NeXTcube, on screenshots and with an evdev dump over
  serial:
  - logging in on tty1;
  - every printable ASCII character, Return, Tab and Delete (typed into
    `cat`, compared with `od -c`);
  - both Shifts, Control (Ctrl-C, Ctrl-D), the arrows (shell history,
    editing) and key repeat;
  - the keypad, NumLock on from boot;
  - mouse movement in both directions, moves larger than 63 split into
    steps, and both buttons;
  - no errors in `dmesg`, `ERR: 0` in `/proc/interrupts`.
- **Limits:**
  - NeXT keyboards have no function keys, so Alt+F*n* can't switch
    virtual terminals; use `chvt`.
  - The default console keymap has nothing for keypad `=`. A keymap
    (`loadkeys`) or X can assign one.
  - The keyboard's LEDs aren't driven.
  - The power key never reaches Linux: Previous turns it into a
    power-down request.
  - Not tried on the Color (its interface is the same), on Turbo, or with
    the ADB keyboards of later Turbos.
- `next_defconfig`: `KEYBOARD_NEXT`, `INPUT_EVDEV`; `KEYBOARD_ATKBD`,
  `MOUSE_PS2` and `SERIO` off (no PS/2 on a NeXT).

### 13. Boot from disk (Part 2, step 9)

The loader doubles as the disk's boot block: `make bootblock` (and
`build-image.sh`) writes it alone as `out/bootblock`, an 8.6 KB a.out
image.

- **ROM:** booting from `sd`, the ROM reads the NeXT label, loads the a.out
  image from the boot block it names (`cd_boot_blkno`, in label sectors:
  32 and 96, i.e. 32 KB and 96 KB, where NeXTSTEP and NetBSD put theirs;
  each copy has 64 KB) and calls it with `sd()` plus the label's boot file
  name. With no boot file name (`cd_kernel`) it stops with "No bootfile in
  label", so the label names `linux`, which the loader ignores.
- **Loader** (`loader/disk.c`, `loader/scsi.c`): when the ROM's boot
  device is `sd`, it turns off the data cache (the instruction cache stays
  on), finds the disk (the *n*th SCSI target to answer, from `sd(n,lun,part)`,
  default the first), reads the label and the partition of type `boot`.
  That holds a header (`struct bootpart`: "NXBK", kernel and initrd offsets
  and sizes, a 256-byte command line), the kernel's ELF file as Debian
  installs it in `/boot`, and optionally an initrd. The loader reads the
  `PT_LOAD` segments into memory after itself, clears the gaps, finds
  `_stext` by the bootinfo version table, and continues as for a netboot.
  Anything after the boot file in the ROM's boot command is still added to
  the command line.
- **SCSI** (after NetBSD's `stand/boot/scsi.c`): the 53C90A, polled. Select
  with ATN, then data in, then status and message in. Sector data goes by
  DMA straight into the destination (16-byte aligned, 32 KB per READ(10)),
  with NetBSD's flush of the DMA interface's buffer at the end; short
  answers such as READ CAPACITY's 8 bytes come through the FIFO a byte at
  a time. Commands are retried on CHECK CONDITION or BUSY (unit attention
  after reset). Timeouts use the event counter. The chip is reset but not
  the bus: the ROM has just used it.
- **Disks:** `mkdisk.py --boot MB --bootblock FILE` makes new ones;
  `build-disk.sh debian` now gives 224 MB swap, a 32 MB boot partition
  (`sda3`) holding the image's kernel, and the boot blocks.
  `nextboot.py setup` converts an existing disk: it adds the boot
  partition from free space at the end, or else from the end of a swap
  partition (rewriting the swap header), and writes the boot blocks.
  `nextboot.py kernel` writes a kernel from the host.
- **Debian:** `debian-root/etc/kernel/postinst.d/zz-next-boot` (Perl, which
  Debian always has) runs when a kernel package is installed. It finds the
  boot partition from the NeXT label of `/dev/sd?` and writes the kernel,
  the initrd if there is one, and `/etc/kernel/cmdline` (else the running
  kernel's command line). It clears the header first and writes it last,
  after an fsync, so a partly written kernel never looks bootable. Without
  a boot partition (as when the image is built on the host) it does
  nothing.
- **Previous:** `run-previous.sh` takes `BOOT=disk` (boot device SCSI) and
  `NET=0` (Ethernet unplugged).
- **Tested:**
  - the existing disk converted with `nextboot.py setup`, then booted with
    `BOOT=disk NET=0` on the NeXTcube to `login:` (swap on, 224 MB);
  - `dpkg -i` of a new kernel package on the NeXT ran the hook, and the
    next reboot from disk ran that kernel (#46);
  - the loader and kernel on the NeXTstation Color (8 MB banks), up to
    mounting the root file system (Debian itself thrashes there, see
    step 11);
  - netbooting with the new loader;
  - `build-disk.sh debian`'s steps on a scratch image.
- **Not tested:** the 68030 (data cache code), several disks or a disk
  other than target 0, an initrd from disk, real hardware (the SCSI code
  follows NetBSD's boot blocks, which work on real NeXTs).

### 14. Turbo and 68030 (Part 2, step 10)

`run-previous.sh` now also takes `MACHINE=turbo`, `turbo-color`,
`cube-turbo` (33 MHz, 4 x 32 MB, the MCS1850 clock, the original NeXT
keyboard rather than ADB) and `cube030` (68030 + 68882, the 1.0 ROM, the
NCR 53C90, 64 MB).

**Turbo** (tested on the NeXTstation Turbo):

- **No disk, SCC or memory-to-memory DMA channels:** their CSRs bus error.
  The loader's and the kernel's "stop all DMA" now skip them on Turbo
  machines; the loader stopped with "Exception #2 ... faultaddr
  0x2000050" before that.
- **DMA controller:** a channel's buffer is reset with bit 0x00800000;
  0x00200000, the non-Turbo bit, means "set complete" there.
  `next_dma_initbuf()` picks the right one. There are no saved
  next/limit/start/stop registers, except the ethernet receive channel's
  saved limit at `0x02004050`.
- **SCSI:** with that, the ESP driver uses DMA on Turbo machines too (it
  was PIO only). 20 MB written, caches dropped, read back: same checksum.
- **Ethernet** (an AT&T 7213 in place of the MB8795, after NetBSD and what
  Previous emulates; `mb8795.c`):
  - the receiver has an enable bit (0x80) and its own match modes (0x02
    own address and broadcast, 0x01 everything; no multicast filter, so
    multicast takes everything);
  - a frame goes out when the transmit mode's start bit (0x80) is set,
    with no end-of-packet flag or "+15" in the DMA limit, and the mode
    goes back afterwards;
  - transmit mode bit 0x04 selects twisted pair; the driver tries it and
    falls back to thin wire if the control register reports no
    twisted-pair link (`BADTPE`);
  - the saved limit is read from `0x02004050`;
  - the receiver switches itself off when a frame arrives with the
    receive DMA channel stopped, so restarting the channel also rewrites
    the receive mode.
- **Ethernet receive race** (all machines): when the other buffer filled
  while the interrupt handler was still queueing the one just emptied,
  the channel stopped, and the handler's "clear complete" hid that frame's
  interrupt: receive stopped for good (on the Turbo after about 1.5 MB of
  a download). The handler now checks the channel after queueing; if it
  stopped, it takes the frame in the other buffer and restarts.
- **Tested:** Debian netbooted and booted from disk (`BOOT=disk NET=0`),
  128 MB (121 MB free for Linux), the display and keyboard (login on the
  screen), reboot, ping, a 20 MB download with the right checksum in
  76 s (about 270 KB/s, against 690 KB/s on the non-Turbo cube: about one
  frame in 1,700 is missed because both receive buffers filled before the
  interrupt was handled, and TCP retransmits).

**68030** (the original NeXT Computer):

- **ROM globals:** the 1.0 ROM's monitor globals are 804 bytes long and
  end at the top of memory, before the machine type and board revision
  (offsets 936, 937): reading them bus errors. The loader now detects the
  CPU (the 68040's CACR has an instruction cache enable bit in bit 15)
  and on a 68030 takes the machine to be the NeXT Computer.
- **Memory mapping:** the loader took the ROM's 8 KB off the top of the
  last memory chunk. On a 68030, `map_node()` (`arch/m68k/mm/motorola.c`)
  maps memory in 256 KB steps and counts the size down unsigned, so a
  size that isn't a multiple of 256 KB wraps around and goes on mapping
  virtual = physical - 0x04000000 over everything above, including the
  device space at `0xf0000000`. The first console write after
  `paging_init()` then bus errored at `0xf4018001`. The loader now passes
  whole banks and `config_next()` reserves the ROM's 8 KB with
  `memblock_reserve()`. (`map_node()` could round down itself; other
  platforms pass whole megabytes.)
- **Tested** with `init=/bin/bash` on the Debian disk: Debian's programs
  run (68882 floating point included), 20 MB written, caches dropped and
  read back with the same checksum, ping, a 20 MB download with the right
  checksum in 159 s (about 125 KB/s; 325 frames missed, see above), the
  reboot path (kernel restart, ROM, netboot again) and booting from disk
  with the cable out.
- **Full Debian boot: not practical.** The emulated 68030 is about a third
  as fast as the 68040 (3.55 against 9.68 BogoMIPS), and systemd never
  settles: services hit their start timeouts and are restarted, systemd
  and udev used 19 and 16 minutes of CPU, and there was still no serial
  login after 88 minutes of guest time (the screen's getty did come up
  once). A lighter init (sysvinit) or longer timeouts would be needed.
- **Open:** in the first full boot, `systemctl` stayed in D state waiting
  for a free disk request tag, with no SCSI timeout or error logged. A
  second boot, sampled once a minute for 88 minutes of guest time, never
  had a request in flight for long. Not reproduced since.
- Not tried: `turbo-color`, `cube-turbo`.

### 15. Polish in the emulator (Part 2, step 10.5)

- **The run script's machine settings.** Previous reads `nSCSI` and `nRTC`
  as booleans (`TRUE`/`FALSE`) and ignores anything else. Step 10's
  `run-previous.sh` wrote `0` and `1`, so every run since had Previous's
  defaults, which are the 68030's: the NCR53C90 on every machine and the
  MC68HC68T1 clock on the Turbos. Fixed:
  - the 68040 machines have the NCR53C90A again;
  - the Turbos have the MCS1850, whose kernel code (a seconds counter) had
    never run. Setting the clock, reading it, letting it run and rolling
    over into 2100 all work, tested with the RTC ioctls.
- **Turbo Color and Turbo cube.** Both boot Debian to a login, netbooted
  and from disk with the cable out:
  - the Turbo Color's 12-bit display is checked with `drmtest`;
  - downloads are correct;
  - `/proc/hardware` now names the Turbo's AT&T 7213 and the 68030's
    NCR53C90.
- **Ethernet.**
  - When both receive buffers fill before the interrupt is serviced, the
    DMA channel keeps only the second frame's end, and the earlier frame
    used to be dropped. The driver now takes it, with its length from its
    IPv4, IPv6 or ARP header; only other kinds still count as missed.
  - The frames still lost after that were dropped by Previous while the
    channel was stopped. The receive interrupt had been held off for over
    a millisecond by the page allocator, which moves free pages between
    pageblocks with interrupts off. With 256 KB pageblocks
    (`PAGE_BLOCK_MAX_ORDER=6`, down from 4 MB), drops fell from about 20 per
    20 MB to between 0 and 2.
  - Previous delivered a received frame every 500 µs, 2.4 times as fast
    as a real 10 Mbit/s wire. It now spaces them at wire speed
    (`previous-linux.patch`, `ethernet.c`).
  - The slow Turbo downloads of step 10 (270 KB/s) were a measurement
    artifact. Under fast-forward, an idle guest's clock races ahead while
    Previous's network runs on host time. In real time (`FAST=0`) a 20 MB
    download takes 59 s on the cube (340 KB/s), limited by the CPU (most of
    it `sed`).
- **Disk read-ahead.** A rotational disk that reports no optimal transfer
  size gets twice its largest transfer as read-ahead, which here was 8 MB,
  since the ESP allowed 0xffff sectors.
  - On 32 MB that pushed programs out of memory at every page fault:
    276,000 file pages were read back in while booting, and the
    NeXTstation took from 17 minutes to hours to boot.
  - `next_esp` now limits commands to 64 KB, which is more than its DMA
    moves at a time anyway, so read-ahead is the usual 128 KB.
  - The 32 MB NeXTstation now boots in 7 minutes like the cube, with 9,383
    refaults. A memory test of 48 MB, with 104 MB going through swap,
    passes.
- **Kernel configuration.** `MEMCG` is off, which makes the boot 6% faster
  (7:30 to 7:03 in real time). `PAGE_BLOCK_MAX_ORDER=6`.
- **The Debian image** (`debian-root/`):
  - systemd's start and device timeouts are 30 minutes, since udev takes
    about 16 minutes on the 68030;
  - `next-early-swap.service` turns swap on before udev;
  - udev runs at most 2 workers. It otherwise runs at least 10 at once,
    however little memory there is.
- **68030:** Debian boots fully under systemd, from disk with the cable out,
  to a serial login with no failed units. It takes 21 minutes of emulated
  time, about 3 minutes in wall time at `FAST=8`. In step 10, systemd's
  90-second timeouts killed the slow services, and the 8 MB read-ahead
  slowed everything. `login`'s own 300-second limit can still run out
  while the boot is busy, so log in once it has settled.
- **Kernel package:** `out/disk.img` has kernel #74, installed with
  `dpkg -i`, whose hook wrote it to the boot partition.
- **Not reproduced:** the D-state wait from step 10.

### 16. NeXTdimension (Part 2, step 10.6)

NeXT's sources for the board (https://github.com/johnsonjh/NeXTDimension,
cloned to `../NeXTDimension`) gave the RAMDAC setup and the ROM's boot
handshake. Previous emulates the board. `ND=1` in `run-previous.sh` puts
one in slot 2 of a cube and shows its screen in the main window and in
screenshots.

- **Detection** (`arch/m68k/next/config.c`). On cubes, the kernel reads
  the NBIC ID at the end of each slot's slot space, catching the bus error
  of an empty slot with `hwreg_present()`. For ID `0xc0000001` it registers
  `next-dimension` with the board's video memory, memory controller,
  RAMDAC and the start of its DRAM.
- **DRM driver** (`drivers/gpu/drm/tiny/nextdimension.c`).
  - 1120×832 in 32 bits, from 4 MB of video memory 1152 pixels wide. A
    pixel is a big-endian word of red, green, blue and an unused byte:
    `XBGR8888` in DRM's terms. XRGB8888 in either byte order is
    converted.
  - The Bt463 RAMDAC is set up as NeXT's driver did, with a linear
    palette.
  - The memory controller unblanks the screen. Its registers are
    big-endian, while `writel()` is little-endian on m68k.
- **Acceleration.** The console is drawn by the board's i860.
  - The program is `nd-firmware/ndfw.s`: 192 words of i860 assembly, since
    GCC dropped the i860 in 2005. It is assembled by binutils 2.30, the
    last release with the i860 (`scripts/build-i860-binutils.sh`), into
    `nextdimension_fw.h` (`nd-firmware/build.sh`).
  - At probe the driver resets the i860 into its ROM and loads the
    program. It stores each instruction word at its address XOR 4, because
    the i860 fetches instructions little-endian. It then interrupts the
    i860, upon which the ROM jumps to the start of DRAM.
  - The program takes fill, copy and glyph commands from a 64 KB ring in
    DRAM. Copies and fills use 128-bit loads and stores when aligned,
    which full-width scrolling always is.
  - The fbdev emulation then draws straight into the video memory through
    the i860. XOR fills, colour images and `read`/`write` wait for the i860
    and use the CPU.
  - When a DRM client gives the screen back, the console is redrawn
    (`fb_restore`, then `fb_set_var_from_user()`). If the i860 takes no
    command for 10 seconds, it's stopped and the CPU draws.
- **Previous** (`previous-linux.patch`):
  - Previous runs the i860 in a thread of its own. On the Apple Silicon
    host, it could see the ring's head index before the commands the
    index announced, and then misread the ring. The board's DRAM now has
    ordered word accesses (acquire loads and release stores in
    `nd_mem.cpp`).
  - The screenshot is taken from whichever slot the window shows.
- **Results** (2000 lines on the console at `FAST=8`):
  - 84 s without acceleration, 54 s with it;
  - text, colours, `drmtest` and the return to the console all check out
    on screenshots;
  - booting from disk with the cable out works;
  - a cube without the board is unaffected.
- **Limits:**
  - while the ring is full, the CPU waits (the console can't sleep);
  - scrolling is bound by how fast Previous emulates the i860: about 27 ms
    for a full-screen copy;
  - DRM clients other than the console still copy with the CPU;
  - one board (slot 2) has been tested, and the ROM handshake has never
    run on real hardware.

### Tooling

- **Build:** a cross-build container (with m68k glibc headers), plus
  scripts for:
  - the kernel and its Debian package;
  - the busybox initramfs and disk;
  - the Debian image (`build-debian.sh`, with `debian-root/` and
    `debian-packages`);
  - the disk (`build-disk.sh`; `mkdisk.py` writes NeXT labels and swap,
    `update-disk.sh` updates an existing disk);
  - the netboot image and boot block (`build-image.sh`), and disks that
    boot without the network (`nextboot.py`).
- **Emulator:** Previous configuration and run script, both serial ports on
  PTYs (`out/ttyA`, `out/ttyB`); `BOOT=disk`, `NET=0`, `MACHINE=`.
- **Console:** `console.py` (interactive), `serial-run.py` (scripted,
  `--port b`, `--login`), `sysrq.sh` (magic SysRq), `screenshot.sh` (the
  emulated screen as a PNG), `input.sh` (types on the NeXT keyboard, moves
  the mouse).
- **Tests:**
  - `regcheck.c`: register preservation across syscalls (including
    `rt_sigprocmask` as glibc uses it) and FPSP;
  - `nfsmount.c`: NFS mounts with text options;
  - `cascheck.c`: `cas.l` and copy-on-write around faults and `fork()`;
  - `ptdump.c`: ptrace dump of a stuck process;
  - `forkabrt.c`, `rwlock.c`: glibc `fork()`/`sigaction`/rwlock checks for
    the Debian root;
  - `drmtest.c`: a DRM test image in each format;
  - `scrollbench.sh`: framebuffer console scrolling speed.

### Previous patches (`previous/previous-linux.patch`)

- The SCC is connected to the host: `PREVIOUS_SCC_A_PTY` gives a pseudo
  terminal and `PREVIOUS_SCC_A_LOG` a transcript.
- ESP non-DMA data-out transfers were unimplemented and aborted the
  emulator; implemented.
- ESP non-DMA data-in did not set the bus-service interrupt status; fixed.
- SIGUSR1 sends a break on SCC channel A, for magic SysRq
  (`scripts/sysrq.sh`); ext/status interrupts and their reset are emulated
  for it.
- 68040 MMU: a fault on the read cycle of a locked RMW (`cas`, `tas`) no
  longer reports a pending writeback (see Done, step 7).
- SIGUSR2 raises an NMI (`scripts/nmi.sh`).
- SIGHUP saves the screen as a PNG to `PREVIOUS_SCREENSHOT`
  (`scripts/screenshot.sh`), which works while the Mac's display is locked.
- `PREVIOUS_INPUT` names a FIFO of keyboard and mouse commands (`type`,
  `key`, `down`/`up`, `move`, `button`; `scripts/input.sh`), sent one event
  at a time at typing speed, for when there's no window to type into.
- Speed (see Done, step 10): `PREVIOUS_FAST_FORWARD=N`, `_setjmp`/`_longjmp`
  in the CPU core's exception macros, skipping idle `stop` time to the
  next event.
- Not patched: the built-in NFS server drops pipelined requests over TCP.
  Use UDP until that's fixed and reported.

## Part 1: boot Debian (serial console)

**1. 64 MB of RAM. (done)** Previous only populates two banks on a non-Turbo
NeXTstation (32 MB). Switch the emulated machine to a NeXTcube with a 68040
(four 16 MB banks, same DMA, Ethernet and SCSI hardware).
*Done when:* the loader reports 64 MB and the disk and network tests pass on
the Cube.

**2. Kernel configuration for Debian. (done)**
- Add what systemd needs:
  - control groups: `CGROUPS` (and `MEMCG` if possible);
  - event and timer interfaces: `INOTIFY_USER`, `SIGNALFD`, `TIMERFD`,
    `EPOLL`;
  - file handles and automount: `FHANDLE`, `AUTOFS_FS`;
  - tmpfs extras: `TMPFS_POSIX_ACL`, `TMPFS_XATTR`;
  - IPC: `POSIX_MQUEUE`.
- Also add `SWAP` and keep IPv6.
- Build everything in (no modules, no initramfs), and produce a kernel
  `.deb` with `make bindeb-pkg`.

*Done when:* the busybox disk still boots and nothing is built as a module.

**3. Debian root file system, built on the host. (done)**
- Use `mmdebstrap` from Debian Ports `unstable` with
  `debian-ports-archive-keyring`, in a privileged container.
- Run the m68k half under `qemu-user-static`.
- Pre-configure the image:
  - `fstab`, hostname, root password, apt sources;
  - DHCP on `eth0`;
  - a serial login on `ttyPZ0`;
  - our kernel package.
- Fallback: if registering QEMU with `binfmt_misc` fails under OrbStack, run
  the second debootstrap stage inside Previous.

*Done when:* `scripts/build-debian.sh` reproducibly builds the ext4 image.

**4. Bigger disk with swap. (done)** Extend `build-disk.sh`/`mkdisk.py`: about
2 GB, partition a for root and b for 128–256 MB of swap.
*Done when:* the kernel shows `sda1` and `sda2`, and `swapon` works.

**5. First Debian boot. (done)**
- Netboot with
  `root=/dev/sda1 rootwait rw console=ttyPZ0,9600 ip=dhcp`.
- Expect slow boots. Trim services, or use `sysvinit-core` if systemd is
  too heavy.
- Chase any Debian-only kernel bugs (glibc, systemd syscalls, FPU-heavy
  code) with focused tests like `regcheck`.

*Done when:* `login:` appears on the serial console, and
`apt update && apt install` works through Previous's network.

**6. Hardening.**
- ~~SCSI: flush partial DMA bursts~~ (deferred: only small control
  commands are affected).
- ~~Ethernet: receive overrun, transmit timeout, carrier~~ (done; there
  is no carrier to report).
- ~~Speed: PID 1's CPU use~~ (not a problem at idle).
- ~~Interrupts: NMI; "unhandled level 3/5 interrupt" messages~~ (done).
- Soak test (`tests/soak.sh`).

*Done when:* an overnight soak (apt upgrades plus disk and network load)
leaves no errors in `dmesg`.

## Part 2: a complete NeXT system

**7. Framebuffer.** Done (Done, step 11): a DRM driver for the 1120×832
display, 2-bit greyscale on monochrome machines and RGBA4444 on Color, with
fbdev emulation and the framebuffer console.
*Done when:* the kernel console shows on the NeXT screen.

**8. Keyboard and mouse.** Done (Done, step 12): an input driver for the
NeXT keyboard/mouse bus (see NetBSD `nextkbd.c` and Previous `kms.c`), with
the NeXT keymap.
*Done when:* you can log in locally without the serial port.

**9. Boot from disk without the network.** Done (Done, step 13): the loader
in the NeXT label's boot blocks, with a polled SCSI reader (after NetBSD
`stand/boot`) that loads the kernel from a raw boot partition, and a
Debian kernel hook that writes new kernels there.
*Done when:* Previous boots from SCSI with Ethernet disconnected.

**10. Turbo and 68030.** Done (Done, step 14): the Turbo DMA variant and
Ethernet, with 128 MB; the 68030 NeXT Computer boots, runs Debian's
programs and reboots (a full systemd boot came in step 10.5).

**10.5. Polish in the emulator.** Done (Done, step 15): the Turbo
Color and Turbo cube; settings, Ethernet, read-ahead and boot fixes that
make the 32 MB NeXTstation and the 68030 boot Debian.

**10.6. NeXTdimension.** Done (Done, step 16): a DRM driver for the
board's 32-bit display, with the console drawn by its i860.

**11. Real hardware.** Netboot from a host via BOOTP/TFTP with a serial
cable, then rerun the step 5 and 6 tests. This validates timing assumptions
and the reboot path.

**12. Upstream.**
- **Linux:** split into a reviewable series (platform, interrupts and timer,
  `pmac_zilog`, SCSI, partitions, Ethernet), add a MAINTAINERS entry, and
  post to linux-m68k. The human submitter adds `Signed-off-by` and notes the
  AI assistance (`Documentation/process/coding-assistants.rst`).
- **Previous:** send the SCC, ESP and 68040 locked-RMW patches upstream
  and report the NFS TCP bug. The locked-RMW fault frame code comes from
  WinUAE, so check whether WinUAE has the same bug.
- **Debian:** once it's upstream, `CONFIG_NEXT` in Debian's m68k kernel.

## Risks

- **Memory:** systemd in 64 MB. Mitigated with swap, or sysvinit.
- **Speed:** the emulator is slow for large installs, hence building the
  root file system and installing packages on the host. The emulated
  68040 reports BogoMips 9.68 ("Clocking: 14.5MHz"), about 60% of a real
  25 MHz 68040, so Previous may be slower than real hardware and these
  timings pessimistic. Check on real hardware (step 11). Fast-forward
  (Done, step 10) makes day-to-day work about 5 times faster.
- **Package churn:** sid on Debian Ports is sometimes uninstallable.
