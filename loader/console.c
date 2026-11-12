// SPDX-License-Identifier: GPL-2.0
/*
 * Loader console: everything goes to the ROM console (usually the screen)
 * and to SCC channel A, the first serial port.
 */

#include <stdarg.h>

#include "loader.h"

#define SCC_BASE_030		0x02018000
#define SCC_BASE_040		0x02118000	/* BMAP alias */
#define SCC_CTRL_A		1
#define SCC_DATA_A		3
#define SCC_RR0_TX_EMPTY	0x04

static void *rom_mg;
static volatile u8 *scc;

/* Channel A, 9600 8N1, baud rate generator from the 3.6864 MHz PCLK */
static const u8 scc_init_table[] = {
	4, 0x44,	/* x16 clock, 1 stop bit, no parity */
	3, 0xc0,	/* receiver: 8 bits per character */
	5, 0xe2,	/* transmitter: 8 bits per character, DTR, RTS */
	10, 0x00,	/* NRZ */
	11, 0x50,	/* rx and tx clock from the baud rate generator */
	12, 10,		/* time constant for 9600 baud */
	13, 0,
	14, 0x03,	/* baud rate generator on, fed from PCLK */
	3, 0xc1,	/* enable receiver */
	5, 0xea,	/* enable transmitter */
};

static void scc_write_reg(u8 reg, u8 val)
{
	scc[SCC_CTRL_A] = reg;
	scc[SCC_CTRL_A] = val;
}

void console_init(void *mg, int is_040)
{
	unsigned int i;
	volatile int delay;

	rom_mg = mg;
	scc = (volatile u8 *)(is_040 ? SCC_BASE_040 : SCC_BASE_030);

	(void)scc[SCC_CTRL_A];		/* reset the register pointer */
	scc_write_reg(9, 0x80);		/* reset channel A */
	for (delay = 0; delay < 100; delay++)
		;
	for (i = 0; i < sizeof(scc_init_table); i += 2)
		scc_write_reg(scc_init_table[i], scc_init_table[i + 1]);
}

static void scc_putc(char c)
{
	while (!(scc[SCC_CTRL_A] & SCC_RR0_TX_EMPTY))
		;
	scc[SCC_DATA_A] = c;
}

void console_flush(void)
{
	while (!(scc[SCC_CTRL_A] & SCC_RR0_TX_EMPTY))
		;
}

static void putc(char c)
{
	typedef int (*putc_fn)(int);

	if (c == '\n')
		putc('\r');
	scc_putc(c);
	MON(putc_fn, rom_mg, MG_putc)(c);
}

static void puts(const char *s)
{
	while (*s)
		putc(*s++);
}

static void put_num(unsigned int v, unsigned int base, int width, char pad)
{
	char buf[12];
	int n = 0;

	do {
		buf[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	while (width-- > n)
		putc(pad);
	while (n)
		putc(buf[--n]);
}

/* Supports %c %s %d %u %x with an optional 0 flag and width. */
void printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		char pad = ' ';
		int width = 0;

		if (*fmt != '%') {
			putc(*fmt);
			continue;
		}
		fmt++;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + *fmt++ - '0';

		switch (*fmt) {
		case 'c':
			putc(va_arg(ap, int));
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);

			puts(s ? s : "(null)");
			break;
		}
		case 'd': {
			int v = va_arg(ap, int);

			if (v < 0) {
				putc('-');
				v = -v;
			}
			put_num(v, 10, width, pad);
			break;
		}
		case 'u':
			put_num(va_arg(ap, unsigned int), 10, width, pad);
			break;
		case 'x':
			put_num(va_arg(ap, unsigned int), 16, width, pad);
			break;
		case '%':
			putc('%');
			break;
		default:
			putc('%');
			putc(*fmt);
			break;
		}
	}
	va_end(ap);
}

void *memcpy(void *dst, const void *src, size_t n)
{
	u8 *d = dst;
	const u8 *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	u8 *d = dst;
	const u8 *s = src;

	if (d <= s)
		return memcpy(dst, src, n);
	d += n;
	s += n;
	while (n--)
		*--d = *--s;
	return dst;
}

void *memset(void *s, int c, size_t n)
{
	u8 *p = s;

	while (n--)
		*p++ = c;
	return s;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}
