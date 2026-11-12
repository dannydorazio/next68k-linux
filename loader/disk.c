// SPDX-License-Identifier: GPL-2.0
/*
 * Loading the kernel from disk
 *
 * When the ROM boots from a SCSI disk, it loads this loader from the boot
 * blocks named in the disk's NeXT label.  The kernel is in the label's
 * partition of type "boot": a struct bootpart header, then the kernel's
 * ELF file as installed in /boot, then optionally an initrd.  This reads
 * them into memory in the layout of a netboot image's payload, so the rest
 * of the loader doesn't know the difference.
 */

#include "loader.h"

#define LABEL_SIZE		8192
#define LABEL_V1		0x4e655854	/* "NeXT" */
#define LABEL_V2		0x646c5632	/* "dlV2" */
#define LABEL_V3		0x646c5633	/* "dlV3" */
#define LABEL_SECSIZE		92
#define LABEL_FRONT		112
#define LABEL_PARTS		190
#define LABEL_NPARTS		8
#define PART_SIZE		46
#define PART_OFFSET		0
#define PART_NSECT		4
#define PART_TYPE		37
#define PART_TYPE_LEN		8

#define ELF_PT_LOAD		1
#define MAX_SEGMENTS		8

#define BOOTVERSION_MAGIC	0x4249561a	/* struct bootversion */

struct segment {
	u32 offset, vaddr, filesz, memsz;
};

static int target, lun;
static u32 bsize;

