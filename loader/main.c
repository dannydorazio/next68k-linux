// SPDX-License-Identifier: GPL-2.0
/*
 * NeXT Linux loader
 *
 * The ROM monitor loads this program together with an appended kernel
 * image (and optionally an initrd) and calls it.  It sizes memory from
 * the ROM's SIMM table, builds the Linux/m68k bootinfo, moves everything
 * into place and starts the kernel with the MMU and caches off.
 *
 * Booted from a SCSI disk, the ROM loads only this program, from the
 * disk's boot blocks, and the kernel comes from the disk (disk.c).
 */

#include <asm/bootinfo.h>
#include <asm/bootinfo-next.h>

#include "loader.h"

#define MAX_CHUNKS	N_SIMM
#define BOOTINFO_MAX	1024

struct chunk {
	u32 addr;
	u32 size;
};

static struct chunk chunks[MAX_CHUNKS];
static int nchunks;
static u32 low_end;	/* end of the memory contiguous with bank 0 */

static u8 bootinfo[BOOTINFO_MAX] __attribute__((aligned(4)));
static u32 bootinfo_len;
static int bootinfo_overflow;

static const char *model_name(int type)
{
	switch (type) {
	case NEXT_MODEL_CUBE:		return "NeXT Computer (68030)";
	case NEXT_MODEL_WARP9:		return "NeXTstation";
	case NEXT_MODEL_X15:		return "NeXTcube";
	case NEXT_MODEL_WARP9C:		return "NeXTstation Color";
	case NEXT_MODEL_TURBO_MONO:	return "NeXTstation Turbo";
	case NEXT_MODEL_TURBO_COLOR:	return "NeXTstation Turbo Color";
	case NEXT_MODEL_CUBE_TURBO:	return "NeXTcube Turbo";
	default:			return "unknown NeXT";
	}
}

/* Distance between RAM banks; the SIMM codes scale from it. */
static u32 bank_stride(int type)
{
	switch (type) {
	case NEXT_MODEL_WARP9C:
		return 0x00800000;
	case NEXT_MODEL_TURBO_MONO:
	case NEXT_MODEL_TURBO_COLOR:
	case NEXT_MODEL_CUBE_TURBO:
		return 0x02000000;
	default:
		return 0x01000000;
	}
}

static void size_memory(void *mg, int type)
{
	u32 stride = bank_stride(type);
	int i;

	for (i = 0; i < N_SIMM; i++) {
		u8 simm = MON(u8, mg, MG_simm + i);
		u32 addr = NEXT_RAMBASE + i * stride;
		u32 size;

		switch (simm & SIMM_SIZE) {
		case SIMM_SIZE_16MB:
			size = stride;
			break;
		case SIMM_SIZE_4MB:
			size = stride / 4;
			break;
		case SIMM_SIZE_1MB:
			size = stride / 16;
			break;
		default:
			continue;
		}
		printf("  bank %d: %08x-%08x (SIMM code %x)\n",
		       i, addr, addr + size - 1, simm);

		/* merge banks that happen to be contiguous */
		if (nchunks && chunks[nchunks - 1].addr +
			       chunks[nchunks - 1].size == addr)
			chunks[nchunks - 1].size += size;
		else {
			chunks[nchunks].addr = addr;
			chunks[nchunks].size = size;
			nchunks++;
		}
	}

	/*
	 * The ROM's data at the top stays in the last chunk, which the
	 * kernel reserves: the '030 kernel maps memory in 256 KB steps.
	 */
	if (nchunks) {
		low_end = chunks[0].addr + chunks[0].size;
		if (nchunks == 1)
			low_end -= NEXT_ROM_RESERVED;
	}
}

static void bi_add(u16 tag, const void *data, u32 len)
{
	u32 size = ALIGN_UP(sizeof(struct bi_record) + len, 4);
	struct bi_record *rec = (struct bi_record *)(bootinfo + bootinfo_len);

	/* keep room for the terminating BI_LAST */
	if (bootinfo_len + size + 4 > BOOTINFO_MAX) {
		bootinfo_overflow = 1;
		return;
	}
	rec->tag = tag;
	rec->size = size;
	memset(rec->data, 0, size - sizeof(*rec));
	memcpy(rec->data, data, len);
	bootinfo_len += size;
}

static void bi_add_u32(u16 tag, u32 val)
{
	bi_add(tag, &val, sizeof(val));
}

static void bi_finish(void)
{
	memset(bootinfo + bootinfo_len, 0, 4);	/* BI_LAST */
	bootinfo_len += 4;
}

/*
 * The kernel starts with a table of the bootinfo versions it understands
 * (struct bootversion).  Refuse kernels built without NeXT support.
 */
