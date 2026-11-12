// SPDX-License-Identifier: GPL-2.0
/*
 * Check glibc's rwlocks and the thread ID glibc caches in its thread
 * descriptor (which rwlocks compare against), before and after fork().
 * The TID offset from the thread pointer is from Debian's libc6 2.43-7
 * for m68k.  Links against glibc; run it in the Debian root.
 */

#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define TID_OFFSET	(-29784)	/* THREAD_SELF->tid - tp */

static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

static void show(const char *what)
{
	unsigned int *w = (unsigned int *)&lock;
	/* what glibc's __m68k_read_tp() does */
	char *tp = (char *)syscall(__NR_get_thread_area);

	printf("%-16s readers %08x cur_writer %d; tp %p, cached tid %d, "
	       "gettid %ld\n", what, w[0], w[7], tp,
	       *(int *)(tp + TID_OFFSET), syscall(SYS_gettid));
}

int main(void)
{
	pid_t pid;

	setvbuf(stdout, NULL, _IONBF, 0);
	show("initial");
	printf("rdlock: %d\n", pthread_rwlock_rdlock(&lock));
	show("read-locked");
	printf("unlock: %d\n", pthread_rwlock_unlock(&lock));
	show("unlocked");

	pid = fork();
	if (pid == 0) {
		show("child");
		_exit(0);
	}
	waitpid(pid, NULL, 0);
	show("parent");
	return 0;
}
