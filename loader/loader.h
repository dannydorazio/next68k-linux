/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LOADER_H
#define _LOADER_H

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long		size_t;

#define NULL			((void *)0)
#define ALIGN_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)	((x) & ~((a) - 1))

/*
 * ROM monitor globals ("struct mon_global" in the NeXT ROM).  Offsets as
 * documented by NetBSD/next68k.
 */
#define MG_simm			0	/* char[4]: SIMM bank types */
#define MG_boot_dev		240	/* char *: "sd", "en", ... */
#define MG_putc			734	/* int (*)(int) */
#define MG_clientetheraddr	788	/* u8 * */
#define MG_machine_type		936	/* char, not in the 68030's ROM */
#define MG_board_rev		937	/* char, not in the 68030's ROM */

#define MON(type, mg, off)	(*(type *)((u32)(mg) + (off)))

#define N_SIMM			4
#define SIMM_SIZE		0x03
#define SIMM_SIZE_EMPTY		0x00
#define SIMM_SIZE_16MB		0x01
#define SIMM_SIZE_4MB		0x02
#define SIMM_SIZE_1MB		0x03

#define NEXT_IO			0x02000000
#define NEXT_BMAP_OFFSET	0x00100000	/* device alias used on '040s */
#define NEXT_ENET		0x00006000	/* MB8795 */
#define NEXT_ENET_RESET		6
#define NEXT_ENET_RESET_MODE	0x80
#define NEXT_DMACSR_CLRCOMPLETE	0x00080000
#define NEXT_DMACSR_RESET	0x00100000

#define NEXT_RAMBASE		0x04000000
/* The ROM keeps data in the top 8 KB of the last memory bank. */
#define NEXT_ROM_RESERVED	0x2000

/* Appended by mkimage.py directly after the loader binary. */
#define PAYLOAD_MAGIC		0x4e58424c	/* "NXBL" */
#define PAYLOAD_CMDLINE_MAX	256

struct payload {
	u32 magic;
	u32 image_size;		/* loader + payload, as loaded by the ROM */
	u32 kernel_offset;	/* from the start of this header */
	u32 kernel_size;	/* bytes of kernel image */
	u32 kernel_memsize;	/* kernel image + bss, i.e. up to _end */
	u32 kernel_vaddr;	/* virtual address of the image's first byte */
	u32 initrd_offset;
	u32 initrd_size;
	char cmdline[PAYLOAD_CMDLINE_MAX];
};

/*
 * At the start of a disk's boot partition (disk.c), followed by the
 * kernel's ELF file and optionally an initrd.  Offsets are from the start
 * of the partition, and the kernel's must be a multiple of 4096.
 */
#define BOOTPART_MAGIC		0x4e58424b	/* "NXBK" */

struct bootpart {
	u32 magic;
	u32 kernel_offset;
	u32 kernel_size;
	u32 initrd_offset;
	u32 initrd_size;
	char cmdline[PAYLOAD_CMDLINE_MAX];
};

/* Register image for enter_kernel(); the order matches its moveml. */
struct handoff {
	u32 image_size;		/* d0 */
	u32 bss_size;		/* d1 */
	u32 bootinfo_size;	/* d2 */
	u32 image_src;		/* a0 */
	u32 image_dst;		/* a1 */
	u32 bootinfo_src;	/* a2 */
	u32 entry;		/* a3 */
	u32 trampoline;		/* a5 */
};

void enter_kernel(const struct handoff *h, int is_040) __attribute__((noreturn));
void data_cache_off(int is_040);
int cpu_is_040(void);
int cpu_is_060(void);
extern const char _start[], trampoline[], trampoline_end[];
extern const struct payload _payload;

const struct payload *disk_load(int is_040, int is_turbo, const char *rom_arg,
				u32 dst, u32 *end);

void scsi_init(int is_040, int is_turbo);
int scsi_cmd(int target, int lun, const u8 *cdb, int cdb_len,
	     void *buf, u32 len);
int scsi_probe(int target, int lun);
u32 scsi_block_size(int target, int lun, void *buf);
int scsi_read(int target, int lun, u32 bsize, u32 lba, u32 count, void *buf);

void console_init(void *mg, int is_040);
void console_flush(void);
void printf(const char *fmt, ...);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
size_t strlen(const char *s);

#endif