static int check_kernel_version(const u8 *image)
{
	const u32 *p = (const u32 *)(image + 2);

	if (*(const u16 *)image >> 8 != 0x60 || *p++ != BOOTINFOV_MAGIC) {
		printf("error: kernel has no bootinfo version table\n");
		return -1;
	}
	for (; p[0]; p += 2) {
		if (p[0] != MACH_NEXT)
			continue;
		if (BI_VERSION_MAJOR(p[1]) != BI_VERSION_MAJOR(NEXT_BOOTI_VERSION)) {
			printf("error: kernel uses NeXT bootinfo version %d.%d, loader %d.%d\n",
			       BI_VERSION_MAJOR(p[1]), BI_VERSION_MINOR(p[1]),
			       BI_VERSION_MAJOR(NEXT_BOOTI_VERSION),
			       BI_VERSION_MINOR(NEXT_BOOTI_VERSION));
			return -1;
		}
		return 0;
	}
	printf("error: kernel was built without NeXT support (CONFIG_NEXT)\n");
	return -1;
}

/*
 * The ROM may leave the ethernet receiver and DMA channels running after a
 * network or disk boot.  Stop them so nothing writes into the kernel's
 * memory behind its back.
 */
static void quiesce_devices(int is_040, int is_turbo)
{
	static const u32 dma_csrs[] = {
		0x010, 0x040, 0x080, 0x090, 0x0d0, 0x110, 0x150,
		/* not on Turbo machines, where they bus error */
		0x050, 0x0c0, 0x1c0, 0x1d0,
	};
	u32 bmap = is_040 ? NEXT_BMAP_OFFSET : 0;
	unsigned int i, n = is_turbo ? 7 : sizeof(dma_csrs) / sizeof(dma_csrs[0]);

	*(volatile u8 *)(NEXT_IO + bmap + NEXT_ENET + NEXT_ENET_RESET) =
		NEXT_ENET_RESET_MODE;
	for (i = 0; i < n; i++) {
		volatile u32 *csr = (volatile u32 *)(NEXT_IO + dma_csrs[i]);

		*csr = NEXT_DMACSR_RESET | NEXT_DMACSR_CLRCOMPLETE;
		*csr = 0;
	}
}

static int overlaps(u32 a, u32 alen, u32 b, u32 blen)
{
	return a < b + blen && b < a + alen;
}

/* Did the ROM boot from a SCSI disk? */
static int booted_from_disk(void *mg, const char *rom_arg)
{
	const char *dev = MON(const char *, mg, MG_boot_dev);

	if (!dev)
		dev = rom_arg;
	return dev && dev[0] == 's' && dev[1] == 'd';
}

