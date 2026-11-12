# NeXT hardware notes

What this project has learned about the hardware, from NetBSD's next68k
port, Previous's source, chip data sheets and experiment. It's
a map, not a register reference: the definitions are in the kernel's
`arch/m68k/include/asm/nexthw.h` and the drivers, and `ROADMAP.md`'s Done
sections say how each fact was found. Everything here has been tested in
Previous only; nothing yet on real hardware.

## The machines

| Machine | CPU | Memory (as emulated) | Display | Notes |
|---|---|---|---|---|
| NeXT Computer | 68030, 68882, 25 MHz | 4 × 16 MB | 2-bit grey | ROM 1.0, NCR53C90, MC68HC68T1 clock |
| NeXTcube | 68040, 25 MHz | 4 × 16 MB | 2-bit grey | NeXTBus slots |
| NeXTstation | 68040, 25 MHz | 2 × 16 MB | 2-bit grey | |
| NeXTstation Color | 68040, 25 MHz | 4 × 8 MB | 12-bit colour | |
| Turbo (station, Color, cube) | 68040, 33 MHz | 4 × 32 MB | as above | AT&T 7213 Ethernet, MCS1850 clock, fewer DMA channels |

The loader identifies the machine from the ROM monitor's globals (model and
board revision), except on the 68030, whose ROM 1.0 has a shorter globals
structure without them: there the CPU type decides (the 68040's CACR has
an instruction-cache enable bit, the 68030's doesn't).

## Memory map

- **ROM** at 0, which is also where the reset vectors are.
- **Main memory** from `0x04000000`, in banks. The ROM keeps its data in the
  top 8 KB, which the kernel reserves. The 68030 kernel maps memory in
  256 KB steps, so memory sizes passed to it must be multiples of that.
- **Devices** in 16 MB at `0x02000000`, mapped by `head.S` at virtual
  `0xf0000000`. On the 68040 machines most devices also appear in the
  "BMAP" alias 1 MB higher, which the drivers use.
- **Video memory**: monochrome `0x0b000000`, Color `0x2c000000`, Turbo
  `0x0c000000`.
- **NeXTBus slots** on cubes: slot *n*'s slot space at `0xFn000000` and its
  board space at `0xn0000000` (see the NeXTdimension below).

## Interrupts

32 sources in one status register and one mask (`0x02007000`,
`0x02007800`), on CPU levels 3, 5, 6 and 7, all autovectored: the kernel
demultiplexes each level (`arch/m68k/next/ints.c`). Device interrupts are on
level 3, the SCC on 5, the DMA channels and the timer on 6, power-fail and
the NMI on 7. The NMI is edge-triggered and has to be cleared at the
keyboard/mouse controller. Long stretches with interrupts off, such as the
page allocator moving pages between pageblocks, delay the level-6 DMA
interrupts (see Ethernet).

## DMA

Each channel has a CSR and next/limit/start/stop registers. A transfer runs
from next to limit, then continues from start to stop if that was queued.
The channel records where a transfer ended in its saved-limit register.
Transfers are in 16-byte bursts.

- The ROM leaves channels running; the kernel and the loader stop them first.
- **Turbo:** only 7 of the channels exist. The disk, SCC, and the two
  memory-to-memory CSRs bus error. The buffer is reset by `0x00800000`, not
  the bit the others use; `0x00200000` means set-complete there. The
  Ethernet receive saved limit is at `0x02004050`.

## SCSI

NCR 53C90 (68030) or 53C90A (68040), driven by the kernel's ESP core
(`drivers/scsi/next_esp.c`). Whole 16-byte bursts go through the DMA
channel and a bounce buffer; the rest is PIO. A DMA moves less than
64 KB, and commands are limited to that. The disks report no optimal
transfer size, and larger commands would make the block layer read ahead
8 MB.

## Ethernet

Fujitsu MB8795; an AT&T 7213 with the same registers on the Turbo
(`drivers/net/ethernet/next/mb8795.c`).

- No DMA of its own: the DMA controller's transmit and receive channels
  move frames.
- Receive keeps two buffers queued. If both fill before the interrupt is
  serviced, only the second frame's end is recorded; the driver takes the
  first with the length from its IP or ARP header.
- **Non-Turbo transmit:** the limit marks the frame's end with a flag, and
  points 15 bytes past it.
- **Turbo:**
  - the receive mode has an enable bit (`0x80`), `0x02` for own address and
    broadcast, `0x01` for everything;
  - the receiver switches itself off when it overflows;
  - a frame is sent by the transmit mode's start bit (`0x80`); `0x04`
    selects twisted pair;
  - register 6 bit `0x40` says the twisted pair has no link.

## Real-time clock and power

An MC68HC68T1 (BCD calendar) or MCS1850 (32-bit seconds counter),
reached serially through bits of the system control register 2. Status bit
`0x80` tells them apart. The clock chip also switches the power off.
Reboot jumps through the ROM's reset vector (`arch/m68k/next/rtc.c`,
`config.c`).

