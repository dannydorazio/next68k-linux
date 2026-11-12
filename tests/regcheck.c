// SPDX-License-Identifier: GPL-2.0
/*
 * Check that system calls and FPSP-emulated FPU instructions preserve the
 * registers user code relies on (a3-a5, d6, d7).  Freestanding: no libc.
 */

typedef unsigned long ulong;

static long sys3(long nr, long a, long b, long c)
{
	register long d0 asm("d0") = nr;
	register long d1 asm("d1") = a;
	register long d2 asm("d2") = b;
	register long d3 asm("d3") = c;

	asm volatile("trap #0" : "+r" (d0) : "r" (d1), "r" (d2), "r" (d3)
		     : "memory", "cc");
	return d0;
}

static void out(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	sys3(4, 1, (long)s, n);
}

static void hex(ulong v)
{
	char b[11] = "0x";
	int i;

	for (i = 0; i < 8; i++)
		b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
	b[10] = 0;
	out(b);
}

/*
 * Run "trap #0" with sentinels in a3-a5/d6/d7 and return them in regs[].
 * A syscall may legitimately change only d0.
 */
static long probe_syscall(long nr, long a, long b, long c, long d,
			  ulong regs[5])
{
	register long d0 asm("d0") = nr;
	register long d1 asm("d1") = a;
	register long d2 asm("d2") = b;
	register long d3 asm("d3") = c;
	register long d4 asm("d4") = d;
	register ulong *r asm("a0") = regs;

	asm volatile(
		"moveml %%a3-%%a5/%%d6-%%d7,%%sp@-\n\t"
		"movel %%a0,%%sp@-\n\t"
		"movel #0x33333333,%%a3\n\t"
		"movel #0x44444444,%%a4\n\t"
		"movel #0x55555555,%%a5\n\t"
		"movel #0x66666666,%%d6\n\t"
		"movel #0x77777777,%%d7\n\t"
		"trap #0\n\t"
		"movel %%sp@+,%%a0\n\t"
		"movel %%a3,%%a0@(0)\n\t"
		"movel %%a4,%%a0@(4)\n\t"
		"movel %%a5,%%a0@(8)\n\t"
		"movel %%d6,%%a0@(12)\n\t"
		"movel %%d7,%%a0@(16)\n\t"
		"moveml %%sp@+,%%a3-%%a5/%%d6-%%d7"
		: "+r" (d0), "+r" (r)
		: "r" (d1), "r" (d2), "r" (d3), "r" (d4)
		: "memory", "cc", "a1");
	return d0;
}

static const ulong sentinel[5] = {
	0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777
};
static const char *const regname[5] = { "a3", "a4", "a5", "d6", "d7" };

static int report(const char *what, ulong regs[5])
{
	int i, bad = 0;

	for (i = 0; i < 5; i++) {
		if (regs[i] == sentinel[i])
			continue;
		out(what);
		out(": ");
		out(regname[i]);
		out(" = ");
		hex(regs[i]);
		out("\n");
		bad = 1;
	}
	return bad;
}

static ulong buf[64];
static ulong allsigs[2] = { ~0UL, ~0UL };
static ulong oldsigs[2];

static int check4(const char *name, long nr, long a, long b, long c, long d)
{
	ulong regs[5];
	long ret = probe_syscall(nr, a, b, c, d, regs);
	int bad = report(name, regs);

	out(bad ? "BAD  " : "ok   ");
	out(name);
	out(" -> ");
	hex(ret);
	out("\n");
	return bad;
}

static int check(const char *name, long nr, long a, long b, long c)
{
	return check4(name, nr, a, b, c, 0);
}

/* FPU instructions the 68040 lacks, emulated by the kernel's FPSP */
static int check_fpsp(void)
{
	static const double vals[] = { 0.5, 1.5, 43.0, 1e6, 4.3e7, 1e10 };
	ulong regs[5];
	unsigned int i;
	int bad = 0;

	for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		register ulong *r asm("a1") = regs;
		double v = vals[i];

		asm volatile(
			"moveml %%a3-%%a5/%%d6-%%d7,%%sp@-\n\t"
			"movel #0x33333333,%%a3\n\t"
			"movel #0x44444444,%%a4\n\t"
			"movel #0x55555555,%%a5\n\t"
			"movel #0x66666666,%%d6\n\t"
			"movel #0x77777777,%%d7\n\t"
			"fmoved %1,%%fp0\n\t"
			"fintrzx %%fp0,%%fp1\n\t"
			"fetoxx %%fp0,%%fp2\n\t"
			"fsinx %%fp0,%%fp3\n\t"
			"flog10x %%fp0,%%fp4\n\t"
			"movel %%a3,%0@(0)\n\t"
			"movel %%a4,%0@(4)\n\t"
			"movel %%a5,%0@(8)\n\t"
			"movel %%d6,%0@(12)\n\t"
			"movel %%d7,%0@(16)\n\t"
			"moveml %%sp@+,%%a3-%%a5/%%d6-%%d7"
			: : "a" (r), "m" (v)
			: "memory", "cc", "fp0", "fp1", "fp2", "fp3", "fp4");
		bad |= report("fpsp", regs);
	}
	out(bad ? "BAD  fpsp\n" : "ok   fpsp\n");
	return bad;
}

void _start(void)
{
	int bad = 0;

	out("regcheck\n");
	bad |= check("getpid", 20, 0, 0, 0);
	bad |= check("time", 13, (long)buf, 0, 0);
	bad |= check("gettimeofday", 78, (long)buf, 0, 0);
	bad |= check("times", 43, (long)buf, 0, 0);
	bad |= check("sysinfo", 116, (long)buf, 0, 0);
	bad |= check("uname", 122, (long)buf, 0, 0);
	bad |= check("clock_gettime", 260, 1, (long)buf, 0);
	bad |= check("clock_gettime64", 403, 1, (long)buf, 0);
	bad |= check("clock_getres", 261, 1, (long)buf, 0);
	bad |= check("getrandom", 352, (long)buf, 16, 1);
	bad |= check("brk", 45, 0, 0, 0);
	/* as glibc blocks and restores all signals around fork() */
	bad |= check4("rt_sigprocmask block", 175, 0, (long)allsigs,
		      (long)oldsigs, 8);
	bad |= check4("rt_sigprocmask restore", 175, 2, (long)oldsigs, 0, 8);
	bad |= check_fpsp();
	out(bad ? "FAILED\n" : "all ok\n");
	sys3(1, bad, 0, 0);
	for (;;)
		;
}
