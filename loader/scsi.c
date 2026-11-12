// SPDX-License-Identifier: GPL-2.0
/*
 * Polled SCSI disk reads for booting from disk
 *
 * The NCR 53C90A is driven without interrupts: every step waits for the
 * chip's interrupt bit.  Sectors come in through the SCSI channel of the
 * DMA controller, straight into the caller's buffer, which must be 16-byte
 * aligned (the DMA burst size), as must the length.  Anything else, such
 * as READ CAPACITY's 8 bytes, comes a byte at a time through the FIFO.
 * After NetBSD's stand/boot/scsi.c.
 */

#include "loader.h"

#define ESP_TCL		0x00	/* transfer count */
#define ESP_TCM		0x01
#define ESP_FIFO	0x02
#define ESP_CMD		0x03
#define  CMD_DMA	0x80
#define  CMD_FLUSH	0x01
#define  CMD_RSTCHIP	0x02
#define  CMD_SELATN	0x42	/* select, identify, send the command */
#define  CMD_TRANS	0x10	/* transfer information */
#define  CMD_ICCS	0x11	/* receive status and message */
#define  CMD_MSGOK	0x12
#define ESP_STAT	0x04	/* read */
#define  STAT_INT	0x80
#define  STAT_GE	0x40
#define  STAT_PE	0x20
#define  STAT_PHASE	0x07
#define ESP_SELID	0x04	/* write */
#define ESP_INTR	0x05	/* read; clears the interrupt */
#define  INTR_SBR	0x80
#define  INTR_ILL	0x40
#define  INTR_DIS	0x20
#define  INTR_BS	0x10
#define  INTR_FC	0x08
#define ESP_TIMEOUT	0x05	/* write */
#define ESP_SYNCTP	0x06	/* write */
#define ESP_FFLAG	0x07	/* read: bytes in the FIFO */
#define ESP_SYNCOFF	0x07	/* write */
#define ESP_CFG1	0x08
#define  CFG1_PARENB	0x10
#define  CFG1_BUSID	0x07	/* we are 7 */
#define ESP_CCF		0x09
#define ESP_CFG2	0x0b
#define ESP_DCTL	0x20	/* NeXT DMA interface control */
#define  DCTL_20MHZ	0x80
#define  DCTL_INTENB	0x20
#define  DCTL_DMAMODE	0x10
#define  DCTL_DMAREAD	0x08
#define  DCTL_FLUSH	0x04

#define PHASE_DATA_IN	1
#define PHASE_STATUS	3
#define PHASE_MSG_IN	7

#define NEXT_SCSI	0x00014000	/* BMAP on '040s */
#define NEXT_FLOPPY_CTRL 0x00014108
#define  FLOPPY_SEL_82077 0x40	/* clear: the SCSI chip has the bus */
#define NEXT_EVENTC	0x0001a000	/* microseconds, 20 bits */
#define NEXT_DMA_SCSI	0x00000010
#define NEXT_DMA_NEXT	0x4000
#define NEXT_DMA_LIMIT	0x4004
#define  DMACSR_ENABLE	0x01000000
#define  DMACSR_COMPLETE 0x08000000
#define  DMACSR_BUSEXC	0x10000000
#define  DMACSR_SETENABLE 0x00010000
#define  DMACSR_READ	0x00040000
#define  DMACSR_INITBUF	0x00200000
#define  DMACSR_INITBUF_TURBO 0x00800000	/* 0x00200000 is something else */

#define SCSI_STATUS_CHECK 0x02
#define SCSI_STATUS_BUSY 0x08

static volatile u8 *esp;
static volatile u32 *eventc;
static volatile u32 *dma_csr;
static u32 dma_initbuf;

static u32 now(void)
{
	return *eventc & 0xfffff;
}

static void delay_us(u32 us)
{
	u32 start = now();

	while (((now() - start) & 0xfffff) < us)
		;
}

/* Wait for the chip to interrupt; 0 or -1 after timeout_us. */
static int esp_wait(u32 timeout_us, u8 *stat, u8 *intr)
{
	u32 waited = 0, last = now();

	while (!(esp[ESP_STAT] & STAT_INT)) {
		u32 t = now();

		waited += (t - last) & 0xfffff;
		last = t;
		if (waited > timeout_us)
			return -1;
	}
	*stat = esp[ESP_STAT];
	*intr = esp[ESP_INTR];
	return 0;
}