static u32 get32(const u8 *p)
{
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static u16 get16(const u8 *p)
{
	return p[0] << 8 | p[1];
}

/* Read len bytes from byte offset pos on the disk into buf. */
static int disk_read(u32 pos, u32 len, void *buf)
{
	if (pos % bsize) {
		printf("error: disk offset %08x is not block aligned\n", pos);
		return -1;
	}
	return scsi_read(target, lun, bsize, pos / bsize,
			 ALIGN_UP(len, bsize) / bsize, buf);
}

/* "sd(count,lun,part)": the count-th disk, as in the ROM's boot command */
static void parse_boot_arg(const char *arg, int *count, int *l)
{
	int vals[3] = { 0, 0, 0 }, n = 0;

	*count = *l = 0;
	if (!arg || arg[0] != 's' || arg[1] != 'd' || arg[2] != '(')
		return;
	for (arg += 3; *arg && *arg != ')' && n < 3; arg++) {
		if (*arg == ',')
			n++;
		else if (*arg >= '0' && *arg <= '9')
			vals[n] = vals[n] * 10 + *arg - '0';
	}
	*count = vals[0];
	*l = vals[1];
}

static int find_disk(const char *arg)
{
	int count, n = 0;

	parse_boot_arg(arg, &count, &lun);
	for (target = 0; target < 7; target++) {
		if (scsi_probe(target, lun))
			continue;
		if (n++ == count)
			return 0;
	}
	printf("error: no SCSI disk %d (lun %d)\n", count, lun);
	return -1;
}

/* Find the boot partition; its byte offset and size. */
static int find_boot_partition(u8 *buf, u32 *start, u32 *size)
{
	u32 magic, secsize;
	int i;

	if (disk_read(0, LABEL_SIZE, buf))
		return -1;
	magic = get32(buf);
	if (magic != LABEL_V1 && magic != LABEL_V2 && magic != LABEL_V3) {
		printf("error: no NeXT disk label on target %d\n", target);
		return -1;
	}
	secsize = get32(buf + LABEL_SECSIZE);
	for (i = 0; i < LABEL_NPARTS; i++) {
		const u8 *p = buf + LABEL_PARTS + i * PART_SIZE;
		const char *type = (const char *)p + PART_TYPE;

		if ((int)get32(p + PART_NSECT) <= 0 || type[0] != 'b' ||
		    type[1] != 'o' || type[2] != 'o' || type[3] != 't' || type[4])
			continue;
		*start = (get16(buf + LABEL_FRONT) + get32(p + PART_OFFSET)) * secsize;
		*size = get32(p + PART_NSECT) * secsize;
		printf("Boot partition %c: target %d, bytes %08x-%08x\n",
		       'a' + i, target, *start, *start + *size - 1);
		return 0;
	}
	printf("error: no partition of type \"boot\" on target %d\n", target);
	return -1;
}

static int read_elf_segments(u32 elf_pos, u8 *buf, struct segment *seg)
{
	u32 phoff;
	int phentsize, phnum, i, n = 0;

	if (disk_read(elf_pos, 4096, buf))
		return -1;
	if (get32(buf) != 0x7f454c46 || buf[4] != 1 || buf[5] != 2 ||
	    get16(buf + 18) != 4) {
		printf("error: the kernel is not a 32-bit m68k ELF file\n");
		return -1;
	}
	phoff = get32(buf + 28);
	phentsize = get16(buf + 42);
	phnum = get16(buf + 44);
	if (phoff + phnum * phentsize > 4096) {
		printf("error: ELF program headers beyond the first 4 KB\n");
		return -1;
	}
	for (i = 0; i < phnum; i++) {
		const u8 *ph = buf + phoff + i * phentsize;
		struct segment s;
		int j;

		if (get32(ph) != ELF_PT_LOAD || !get32(ph + 20))
			continue;
		if (n == MAX_SEGMENTS) {
			printf("error: too many ELF segments\n");
			return -1;
		}
		s.offset = get32(ph + 4);
		s.vaddr = get32(ph + 8);
		s.filesz = get32(ph + 16);
		s.memsz = get32(ph + 20);
		/* keep them sorted by address */
		for (j = n; j > 0 && seg[j - 1].vaddr > s.vaddr; j--)
			seg[j] = seg[j - 1];
		seg[j] = s;
		n++;
	}
	if (!n)
		printf("error: no loadable ELF segments\n");
	return n;
}

/*
 * Read the kernel and initrd into memory at dst, laid out like a netboot
 * payload, which this returns.  *end is set to the end of the data.
 */
const struct payload *disk_load(int is_040, int is_turbo, const char *rom_arg,
				u32 dst, u32 *end)
{
	struct payload *pl = (struct payload *)dst;
	struct bootpart *bp;
	struct segment seg[MAX_SEGMENTS];
	u32 part, part_size, low, top = 0, memtop = 0, image;
	u32 kernel_pos, initrd_pos;
	u8 *buf;
	int nseg, i;

	/* scratch space for the label and headers, past the payload header */
	buf = (u8 *)ALIGN_UP(dst + sizeof(*pl), 4096);

	scsi_init(is_040, is_turbo);
	if (find_disk(rom_arg))
		return NULL;
	bsize = scsi_block_size(target, lun, buf);
	if (!bsize || bsize > 4096 || 4096 % bsize) {
		printf("error: unusable block size %u\n", bsize);
		return NULL;
	}
	if (find_boot_partition(buf, &part, &part_size))
		return NULL;

	if (disk_read(part, 4096, buf))
		return NULL;
	bp = (struct bootpart *)buf;
	if (bp->magic != BOOTPART_MAGIC) {
		printf("error: no kernel in the boot partition (magic %08x)\n",
		       bp->magic);
		return NULL;
	}
	if (bp->kernel_offset + bp->kernel_size > part_size ||
	    bp->initrd_offset + bp->initrd_size > part_size) {
		printf("error: bad boot partition header\n");
		return NULL;
	}
	memset(pl, 0, sizeof(*pl));
	memcpy(pl->cmdline, bp->cmdline, PAYLOAD_CMDLINE_MAX);
	pl->cmdline[PAYLOAD_CMDLINE_MAX - 1] = 0;
	pl->initrd_size = bp->initrd_size;
	kernel_pos = part + bp->kernel_offset;
	initrd_pos = part + bp->initrd_offset;

	/* the ELF headers go where bp was */
	nseg = read_elf_segments(kernel_pos, buf, seg);
	if (nseg <= 0)
		return NULL;

	/*
	 * Each segment goes to its place relative to the lowest one.  Reads
	 * are whole blocks and may run past a segment's file data; that
	 * part is cleared again below, or overwritten by the next segment.
	 */
	image = ALIGN_UP(dst + sizeof(*pl), 4096);
	low = seg[0].vaddr;
	printf("Loading the kernel");
	for (i = 0; i < nseg; i++) {
		if (seg[i].filesz &&
		    disk_read(kernel_pos + seg[i].offset, seg[i].filesz,
			      (void *)(image + seg[i].vaddr - low)))
			return NULL;
		if (seg[i].vaddr + seg[i].filesz > top)
			top = seg[i].vaddr + seg[i].filesz;
		if (seg[i].vaddr + seg[i].memsz > memtop)
			memtop = seg[i].vaddr + seg[i].memsz;
		printf(".");
	}
	printf("\n");
	for (i = 0; i < nseg; i++) {
		u32 from = seg[i].vaddr + seg[i].filesz;
		u32 to = i + 1 < nseg ? seg[i + 1].vaddr : ALIGN_UP(top, 4);

		if (to > from)
			memset((void *)(image + from - low), 0, to - from);
	}

	/* the kernel image starts at _stext, with its bootinfo version table */
	for (i = 0; i < 4; i++) {
		const u8 *p = (const u8 *)image + i * 4096;

		if (low + i * 4096 < top && p[0] == 0x60 &&
		    get32(p + 2) == BOOTVERSION_MAGIC)
			break;
	}
	if (i == 4) {
		printf("error: no bootinfo version table in the kernel\n");
		return NULL;
	}
	pl->kernel_vaddr = low + i * 4096;
	pl->kernel_offset = image + i * 4096 - dst;
	pl->kernel_size = ALIGN_UP(top - pl->kernel_vaddr, 4);
	pl->kernel_memsize = ALIGN_UP(memtop, 4) - pl->kernel_vaddr;
	*end = dst + pl->kernel_offset + pl->kernel_size;

	if (pl->initrd_size) {
		u32 initrd = ALIGN_UP(*end + bsize, 4096);

		printf("Loading the initrd (%u bytes)\n", pl->initrd_size);
		if (disk_read(initrd_pos, pl->initrd_size,
			      (void *)initrd))
			return NULL;
		pl->initrd_offset = initrd - dst;
		*end = initrd + pl->initrd_size;
	}
	pl->magic = PAYLOAD_MAGIC;
	return pl;
}
