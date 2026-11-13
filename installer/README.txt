Debian installer for the NeXT
=============================

This installs Debian GNU/Linux (m68k, Debian Ports' unstable) on a 68030
or 68040 NeXT: the NeXT Computer, NeXTcube, NeXTstation, NeXTstation
Color and the Turbos. It's the regular debian-installer, with a Linux
kernel that supports the NeXT and two NeXT steps of its own: one gives
the disk a NeXT disk label, the other installs the kernel where the NeXT
ROM can boot it. The kernel and tools come from
https://github.com/dannydorazio/next68k-linux, which says what works.
None of it is official Debian. So far it has been tested only in the
Previous emulator, not on real hardware.

What you need
-------------

- A NeXT with at least 64 MB of memory. Smaller machines may work, but
  slowly. In the Previous emulator you need a build with next68k-linux's
  patches (previous/ in that repository).
- A SCSI disk of 2 GB or more, to be erased. A SCSI emulator such as
  BlueSCSI or SCSI2SD works too.
- Ethernet with DHCP and a route to the Internet: the installer downloads
  Debian from http://deb.debian.org/debian-ports. With Previous, the
  built-in network does this.
- Time. A NeXT is slow: allow several hours for an install on a 25 MHz
  68040.

Files
-----

  boot              the installer as one netboot image: loader, kernel
                    and installer
  installer.img     a disk image that boots the installer
  initrd.gz, vmlinuz, bootblock, linux-image-*.deb
                    the parts, if you'd rather put them together yourself
  MANIFEST          what this was built from, and every installer
                    component's version
  SHA256SUMS

Starting the installer
----------------------

Either netboot it: serve "boot" from a BOOTP/TFTP server, as for
NeXTSTEP. Previous's built-in server takes it from
<NFS path>/private/tftpboot/boot. At the ROM monitor's prompt type

    b en()boot

or set the boot device to Ethernet.

Or boot it from a disk: write installer.img to a disk or SD card (with
dd, for example), or attach it in Previous, as another SCSI disk next to
the one to install on. At the ROM monitor type

    b sd(1,0,0)

The first number counts the disks that answer, starting from 0, so it is
1 when the installer's disk has the higher SCSI ID. With the installer's
disk at the lower ID, plain "b sd" boots it.

The installer runs both on the NeXT's screen and on the serial port
(modem port A, 9600 baud): use either. The kernel's messages go to both.

The install
-----------

The steps are debian-installer's, with these differences:

- "Partition disks" offers whole disks only. It gives the chosen disk a
  NeXT disk label with three partitions: the root file system (ext4) on
  the rest of the disk, swap (256 MB unless you change it) and a 32 MB
  boot partition for the kernel. It also writes the boot blocks, which
  hold the loader the ROM starts.
- No kernel comes from the Debian archive, whose kernels don't support
  the NeXT. "Install the kernel and make the disk bootable" installs the
  NeXT kernel package and writes the kernel into the boot partition.
- The mirror is set to deb.debian.org/debian-ports, unstable.

When the installer finishes, take its disk away, if you used one, and
boot from the target disk: "bsd" at the ROM monitor, or set the boot
device to SCSI disk. The kernel names disks in SCSI ID order. The
installer gives the root partition the name it will have once the
installer's disk is gone, so a target disk with a higher SCSI ID than the
installer's disk won't boot while the installer's disk is still attached.

On the new system
-----------------

- Installing a newer NeXT kernel package (dpkg -i linux-image-...deb)
  writes it into the boot partition by itself. The kernel command line
  is in /etc/kernel/cmdline.
- Logins and services are slow to start at 25 MHz. Give it time.
- For X with WindowMaker:
      apt install xserver-xorg-core xserver-xorg-input-libinput xdm \
          wmaker xterm xfonts-base
  The NeXT's X settings are already in /etc/X11/xorg.conf.d/20-next.conf.
