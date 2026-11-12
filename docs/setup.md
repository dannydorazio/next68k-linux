# Setting up from scratch

How to get from an empty machine to Debian booting on an emulated NeXT.
Developed on macOS (Apple Silicon) with Homebrew and Docker; the scripts
assume the checkouts below sit side by side, e.g. in `~/git`.

## What you need

- **Docker** (Docker Desktop): every cross build runs in the `next68k-build`
  container. `build-debian.sh` and `update-disk.sh` use `--privileged` (loop
  mounts, binfmt).
- **Homebrew**: `brew install cmake sdl3 libpng subversion gnupg` for
  Previous and the checks below.
- Python 3, `curl`, `lsof` (macOS has them).

## The checkouts

```sh
cd ~/git
git clone git@github.com:dannydorazio/next68k-linux.git
git clone -b next68k-linux git@github.com:dannydorazio/linux.git
svn checkout -r 1864 https://svn.code.sf.net/p/previous/code/trunk trunk
# references, optional:
git clone https://github.com/johnsonjh/NeXTDimension.git
```

NeXT's NeXTdimension sources have no license and are marked confidential;
read them for orientation only (see `ROADMAP.md`, Done, step 17).

The Linux clone is large; `--depth 1` is enough to build. On a
case-insensitive disk (the Mac's default), 13 files whose names differ only
in case show up as modified; mark them so Git leaves them alone:

```sh
cd ~/git/linux
git status --short | awk '/^ M/ {print $2}' | xargs git update-index --skip-worktree
```

(Check first that only those netfilter headers and sources and one
`tools/memory-model` litmus test are listed.)

## Previous

```sh
cd ~/git/trunk
python3 ../next68k-linux/previous/patches.py apply
mkdir build && cd build
cmake -DCMAKE_OSX_ARCHITECTURES=arm64 ..
make -j8
```

`run-previous.sh` runs `../trunk/build/src/Previous.app/Contents/MacOS/Previous`
(set `PREVIOUS=` to use another binary). Previous's source tree has the
NeXT ROMs (1.0 for the 68030, 2.5 and 3.3 for the 68040) and the
NeXTdimension's; the build copies them next to the binary. The scripts give
Previous its own home (`out/previous-home`), so they don't touch your own
Previous settings.

## Kernel, loader, images

```sh
cd ~/git/next68k-linux
docker build -t next68k-build docker
./scripts/build-kernel.sh                 # next_defconfig + vmlinux
./scripts/build-image.sh INITRD= \
    CMDLINE="earlyprintk console=ttyPZ0,9600 root=/dev/sda1 rootwait rw"
```

Then the Debian disk, which takes a while (see "Debian" in `README.md`):

```sh
./scripts/build-kernel-deb.sh             # the kernel as a .deb, which the next step installs
./scripts/build-debian.sh                 # out/debian-root.img, from Debian Ports sid
./scripts/build-disk.sh debian            # out/disk.img: root, swap, boot partition (uses
                                          # out/bootblock from build-image.sh)
./scripts/boot-wait.sh                    # netboot a NeXTcube to a login
python3 scripts/serial-run.py --login "uname -a"
```

Root's password is `next`. `README.md` has the rest: the other machines,
booting from disk, the network, the busybox initramfs.

## Optional tools

- `./scripts/build-tests.sh`: the test programs in `tests/`.
- `./scripts/build-i860-binutils.sh`: the i860 assembler, only needed to
  change the NeXTdimension firmware (`nd-firmware/`); the kernel ships its
  output.
