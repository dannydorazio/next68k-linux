# Debugging

What has worked for finding out what the kernel, the NeXT or the emulator
is doing. Each Done section of `ROADMAP.md` tells how a particular problem
was found; this is the toolbox.

## Talking to the machine

- **Serial console.** `out/serial-a.log` has everything sent on the first
  serial port (`ttyPZ0`, the kernel console) since Previous started;
  `out/ttyA` is the pseudo terminal. `scripts/serial-run.py --login CMD...`
  runs commands and prints the transcript. Without `--login` it types into
  whatever is there, including a login prompt. `scripts/console.py` is
  interactive.
- **Screen and keyboard.** `scripts/screenshot.sh FILE` saves the screen
  that Previous's window shows: the built-in display, or the NeXTdimension
  with `ND=1`. `scripts/input.sh 'type root\n'` types on the NeXT's
  keyboard. If the serial console has no getty, log in on the screen and
  run `setsid -f agetty -L ttyPZ0 9600 vt100`.
- **Files.** The Debian image has no wget or curl. Put files in `out/www`;
  `scripts/time-download.sh` serves that directory on port 8765, which the
  NeXT reaches as 10.0.2.2:
  `exec 3<>/dev/tcp/10.0.2.2/8765; printf 'GET /f HTTP/1.0\r\n\r\n' >&3; sed '1,/^\r$/d' <&3 > f`.
- **A hung kernel.** `scripts/sysrq.sh KEY` sends a magic SysRq (the
  command line needs `sysrq_always_enabled`): `l` backtraces, `w` blocked
  tasks, `t` all tasks, `m` memory, `p` registers; `s`, `u`, `o` to sync,
  remount read-only and power off. `scripts/nmi.sh` raises an NMI, whose
  handler prints the interrupted registers even with interrupts off. If
  neither answers, Previous itself may be stuck.

## Instrumenting the emulator

The emulator sees what the guest can't: which instruction runs when a
device does something. Add a temporary `fprintf(stderr, ...)` to the
device's code in `../trunk/src`, rebuild (`make -C ../trunk/build`), and
read `out/previous-stdout.log`. `m68k_getpc()` gives the guest's PC and
`M68000_GetSR()` its status register; the interrupt mask is bits 8-10 (a
`27xx` SR means interrupts off). `scripts/pc2sym.py` turns the logged PCs
into function+offset from the current kernel's `System.map`, counted:

```sh
grep TEMPLOG out/previous-stdout.log | python3 scripts/pc2sym.py
```

That is how the 68030's bus errors were traced to a mapping wrap (step
10), and how the Ethernet frame drops were traced to the page allocator
holding interrupts off (step 10.5). Mark such code `TEMP`, and before
regenerating the patch check with `svn diff` that none is left.

## Instrumenting the kernel

- A temporary `pr_info_ratelimited()` is quick, but printk goes out on the
  9600-baud console with interrupts off, about 1 ms per character, which
  changes interrupt timing a lot. When timing matters, count instead: an
  unused statistic (e.g. `dev->stats.rx_compressed`) or a counter under
  `/sys` is visible without disturbing anything.
- A self-test at probe time, with fixed inputs and the results printed,
  shows whether a mechanism works at all before chasing it under load. The
  NeXTdimension's ring was checked this way, command by command.
- Grep for `TEMP` before committing.

## Where the time goes

- **Measure in real time** (`FAST=0`). Under fast-forward the guest's clock
  runs ahead while it's idle, so anything that waits on the host (network,
  the i860 thread) looks slow and timeouts fire early.
- `systemd-analyze` and `systemd-analyze blame` for boots.
- `/proc/vmstat`: `pgmajfault` and `workingset_refault_file` showed the
  32 MB machine re-reading programs from disk (step 10.5), which
  `/sys/block/sda/queue/read_ahead_kb` explained.
- `ip -s -s link show eth0` for frames missed and dropped; the TCP
  counters (`nstat -az`) for loss further up.

## The NeXTdimension

- The driver says when the i860 stops taking commands, with the ring's
  head, tail and the command at the tail.
- Previous runs the i860 in a thread (`bI860Thread` in
  `previous/previous.cfg.in`). Setting it to `FALSE` runs it in the main
  thread instead, which is much slower but rules out thread ordering. That
  was the cue for the ordered accesses in the patch.
- Previous has an i860 debugger (`nd_start_debugger()`), which this project
  hasn't needed yet.