void scsi_init(int is_040, int is_turbo)
{
	u32 bmap = is_040 ? NEXT_BMAP_OFFSET : 0;

	dma_initbuf = is_turbo ? DMACSR_INITBUF_TURBO : DMACSR_INITBUF;

	esp = (volatile u8 *)(NEXT_IO + bmap + NEXT_SCSI);
	eventc = (volatile u32 *)(NEXT_IO + bmap + NEXT_EVENTC);
	dma_csr = (volatile u32 *)(NEXT_IO + NEXT_DMA_SCSI);

	*(volatile u8 *)(NEXT_IO + bmap + NEXT_FLOPPY_CTRL) &= ~FLOPPY_SEL_82077;

	*dma_csr = NEXT_DMACSR_RESET;
	esp[ESP_DCTL] = DCTL_20MHZ | DCTL_INTENB;
	esp[ESP_CMD] = CMD_RSTCHIP;
	esp[ESP_CMD] = 0;
	delay_us(500);
	esp[ESP_CFG1] = CFG1_PARENB | CFG1_BUSID;
	esp[ESP_CFG2] = 0;
	esp[ESP_CCF] = 4;		/* 20 MHz */
	esp[ESP_TIMEOUT] = 152;		/* 250 ms selection timeout */
	esp[ESP_SYNCOFF] = 0;		/* asynchronous */
	esp[ESP_SYNCTP] = 5;
	/* any interrupt the ROM left behind */
	(void)esp[ESP_INTR];
}

static int dma_finish(u32 len)
{
	u32 csr = *dma_csr;
	int i;

	/* push out whatever is still in the DMA interface's buffer */
	for (i = 0; i < 16 && (csr & DMACSR_ENABLE) &&
		    !(csr & DMACSR_COMPLETE); i++) {
		esp[ESP_DCTL] = DCTL_20MHZ | DCTL_INTENB | DCTL_DMAMODE |
				DCTL_DMAREAD | DCTL_FLUSH;
		esp[ESP_DCTL] = DCTL_20MHZ | DCTL_INTENB | DCTL_DMAMODE |
				DCTL_DMAREAD;
		csr = *dma_csr;
	}
	esp[ESP_DCTL] = DCTL_20MHZ | DCTL_INTENB;
	*dma_csr = NEXT_DMACSR_RESET | NEXT_DMACSR_CLRCOMPLETE;

	if (esp[ESP_TCM] || esp[ESP_TCL]) {
		printf("scsi: %u of %u bytes not transferred\n",
		       esp[ESP_TCM] << 8 | esp[ESP_TCL], len);
		return -1;
	}
	if (csr & DMACSR_BUSEXC) {
		printf("scsi: DMA bus error\n");
		return -1;
	}
	return 0;
}

/*
 * Run one command on target/lun, reading up to len bytes (at most 64 KB
 * minus 16, a multiple of 16) into buf.  Returns the SCSI status byte, or
 * -1 for no such device, -2 for any other failure.
 */
