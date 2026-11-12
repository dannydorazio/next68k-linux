// SPDX-License-Identifier: GPL-2.0
/*
 * Attach to a process with ptrace and print its user registers, the word
 * that d1 points to (the futex address if it is in futex()) and the top of
 * its user stack, then detach.  For finding out where a process is stuck
 * without strace or gdb.  Freestanding: no libc.
 *
 * usage: ptdump PID [STACKWORDS [HEXADDR]]
 */

#define NR_exit		1
#define NR_write	4
#define NR_ptrace	26
#define NR_wait4	114

#define PTRACE_PEEKDATA	2
#define PTRACE_GETREGS	12
#define PTRACE_ATTACH	16
#define PTRACE_DETACH	17

#define __WALL		0x40000000

typedef unsigned long ulong;

static long sys4(long nr, long a, long b, long c, long d)
{
	register long d0 asm("d0") = nr;
	register long d1 asm("d1") = a;
	register long d2 asm("d2") = b;
	register long d3 asm("d3") = c;
	register long d4 asm("d4") = d;

	asm volatile("trap #0" : "+r" (d0) : "r" (d1), "r" (d2), "r" (d3),
		     "r" (d4) : "memory", "cc");
	return d0;
}

static void out(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	sys4(NR_write, 1, (long)s, n, 0);
}

static void hex(ulong v)
{
	char buf[9];
	int i;

	for (i = 7; i >= 0; i--, v >>= 4)
		buf[i] = "0123456789abcdef"[v & 15];
	buf[8] = 0;
	out(buf);
}

static long atol(const char *s)
{
	long v = 0;

	while (*s >= '0' && *s <= '9')
		v = v * 10 + *s++ - '0';
	return v;
}

static ulong atox(const char *s)
{
	ulong v = 0;

	for (;; s++) {
		if (*s >= '0' && *s <= '9')
			v = v * 16 + *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			v = v * 16 + *s - 'a' + 10;
		else
			return v;
	}
}

static int peek(long pid, ulong addr, ulong *val)
{
	return sys4(NR_ptrace, PTRACE_PEEKDATA, pid, addr, (long)val) == 0;
}

static void dump(long pid, ulong addr, long nwords)
{
	ulong val;
	long i;

	for (i = 0; i < nwords; i++) {
		if (!peek(pid, addr + 4 * i, &val))
			break;
		out(i % 8 ? " " : "\n");
		hex(val);
	}
	out("\n");
}

/* order of PTRACE_GETREGS on m68k */
static const char *const names[19] = {
	"d1", "d2", "d3", "d4", "d5", "d6", "d7",
	"a0", "a1", "a2", "a3", "a4", "a5", "a6",
	"d0", "usp", "orig_d0", "sr", "pc",
};

__attribute__((used)) static void ptdump_main(int argc, char **argv)
{
	ulong regs[19], val;
	long pid, nwords = 64, i;
	int status;

	if (argc < 2) {
		out("usage: ptdump PID [STACKWORDS [HEXADDR]]\n");
		sys4(NR_exit, 2, 0, 0, 0);
	}
	pid = atol(argv[1]);
	if (argc > 2)
		nwords = atol(argv[2]);

	if (sys4(NR_ptrace, PTRACE_ATTACH, pid, 0, 0) < 0) {
		out("ptdump: PTRACE_ATTACH failed\n");
		sys4(NR_exit, 1, 0, 0, 0);
	}
	sys4(NR_wait4, pid, (long)&status, __WALL, 0);
	if (sys4(NR_ptrace, PTRACE_GETREGS, pid, 0, (long)regs) < 0) {
		out("ptdump: PTRACE_GETREGS failed\n");
		sys4(NR_ptrace, PTRACE_DETACH, pid, 0, 0);
		sys4(NR_exit, 1, 0, 0, 0);
	}

	for (i = 0; i < 19; i++) {
		out(names[i]);
		out("=");
		hex(regs[i]);
		out((i % 4 == 3 || i == 18) ? "\n" : " ");
	}
	if (peek(pid, regs[0], &val)) {
		out("*d1=");
		hex(val);
		out("\n");
	}
	out("stack:");
	dump(pid, regs[15], nwords);
	if (argc > 3) {
		ulong addr = atox(argv[3]);

		out("at ");
		hex(addr);
		out(":");
		dump(pid, addr, 16);
	}

	sys4(NR_ptrace, PTRACE_DETACH, pid, 0, 0);
	sys4(NR_exit, 0, 0, 0, 0);
}

/* the kernel starts us with argc, argv[] on the stack */
asm(".globl _start\n"
    "_start:\n"
    "	movel %sp@,%d0\n"
    "	pea %sp@(4)\n"
    "	movel %d0,%sp@-\n"
    "	jsr ptdump_main\n");