int loader_main(void *mg, u32 rom_vbr, const char *rom_arg)
{
	const struct payload *pl = &_payload;
	/*
	 * The 68030's ROM (1.0) has a shorter mg that ends before the
	 * machine type, at the very top of memory: there is only one 68030
	 * model.
	 */
	int is_040 = cpu_is_040();	/* or 68060: they work alike here */
	int is_060 = is_040 && cpu_is_060();
	int type = is_040 ? MON(u8, mg, MG_machine_type) : NEXT_MODEL_CUBE;
	int rev = is_040 ? MON(u8, mg, MG_board_rev) : 0;
	int is_turbo = type == NEXT_MODEL_TURBO_MONO ||
		       type == NEXT_MODEL_TURBO_COLOR ||
		       type == NEXT_MODEL_CUBE_TURBO;
	const u8 *ether = MON(const u8 *, mg, MG_clientetheraddr);
	const char *extra_args = NULL;
	char cmdline[PAYLOAD_CMDLINE_MAX * 2];
	struct handoff h;
	u32 image_end, kernel_dst, kernel_end, tramp, tramp_len, initrd_dst = 0;
	u32 sp;
	int i;

	console_init(mg, is_040);
	asm volatile("movel %%sp,%0" : "=r" (sp));

	printf("\nNeXT Linux loader\n");
	printf("%s (machine type %d, board rev %d), %s\n", model_name(type),
	       type, rev, is_060 ? "68060" : is_040 ? "68040" : "68030");
	printf("ROM: mg %08x vbr %08x sp %08x\n", (u32)mg, rom_vbr, sp);
	printf("ROM boot command: %s\n", rom_arg ? rom_arg : "");

	image_end = (u32)_start + pl->image_size;
	if (booted_from_disk(mg, rom_arg)) {
		data_cache_off(is_040);
		pl = disk_load(is_040, is_turbo, rom_arg,
			       ALIGN_UP((u32)&_payload, 4096), &image_end);
		if (!pl)
			return 0;
	} else if (pl->magic != PAYLOAD_MAGIC) {
		printf("error: no kernel appended to the loader (bad payload magic %08x)\n",
		       pl->magic);
		return 0;
	}

	printf("Memory:\n");
	size_memory(mg, type);
	if (!nchunks || chunks[0].addr != NEXT_RAMBASE) {
		printf("error: no memory in bank 0\n");
		return 0;
	}

	if (check_kernel_version((const u8 *)pl + pl->kernel_offset))
		return 0;

	/*
	 * Memory layout.  The kernel goes to the start of bank 0, where
	 * physical NEXT_RAMBASE corresponds to virtual 0.  The trampoline
	 * and a copy of the bootinfo go above both the loaded image and the
	 * kernel's final location.  The initrd goes to the top of the memory
	 * contiguous with bank 0: with 8 MB SIMMs (NeXTstation Color), the
	 * loaded image itself runs into bank 1.
	 */
	kernel_dst = NEXT_RAMBASE + pl->kernel_vaddr;
	kernel_end = kernel_dst + pl->kernel_memsize;

	/*
	 * Command line: the built-in part, then anything that follows the
	 * boot file in the ROM boot command, as in "b en()boot root=...".
	 */
	memcpy(cmdline, pl->cmdline, PAYLOAD_CMDLINE_MAX);
	cmdline[PAYLOAD_CMDLINE_MAX - 1] = 0;
	if (rom_arg) {
		extra_args = rom_arg;
		while (*extra_args && *extra_args != ' ')
			extra_args++;
		while (*extra_args == ' ')
			extra_args++;
	}
	if (extra_args && *extra_args) {
		u32 n = strlen(cmdline);
		u32 m = strlen(extra_args);

		if (n + 1 + m < sizeof(cmdline)) {
			if (n)
				cmdline[n++] = ' ';
			memcpy(cmdline + n, extra_args, m + 1);
		}
	}

	bi_add_u32(BI_MACHTYPE, MACH_NEXT);
	bi_add_u32(BI_CPUTYPE, is_060 ? CPU_68060 : is_040 ? CPU_68040 : CPU_68030);
	bi_add_u32(BI_FPUTYPE, is_060 ? FPU_68060 : is_040 ? FPU_68040 : FPU_68882);
	bi_add_u32(BI_MMUTYPE, is_060 ? MMU_68060 : is_040 ? MMU_68040 : MMU_68030);
	for (i = 0; i < nchunks; i++)
		bi_add(BI_MEMCHUNK, &chunks[i], sizeof(chunks[i]));
	bi_add(BI_COMMAND_LINE, cmdline, strlen(cmdline) + 1);
	bi_add_u32(BI_NEXT_MODEL, type);
	bi_add_u32(BI_NEXT_BOARD_REV, rev);
	if (ether)
		bi_add(BI_NEXT_ETHER_ADDR, ether, 6);
	bi_add_u32(BI_NEXT_ROM_MG, (u32)mg);
	bi_add_u32(BI_NEXT_ROM_VBR, rom_vbr);

	if (pl->initrd_size) {
		u32 ramdisk[2];

		initrd_dst = ALIGN_DOWN(low_end - pl->initrd_size, 4096);
		ramdisk[0] = initrd_dst;
		ramdisk[1] = pl->initrd_size;
		bi_add(BI_RAMDISK, ramdisk, sizeof(ramdisk));
	}
	bi_finish();
	if (bootinfo_overflow) {
		printf("error: bootinfo too large\n");
		return 0;
	}

	tramp_len = trampoline_end - trampoline;
	tramp = ALIGN_UP(image_end > kernel_end + bootinfo_len ?
			 image_end : kernel_end + bootinfo_len, 4096);

	printf("Kernel: %08x-%08x (image %u bytes, bss %u bytes)\n",
	       kernel_dst, kernel_end - 1, pl->kernel_size,
	       pl->kernel_memsize - pl->kernel_size);
	if (pl->initrd_size)
		printf("Initrd: %08x-%08x\n", initrd_dst,
		       initrd_dst + pl->initrd_size - 1);
	printf("Command line: %s\n", cmdline);

	if (tramp + tramp_len + bootinfo_len > (initrd_dst ? initrd_dst : low_end)) {
		printf("error: not enough memory from bank 0 for the kernel%s\n",
		       pl->initrd_size ? " and initrd" : "");
		return 0;
	}
	/* the ROM's stack is still in use until the kernel starts */
	if (overlaps(sp - 0x1000, 0x2000, kernel_dst, kernel_end + bootinfo_len - kernel_dst) ||
	    overlaps(sp - 0x1000, 0x2000, tramp, tramp_len + bootinfo_len) ||
	    (initrd_dst && overlaps(sp - 0x1000, 0x2000, initrd_dst, pl->initrd_size))) {
		printf("error: ROM stack at %08x is in the way\n", sp);
		return 0;
	}

	if (pl->initrd_size)
		memmove((void *)initrd_dst, (const u8 *)pl + pl->initrd_offset,
			pl->initrd_size);

	memcpy((void *)tramp, trampoline, tramp_len);
	memcpy((void *)(tramp + tramp_len), bootinfo, bootinfo_len);

	h.image_size = ALIGN_UP(pl->kernel_size, 4);
	h.bss_size = pl->kernel_memsize - h.image_size;
	h.bootinfo_size = bootinfo_len;
	h.image_src = (u32)pl + pl->kernel_offset;
	h.image_dst = kernel_dst;
	h.bootinfo_src = tramp + tramp_len;
	h.entry = kernel_dst;
	h.trampoline = tramp;

	printf("Starting kernel at %08x\n\n", kernel_dst);
	console_flush();
	quiesce_devices(is_040, is_turbo);
	enter_kernel(&h, is_040);
}
