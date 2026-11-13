// SPDX-License-Identifier: GPL-2.0
/*
 * nextlabel: give a disk a NeXT disk label for Linux, as the installer's
 * partitioning step (next-partitioner) does.
 *
 *   nextlabel DEVICE SWAP_MB BOOT_MB BOOTBLOCK [NAME]
 *
 * Partition a is the root file system and fills the disk, then b is SWAP_MB
 * of swap (none if 0), then the boot partition of BOOT_MB, which the loader
 * reads the kernel from (struct bootpart in loader/loader.h).  BOOTBLOCK, the
 * loader built with "make bootblock", goes into the label's two boot
 * blocks.  The label is version 3 ("dlV3") with 1024-byte label sectors, as
 * scripts/mkdisk.py makes it; see there for the layout.  The front porch is
 * cleared first, and the kernel is asked to read the new partitions.
 *
 * Prints the partitions' numbers as the kernel names them (sdX1, ...):
 * "root 1", "swap 2", "boot 3".
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fs.h>

#define LABEL_V3	0x646C5633	/* "dlV3" */
#define LABEL_BYTES	7240
#define PART_OFFSET	190
#define PART_SIZE	46
#define NPART		8
#define V3_CHECKSUM	558
#define BOOT_BLKNO	124
#define BOOTFILE	132
#define SECSIZE		1024
#define FRONT		160		/* label sectors before partition 0 */
#define BOOTBLOCK_MAX	(64 * 1024)

static const long bootblocks[2] = { 32 * 1024, 96 * 1024 };

static void put16(unsigned char *p, int v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static void put32(unsigned char *p, long v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void putstr(unsigned char *p, const char *s, size_t n)
{
	strncpy((char *)p, s, n);
}

static void partition(unsigned char *p, long offset, long size,
		      const char *type, const char *mountpt)
{
	put32(p, offset);
	put32(p + 4, size);
	put16(p + 8, 8192);		/* block size */
	put16(p + 10, 1024);		/* fragment size */
	p[12] = 't';			/* optimisation: time */
	put16(p + 14, 16);		/* cylinders per group */
	put16(p + 16, 4096);		/* bytes per inode */
	p[18] = 5;			/* minfree % */
	putstr(p + 20, mountpt, 16);
	p[36] = 1;			/* automount */
	putstr(p + 37, type, 8);
}

static void die(const char *what)
{
	fprintf(stderr, "nextlabel: %s: %s\n", what, strerror(errno));
	exit(1);
}

static void pwrite_all(int fd, const void *buf, size_t n, off_t pos)
{
	if (pwrite(fd, buf, n, pos) != (ssize_t)n)
		die("write");
}

int main(int argc, char **argv)
{
	static unsigned char label[LABEL_BYTES], boot[BOOTBLOCK_MAX];
	static unsigned char zero[FRONT * SECSIZE];
	unsigned long long bytes;
	long total, swap, bootsz, root;
	unsigned int sum;
	int fd, bfd, i, n;
	ssize_t len;

	if (argc < 5 || argc > 6) {
		fprintf(stderr, "usage: nextlabel DEVICE SWAP_MB BOOT_MB BOOTBLOCK [NAME]\n");
		return 2;
	}
	swap = atol(argv[2]) * 1024;
	bootsz = atol(argv[3]) * 1024;

	bfd = open(argv[4], O_RDONLY);
	if (bfd < 0)
		die(argv[4]);
	len = read(bfd, boot, sizeof(boot));
	if (len <= 0 || read(bfd, zero, 1) != 0) {
		fprintf(stderr, "nextlabel: %s: empty or over %d bytes\n",
			argv[4], BOOTBLOCK_MAX);
		return 1;
	}
	close(bfd);

	fd = open(argv[1], O_RDWR | O_EXCL);
	if (fd < 0)
		die(argv[1]);
	if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
		struct stat st;

		if (fstat(fd, &st) < 0)
			die(argv[1]);
		bytes = st.st_size;
	}
	total = bytes / SECSIZE;
	root = total - FRONT - swap - bootsz;
	if (root < 64 * 1024) {
		fprintf(stderr, "nextlabel: %s: too small (%llu MB)\n",
			argv[1], bytes >> 20);
		return 1;
	}

	put32(label, LABEL_V3);
	put32(label + 8, total);
	putstr(label + 12, argc > 5 ? argv[5] : "linux", 24);
	putstr(label + 44, "Linux disk", 24);
	putstr(label + 68, "fixed_rw_scsi", 24);
	put32(label + 92, SECSIZE);
	put32(label + 96, 1);			/* tracks */
	put32(label + 100, 32);			/* sectors per track */
	put32(label + 104, total / 32);		/* cylinders */
	put32(label + 108, 3600);		/* rpm */
	put16(label + 112, FRONT);
	put32(label + BOOT_BLKNO, bootblocks[0] / SECSIZE);
	put32(label + BOOT_BLKNO + 4, bootblocks[1] / SECSIZE);
	/* passed to the boot program, which ignores it; the ROM needs one */
	putstr(label + BOOTFILE, "linux", 24);
	label[188] = 'a';			/* root partition */
	label[189] = 'b';			/* read-write partition */

	n = 0;
	partition(label + PART_OFFSET, 0, root, "ext4", "/");
	printf("root %d\n", ++n);
	if (swap) {
		partition(label + PART_OFFSET + n * PART_SIZE, root, swap,
			  "swap", "");
		printf("swap %d\n", ++n);
	}
	partition(label + PART_OFFSET + n * PART_SIZE, root + swap, bootsz,
		  "boot", "");
	printf("boot %d\n", ++n);
	for (i = n; i < NPART; i++)
		partition(label + PART_OFFSET + i * PART_SIZE, -1, -1, "", "");

	for (sum = 0, i = 0; i < V3_CHECKSUM; i += 2)
		sum += label[i] << 8 | label[i + 1];
	sum += sum >> 16;
	put16(label + V3_CHECKSUM, sum & 0xffff);

	memset(zero, 0, sizeof(zero));
	pwrite_all(fd, zero, sizeof(zero), 0);
	for (i = 0; i < 2; i++)
		pwrite_all(fd, boot, sizeof(boot), bootblocks[i]);
	pwrite_all(fd, label, sizeof(label), 0);
	if (fsync(fd) < 0)
		die("fsync");
	if (ioctl(fd, BLKRRPART) < 0 && errno != ENOTTY)
		die("re-reading the partitions");
	close(fd);
	return 0;
}