int scsi_cmd(int target, int lun, const u8 *cdb, int cdb_len,
	     void *buf, u32 len)
{
	u8 stat, intr, status = 0xff;
	u32 data_done = 0;
	int i;

	esp[ESP_CMD] = CMD_FLUSH;
	esp[ESP_SELID] = target;
	esp[ESP_FIFO] = 0x80 | lun;	/* IDENTIFY, no disconnecting */
	for (i = 0; i < cdb_len; i++)
		esp[ESP_FIFO] = cdb[i];
	esp[ESP_CMD] = CMD_SELATN;

	if (esp_wait(500000, &stat, &intr))
		goto timeout;
	if (intr & INTR_DIS)
		return -1;		/* selection timed out */
	if ((intr & (INTR_BS | INTR_FC)) != (INTR_BS | INTR_FC))
		goto fail;

	for (;;) {
		if ((intr & (INTR_SBR | INTR_ILL)) || (stat & (STAT_GE | STAT_PE)))
			goto fail;
		if (intr & INTR_DIS)
			break;

		switch (stat & STAT_PHASE) {
		case PHASE_DATA_IN:
			if (len % 16 || (u32)buf % 16) {
				/* a byte at a time, as long as the target sends */
				if (data_done >= len)
					goto fail;
				esp[ESP_CMD] = CMD_TRANS;
				if (esp_wait(500000, &stat, &intr))
					goto timeout;
				((u8 *)buf)[data_done++] = esp[ESP_FIFO];
				continue;
			}
			if (data_done || !len || len > 0xfff0)
				goto fail;
			*dma_csr = NEXT_DMACSR_RESET | dma_initbuf | DMACSR_READ;
			*(volatile u32 *)((u32)dma_csr + NEXT_DMA_NEXT) = (u32)buf;
			*(volatile u32 *)((u32)dma_csr + NEXT_DMA_LIMIT) = (u32)buf + len;
			*dma_csr = DMACSR_SETENABLE | DMACSR_READ;
			esp[ESP_DCTL] = DCTL_20MHZ | DCTL_INTENB | DCTL_DMAMODE |
					DCTL_DMAREAD;
			esp[ESP_TCL] = len & 0xff;
			esp[ESP_TCM] = len >> 8;
			esp[ESP_CMD] = CMD_DMA | CMD_TRANS;
			if (esp_wait(5000000, &stat, &intr)) {
				dma_finish(len);
				goto timeout;
			}
			if (dma_finish(len))
				goto fail;
			data_done = 1;
			continue;
		case PHASE_STATUS:
			esp[ESP_CMD] = CMD_ICCS;
			if (esp_wait(500000, &stat, &intr))
				goto timeout;
			if (esp[ESP_FFLAG] & 0x1f)
				status = esp[ESP_FIFO];
			if (esp[ESP_FFLAG] & 0x1f)
				(void)esp[ESP_FIFO];	/* COMMAND COMPLETE */
			esp[ESP_CMD] = CMD_MSGOK;
			break;
		case PHASE_MSG_IN:
			esp[ESP_CMD] = CMD_TRANS;
			if (esp_wait(500000, &stat, &intr))
				goto timeout;
			if (esp[ESP_FFLAG] & 0x1f)
				(void)esp[ESP_FIFO];
			esp[ESP_CMD] = CMD_MSGOK;
			break;
		default:
			printf("scsi: unexpected phase %d\n", stat & STAT_PHASE);
			goto fail;
		}
		if (esp_wait(500000, &stat, &intr))
			goto timeout;
	}
	return status;

timeout:
	printf("scsi: target %d timed out\n", target);
	return -2;
fail:
	printf("scsi: target %d failed (status %02x, interrupt %02x)\n",
	       target, stat, intr);
	return -2;
}

/* Like scsi_cmd(), but retries while the target is busy or reports a
 * condition, such as the unit attention after a reset. */
static int scsi_cmd_retry(int target, int lun, const u8 *cdb, int cdb_len,
			  void *buf, u32 len)
{
	int i, status = -2;

	for (i = 0; i < 5; i++) {
		status = scsi_cmd(target, lun, cdb, cdb_len, buf, len);
		if (status != SCSI_STATUS_CHECK && status != SCSI_STATUS_BUSY)
			break;
		delay_us(100000);
	}
	return status;
}

/* 0 if target answers TEST UNIT READY, -1 if there is no such device */
int scsi_probe(int target, int lun)
{
	static const u8 tur[6];

	return scsi_cmd_retry(target, lun, tur, sizeof(tur), NULL, 0) ? -1 : 0;
}

/* Disk block size, or 0 */
u32 scsi_block_size(int target, int lun, void *buf)
{
	static const u8 cap[10] = { 0x25 };	/* READ CAPACITY (10) */
	u8 *b = buf;

	if (scsi_cmd_retry(target, lun, cap, sizeof(cap), buf, 8))
		return 0;
	return b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
}

/* Read count blocks of bsize bytes from block lba into buf. */
int scsi_read(int target, int lun, u32 bsize, u32 lba, u32 count, void *buf)
{
	u32 per_cmd = 0x8000 / bsize;

	while (count) {
		u32 n = count < per_cmd ? count : per_cmd;
		u8 cdb[10] = {
			0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba,	/* READ (10) */
			0, n >> 8, n, 0,
		};

		if (scsi_cmd_retry(target, lun, cdb, sizeof(cdb), buf, n * bsize))
			return -1;
		buf = (u8 *)buf + n * bsize;
		lba += n;
		count -= n;
	}
	return 0;
}