## Serial

A Z85C30 SCC, as on the Macs, with each channel's control and data
registers at their own addresses (`pmac_zilog`).

## Displays

All 1120×832.

- **Monochrome:** 2 bits per pixel, leftmost pixel in the high bits; 0 is
  white and 3 black.
- **Color:** RGBA4444 in big-endian words; alpha is ignored.
- **Line length:** 1152 pixels on non-Turbo machines; Turbo lines aren't
  padded.
- **Color CSR:** non-Turbo Color machines have a video control register
  that blanks the screen.

## Keyboard and mouse

Behind the sound/keyboard/mouse interface at `0x0200e000`
(`drivers/input/keyboard/nextkbd.c`).

- **Registers:** a status byte at `e001` (`0x40` an event is waiting,
  `0x20` overrun, written to clear) and the event at `e008`, taken by
  reading it.
- **Event format:** bit 24 marks the mouse, bits 29 and 30 command answers.
  Keyboard events carry all modifier states (bits 8-14) and a key code with
  bit 7 for release. Mouse events carry 7-bit counts that run the other way
  and active-low buttons.

## Booting

- The ROM monitor netboots (BOOTP/TFTP) or loads boot blocks named in a
  disk's label. A dlV3 label names the boot block's location and a boot
  file, which must be set (we use "linux").
- The boot program is an a.out image loaded at `0x04380000`, which our
  loader is.
- Disk labels come in three versions ("NeXT", "dlV2", "dlV3"), with eight
  partitions whose offsets count label sectors from the end of the front
  porch (`block/partitions/next.c`).

## NeXTdimension

A cube's NeXTBus board with an i860 XR at 33 MHz, its own DRAM (up to
64 MB) and a 32-bit display (`drivers/gpu/drm/tiny/nextdimension.c`,
`nd-firmware/`).

- **Finding it:** the NBIC ID is in the last bytes of slot space, one byte
  per word at `0xFnFFFFF0`-`FC`; it's `0xC0000001` for this board. An empty
  slot bus errors.
- **Board space** (`0xn0000000` plus): DRAM at `+0x08000000`, video memory
  at `+0x0e000000`. The video memory is 4 MB, 1152 pixels a line, each
  pixel a big-endian word `0xRRGGBBAA`.
- **DRAM size:** four banks, each in its own 16 MB of board space, holding
  4 or 16 MB or nothing. In Previous a 4 MB bank repeats through its 16 MB
  and an empty one reads 0 and ignores writes, so `config.c` sizes each bank
  by writing a mark every 4 MB and counting those that survive (and puts
  back what was there). How a real board answers for an empty bank is
  unknown: a read that bus errors counts as empty.
- **Slot space** (`0xFn000000` plus):
  - the memory controller at `+0x800000`. CSR0 bit 0 restarts the i860,
    bit 3 interrupts it, and bit 2 is the i860's interrupt enable. CSR1
    bit 0 interrupts the host. Writing 1 at `+0x2000` unblanks the display.
  - the Bt463 RAMDAC at `+0x200000`: address low and high at 0 and 4, a
    control register at 8, the palette at `0xC`.
- **The i860's own view:** DRAM at `0xF8000000` and video memory at
  `0xFE000000`.
- **Byte order:** the i860 runs big-endian but fetches instructions
  little-endian, a doubleword at a time. The host stores each instruction
  word at its address XOR 4.
- **Boot** (by experiment in Previous, which runs the board's ROM image):
  after a restart, the ROM sets the board up and waits for the host's
  interrupt, then jumps to the start of DRAM with paging off. Previous
  doesn't emulate the i860's caches, so whether the ROM leaves them on is
  unknown; check before trusting the shared ring on a real board.
- **Bt463 RAMDAC** (Brooktree's data sheet, in the 1993 Graphics and
  Imaging Product Databook on bitsavers and archive.org; Linux's `tgafb`
  drives the same chip): command register 0 = `0x40` is 4:1 multiplexing,
  register 1 = `0` gives 16 window types and no cursor, register 2 =
  `0x80` puts sync on green with a 0 IRE pedestal. A window type of
  `0x000100` is 8 planes a channel of true color through the palette.
  Previous ignores the command registers and the window type table, so
  only the palette is tested.

## Previous's emulation, where it matters

- **Wrong, not just different:**
  - received Ethernet frames came every 500 µs, 2.4 times wire speed; the
    patch paces them at 10 Mbit/s;
  - the i860 thread saw shared memory out of order on Apple Silicon; the
    patch orders the DRAM accesses.
- **Different from real hardware:** the emulated 68040 runs at about 60%
  of a real 25 MHz one. That makes the emulator stricter than real hardware
  on interrupt latency.
- **Settings:** bool settings take only `TRUE`/`FALSE`. A machine type's
  defaults (SCSI chip, clock) come from Previous's configuration code, not
  from the type.
