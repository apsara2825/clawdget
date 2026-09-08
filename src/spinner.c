/* clawdget: spinner.c - "thinking..." indicator thread */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "spinner.h"

static int g_allowed = 1;
static pthread_t g_tid;
static volatile int g_run;
static volatile int g_started;
static volatile int g_cleared;

static void *spin_fn(void *arg)
{
	(void)arg;
	static const char frames[] = "|/-\\";
	int i = 0;
	time_t t0 = time(NULL);
	while (g_run) {
		int el = (int)(time(NULL) - t0);
		fprintf(stderr, "\r%c thinking... %ds ", frames[i % 4], el);
		fflush(stderr);
		i++;
		struct timespec ts = { 0, 200 * 1000 * 1000 }; /* 200ms */
		nanosleep(&ts, NULL);
	}
	/* erase the spinner line so streamed content starts clean */
	fprintf(stderr, "\r\033[2K");
	fflush(stderr);
	g_cleared = 1;
	return NULL;
}

void pc_thinking_set_enabled(int enabled)
{
	g_allowed = enabled;
}

void pc_thinking_start(void)
{
	if (g_started || !g_allowed || !isatty(STDERR_FILENO))
		return;
	g_run = 1;
	g_cleared = 0;
	if (pthread_create(&g_tid, NULL, spin_fn, NULL) == 0)
		g_started = 1;
}

void pc_thinking_stop(void)
{
	if (!g_started)
		return;
	g_run = 0;
	pthread_join(g_tid, NULL);
	g_started = 0;
	if (!g_cleared) {
		/* stopped before the thread's last tick: erase line here */
		fprintf(stderr, "\r\033[2K");
		fflush(stderr);
	}
}
