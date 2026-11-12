// SPDX-License-Identifier: GPL-2.0
/*
 * Mount an NFS export with text mount options, which busybox's mount does
 * not use.  Freestanding: no libc.
 *
 * usage: nfsmount server:/path mountpoint options
 */

static long sys5(long nr, long a, long b, long c, long d, long e)
{
	register long d0 asm("d0") = nr;
	register long d1 asm("d1") = a;
	register long d2 asm("d2") = b;
	register long d3 asm("d3") = c;
	register long d4 asm("d4") = d;
	register long d5 asm("d5") = e;

	asm volatile("trap #0" : "+r" (d0)
		     : "r" (d1), "r" (d2), "r" (d3), "r" (d4), "r" (d5)
		     : "memory", "cc");
	return d0;
}

static void out(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	sys5(4, 2, (long)s, n, 0, 0);
}

void cstart(long *sp);
void cstart(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	long ret;

	if (argc != 4) {
		out("usage: nfsmount server:/path mountpoint options\n");
		sys5(1, 2, 0, 0, 0, 0);
	}
	ret = sys5(21, (long)argv[1], (long)argv[2], (long)"nfs", 0,
		   (long)argv[3]);
	if (ret < 0) {
		char msg[] = "nfsmount: mount failed, errno 000\n";

		ret = -ret;
		msg[30] = '0' + ret / 100;
		msg[31] = '0' + ret / 10 % 10;
		msg[32] = '0' + ret % 10;
		out(msg);
		sys5(1, 1, 0, 0, 0, 0);
	}
	sys5(1, 0, 0, 0, 0, 0);
}

asm(".globl _start\n_start:\n\tmovel %sp,%a0\n\tmovel %a0,%sp@-\n\tjsr cstart\n");
