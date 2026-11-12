# SPDX-License-Identifier: GPL-2.0
#
# NeXT Linux loader
#
#   make                        build loader.bin
#   make image KERNEL=vmlinux [INITRD=file] [CMDLINE="..."]
#                               build the netboot image "boot"
#   make bootblock              build "bootblock", the loader alone for a
#                               disk's boot blocks

CROSS_COMPILE	?= m68k-linux-gnu-
LINUX		?= ../../linux

CC		:= $(CROSS_COMPILE)gcc
OBJCOPY		:= $(CROSS_COMPILE)objcopy

CFLAGS		:= -m68020-40 -msoft-float -Os -std=gnu11 -ffreestanding \
		   -fno-builtin -fno-pic -fno-stack-protector \
		   -fno-asynchronous-unwind-tables -nostdinc \
		   -isystem $(shell $(CC) -print-file-name=include) \
		   -Iinclude -I$(LINUX)/arch/m68k/include/uapi \
		   -Wall -Wextra -Wno-unused-parameter
ASFLAGS		:= -m68020-40
LDFLAGS		:= -nostdlib -static -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-T,loader.ld

OBJS		:= start.o main.o console.o disk.o scsi.o

CMDLINE		?= earlyprintk console=ttyPZ0,9600
OUTPUT		?= boot
BOOTBLOCK	?= bootblock

all: loader.bin

loader.elf: $(OBJS) loader.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

loader.bin: loader.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c loader.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

image: loader.bin
	python3 mkimage.py --loader loader.elf --loader-bin loader.bin \
		--kernel $(KERNEL) $(if $(INITRD),--initrd $(INITRD)) \
		--cmdline "$(CMDLINE)" -o $(OUTPUT)

bootblock: loader.bin
	python3 mkimage.py --loader loader.elf --loader-bin loader.bin \
		-o $(BOOTBLOCK)

clean:
	rm -f $(OBJS) loader.elf loader.bin

.PHONY: all image bootblock clean
