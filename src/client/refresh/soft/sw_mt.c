/*
 * sw_mt.c -- a second thread for the software renderer (Miyoo Mini port).
 *
 * From tyrquake-miyoo's common/d_mt.c (its worker and D_MT_RunBoth): one worker thread,
 * pinned to the second core, runs part 1 of a job while the calling thread runs part 0.
 * The worker spins a little between jobs (the next comes soon), then sleeps on a condition
 * variable, so an idle worker costs no CPU. Every spin wait yields after ~2000 rounds:
 * otherwise a preempted thread can wait 100-500 ms for the core (tyrquake-miyoo, miyoo.md).
 *
 * sw_threads 0 (or no worker): SW_MT_RunBoth returns false and the caller does the whole job.
 */

#ifdef __linux__
#define _GNU_SOURCE	/* cpu_set_t, sched_setaffinity */
#endif

#include <pthread.h>
#include <sched.h>

#include "header/local.h"
#include "header/sw_mt.h"

cvar_t *sw_threads;

static pthread_t worker;
static qboolean worker_running;
static pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static volatile int job_seq, done_seq;
static volatile qboolean worker_quit;
static void (*task_fn)(int part, void *arg);
static void *task_arg;

static void *
worker_main(void *unused)
{
	int seen = 0;

#ifdef __linux__
	{
		/* the calling thread mostly runs on core 0 */
		cpu_set_t set;

		CPU_ZERO(&set);
		CPU_SET(1, &set);
		sched_setaffinity(0, sizeof(set), &set);
	}
#endif

	for (;;)
	{
		int spins = 0;

		while (job_seq == seen && !worker_quit)
		{
			if (++spins < 2000)
			{
				continue;
			}
			pthread_mutex_lock(&job_lock);
			while (job_seq == seen && !worker_quit)
			{
				pthread_cond_wait(&job_cond, &job_lock);
			}
			pthread_mutex_unlock(&job_lock);
		}
		if (worker_quit)
		{
			break;
		}
		seen = job_seq;
		__sync_synchronize();

		task_fn(1, task_arg);

		__sync_synchronize();
		done_seq = seen;
	}
	return NULL;
}

static qboolean
start_worker(void)
{
	if (worker_running)
	{
		return true;
	}
	worker_quit = false;
	job_seq = done_seq = 0;
	if (pthread_create(&worker, NULL, worker_main, NULL) != 0)
	{
		R_Printf(PRINT_ALL, "sw_threads: can't start the drawing thread\n");
		ri.Cvar_SetValue("sw_threads", 0);
		return false;
	}
	worker_running = true;
	return true;
}

void
SW_MT_Init(void)
{
#ifdef MIYOO_MINI
	sw_threads = ri.Cvar_Get("sw_threads", "1", CVAR_ARCHIVE);
#else
	sw_threads = ri.Cvar_Get("sw_threads", "0", CVAR_ARCHIVE);
#endif
}

void
SW_MT_Shutdown(void)
{
	if (!worker_running)
	{
		return;
	}
	pthread_mutex_lock(&job_lock);
	worker_quit = true;
	pthread_cond_broadcast(&job_cond);
	pthread_mutex_unlock(&job_lock);
	pthread_join(worker, NULL);
	worker_running = false;
}

qboolean
SW_MT_RunBoth(void (*fn)(int part, void *arg), void *arg)
{
	int spins = 0;

	if (!sw_threads || !sw_threads->value)
	{
		SW_MT_Shutdown();
		return false;
	}
	if (!start_worker())
	{
		return false;
	}

	task_fn = fn;
	task_arg = arg;
	__sync_synchronize();
	pthread_mutex_lock(&job_lock);
	job_seq++;
	pthread_cond_signal(&job_cond);
	pthread_mutex_unlock(&job_lock);

	fn(0, arg);

	while (done_seq != job_seq)
	{
		if (++spins > 2000)
		{
			sched_yield();
		}
	}
	__sync_synchronize();
	return true;
}
