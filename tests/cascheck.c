// SPDX-License-Identifier: GPL-2.0
/*
 * Check that cas.l works when its memory operand faults: on a page that is
 * not present yet, on the read-only zero page, and on a copy-on-write page
 * in a child after fork().  glibc takes its internal locks with cas.l, so
 * a cas.l that reports failure after a fault deadlocks the caller.
 *
 * Also check that a copy of a copy-on-write page has what the parent wrote
 * just before fork(), which may still be in the data cache: the parent's
 * copy, made when it writes right after fork(), and the child's.  And that
 * the parent's stores right after fork() don't reach the child, as they
 * would if the parent's TLB still had writable entries.
 * Freestanding: no libc.
 */

#define PAGE		4096
#define NPAGES		64

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

static void num(long v)
{
	char buf[12], *p = buf + sizeof(buf) - 1;

	*p = 0;
	do
		*--p = '0' + v % 10;
	while (v /= 10);
	out(p);
}

/* glibc's atomic_compare_and_exchange_val_acq: returns the old value */
static int cas(volatile int *p, int old, int new)
{
	int prev = old;

	asm volatile("casl %0,%2,%1" : "+d" (prev), "+m" (*p) : "d" (new)
		     : "cc");
	return prev;
}

static int untouched[NPAGES][PAGE / sizeof(int)] __attribute__((aligned(PAGE)));
static int fresh[NPAGES][PAGE / sizeof(int)] __attribute__((aligned(PAGE)));
static int locks[4][NPAGES][PAGE / sizeof(int)] __attribute__((aligned(PAGE)));
static int zeropage[NPAGES][PAGE / sizeof(int)] __attribute__((aligned(PAGE)));
static int cow[NPAGES][PAGE / sizeof(int)] __attribute__((aligned(PAGE)));

/* cas.l 0 -> 1 on the first word of each page; count the failures */
static int run(const char *name, int (*pages)[PAGE / sizeof(int)])
{
	int bad_ret = 0, bad_mem = 0, i;

	for (i = 0; i < NPAGES; i++) {
		if (cas(&pages[i][0], 0, 1) != 0)
			bad_ret++;
		if (pages[i][0] != 1)
			bad_mem++;
	}
	out(name);
	out(": ");
	num(NPAGES - bad_ret);
	out("/");
	num(NPAGES);
	out(" succeeded");
	if (bad_ret || bad_mem) {
		out(" (");
		num(bad_ret);
		out(" reported failure, ");
		num(bad_mem);
		out(" left memory unchanged)");
	}
	out("\n");
	return bad_ret + bad_mem;
}

static void count(const char *name, int bad, const char *what)
{
	out(name);
	out(": ");
	num(NPAGES - bad);
	out("/");
	num(NPAGES);
	out(what);
}

/* after the copy-on-write fault, the parent's last stores must be there */
static int check_copy(const char *name)
{
	int bad = 0, i;

	for (i = 0; i < NPAGES; i++)
		if (cow[i][2] != 0x1000 + i)
			bad++;
	count(name, bad, " copies have the parent's last stores\n");
	return bad;
}

/* the parent's stores after fork() must not show up here */
static int check_isolated(const char *name)
{
	int bad = 0, i;

	for (i = 0; i < NPAGES; i++)
		if (cow[i][3] != 0x2000 + i)
			bad++;
	count(name, bad, " pages unaffected by the parent's later stores\n");
	return bad;
}

/*
 * Store to pages, fork, and store again at once: the parent's copies must
 * have the stores from before fork().
 */
static int check_parent_copy(void)
{
	int bad = 0, i;
	long pid;

	for (i = 0; i < NPAGES; i++)
		fresh[i][0] = 0x3000 + i;
	pid = sys3(2, 0, 0, 0);			/* fork */
	if (pid == 0) {
		long ts[2] = { 0, 200000000 };

		sys3(162, (long)ts, 0, 0);	/* nanosleep */
		sys3(1, 0, 0, 0);
	}
	for (i = 0; i < NPAGES; i++)
		fresh[i][1] = 1;		/* copy-on-write fault */
	for (i = 0; i < NPAGES; i++)
		if (fresh[i][0] != 0x3000 + i)
			bad++;
	sys3(7, pid, 0, 0);			/* waitpid */
	count("parent, copy-on-write", bad,
	      " copies have the parent's last stores\n");
	return bad;
}

/*
 * What glibc's fork() does with its abort lock: count a reader before
 * fork(), and in the parent take it away again at once, each with cas.l
 * or a plain store.  The second one hits the copy-on-write fault.
 */
static int check_lock(int n, int cas_before, int cas_after)
{
	int (*w)[PAGE / sizeof(int)] = locks[n];
	int bad = 0, first = -1, ret = 8, i;
	long pid;

	for (i = 0; i < NPAGES; i++) {
		if (cas_before)
			cas(&w[i][0], 0, 8);
		else
			w[i][0] = 8;
	}
	pid = sys3(2, 0, 0, 0);			/* fork */
	if (pid == 0) {
		long ts[2] = { 0, 200000000 };

		sys3(162, (long)ts, 0, 0);	/* nanosleep */
		sys3(1, 0, 0, 0);
	}
	for (i = 0; i < NPAGES; i++) {
		int r = 8;

		if (cas_after)
			r = cas(&w[i][0], 8, 0);
		else
			w[i][0] = 0;
		if (r != 8 || w[i][0] != 0) {
			if (first < 0) {
				first = i;
				ret = r;
			}
			bad++;
		}
	}
	sys3(7, pid, 0, 0);			/* waitpid */
	out(cas_before ? "cas.l" : "move");
	out(" before fork, ");
	out(cas_after ? "cas.l" : "move");
	count(" after", bad, " ok");
	if (first >= 0) {
		out(" (first: cas.l returned ");
		num(ret);
		out(", memory ");
		num(w[first][0]);
		out(")");
	}
	out("\n");
	return bad;
}

void _start(void)
{
	volatile int sink = 0;
	int status = 0, bad = 0, i;
	long pid;

	bad += run("not present", untouched);

	for (i = 0; i < NPAGES; i++)
		sink += zeropage[i][0];
	bad += run("zero page", zeropage);

	for (i = 0; i < NPAGES; i++)
		cow[i][1] = 1;
	for (i = 0; i < NPAGES; i++)
		cow[i][2] = 0x1000 + i;
	for (i = 0; i < NPAGES; i++)
		cow[i][3] = 0x2000 + i;
	pid = sys3(2, 0, 0, 0);			/* fork */
	if (pid == 0) {
		long ts[2] = { 0, 200000000 };	/* let the parent run */
		int b;

		sys3(162, (long)ts, 0, 0);	/* nanosleep */
		b = check_isolated("child, copy-on-write");
		b += run("child, copy-on-write", cow);
		b += check_copy("child, copy-on-write");
		sys3(1, b != 0, 0, 0);
	}
	for (i = 0; i < NPAGES; i++)
		cow[i][3] = 0xdead;
	bad += check_parent_copy();
	bad += check_lock(0, 1, 1);
	bad += check_lock(1, 1, 0);
	bad += check_lock(2, 0, 1);
	bad += check_lock(3, 0, 0);
	sys3(7, pid, (long)&status, 0);		/* waitpid */
	bad += (status >> 8) & 0xff;
	bad += run("parent, copy-on-write", cow);

	out(bad ? "cascheck: FAIL\n" : "cascheck: OK\n");
	sys3(1, bad != 0, 0, 0);
}
