// SPDX-License-Identifier: GPL-2.0
/*
 * glibc (2.41 and later) holds an internal lock for reading across fork()
 * and resets it in the child; sigaction(SIGABRT) takes it for writing.
 * Check that a child can always call sigaction(SIGABRT): after fork(), and
 * after a raw clone() as systemd's safe_fork() sometimes does.  A child
 * that hasn't exited after 10 seconds counts as stuck and is left running
 * for inspection.  Links against glibc, unlike the other tests; run it in
 * the Debian root.
 *
 * With -l, also print the lock's words before fork(), in the child and in
 * the parent; the lock's offset in libc is from Debian's libc6 2.43-7 for
 * m68k.
 *
 * usage: forkabrt [-l] [ITERATIONS]
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ABORT_LOCK	0x192f88	/* __abort_lock, libc6 2.43-7 */

static volatile unsigned int *abort_lock;

static void show_lock(const char *who)
{
	if (abort_lock)
		printf("%-7s __abort_lock: %08x %08x %08x %08x ... %08x\n", who,
		       abort_lock[0], abort_lock[1], abort_lock[2],
		       abort_lock[3], abort_lock[7]);
}

static void find_lock(void)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[256];
	unsigned long base;

	while (f && fgets(line, sizeof(line), f))
		if (strstr(line, "/libc.so.6") &&
		    sscanf(line, "%lx-", &base) == 1) {
			abort_lock = (unsigned int *)(base + ABORT_LOCK);
			break;
		}
	if (f)
		fclose(f);
}

static void child(void)
{
	struct sigaction sa = { .sa_handler = SIG_DFL };

	sigaction(SIGABRT, &sa, NULL);
	_exit(0);
}

static int run(const char *how, int raw, int n)
{
	int stuck = 0, i;

	for (i = 0; i < n; i++) {
		time_t start = time(NULL);
		pid_t pid;
		int status;

		show_lock("before");
		pid = raw ? syscall(SYS_clone, SIGCHLD, 0) : fork();
		if (pid < 0) {
			perror(how);
			return 1;
		}
		if (pid == 0) {
			show_lock("child");
			child();
		}
		show_lock("parent");
		while (waitpid(pid, &status, WNOHANG) == 0) {
			if (time(NULL) - start > 10) {
				printf("%s: iteration %d: child %d stuck\n",
				       how, i, pid);
				stuck++;
				break;
			}
			usleep(10000);
		}
		if (stuck)
			break;
	}
	printf("%s: %d/%d children exited\n", how, i, n);
	return stuck;
}

int main(int argc, char **argv)
{
	int n, bad;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 1 && !strcmp(argv[1], "-l")) {
		find_lock();
		argc--, argv++;
	}
	n = argc > 1 ? atoi(argv[1]) : 200;
	bad = run("fork", 0, n);
	bad += run("raw clone", 1, n);
	printf(bad ? "forkabrt: FAIL\n" : "forkabrt: OK\n");
	return bad != 0;
}
