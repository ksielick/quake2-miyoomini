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
#include <time.h>

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

qboolean SW_MT_RunBoth(void (*fn)(int part, void *arg), void *arg);

/* ---- the world's spans on both threads (see sw_mt.h) */

SW_TLS int sw_band_v0 = 0, sw_band_v1 = 0x7fffffff;
SW_TLS int sw_mt_replaying;

static sw_drawcmd_t *cmds;
static int numcmds, maxcmds;

/* surface caches to build before the records are drawn: both threads take them until none
   are left (a band's spans may use a cache the other thread built) */
static drawsurf_t *jobs;
static int numjobs, maxjobs;
static volatile int job_next;
extern int r_numallocatedlights;
static SW_TLS int worker_lights;	/* the size of this thread's own blocklights (the worker) */
static qboolean recording;
static int rec_v0, rec_v1;
static int mtpass = 1;	/* surfcache_t.mtpass of the blocks the current records use */

/* the upper part's share of the rows, moved towards equal drawing times; per flush of a
 * frame (a frame's flushes cover different parts of the screen) */
#define MAX_SPLIT_SLOTS 8
static float split[MAX_SPLIT_SLOTS];
static int flush_slot, flush_frame = -1;

typedef struct
{
	int v[3];		/* part 0: [v[0], v[1]), part 1: [v[1], v[2]) */
	double ms[2];
} replayjob_t;

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void
replay(int part, void *arg)
{
	replayjob_t *j = arg;
	const double t0 = now_ms();
	int i;

	sw_band_v0 = j->v[part];
	sw_band_v1 = j->v[part + 1];
	sw_mt_replaying = 1;
	for (i = 0; i < numcmds; i++)
	{
		const sw_drawcmd_t *c = &cmds[i];

		d_sdivzstepu = c->sdivzstepu;
		d_tdivzstepu = c->tdivzstepu;
		d_sdivzstepv = c->sdivzstepv;
		d_tdivzstepv = c->tdivzstepv;
		d_sdivzorigin = c->sdivzorigin;
		d_tdivzorigin = c->tdivzorigin;
		sadjust = c->sadjust;
		tadjust = c->tadjust;
		bbextents = c->bbextents;
		bbextentt = c->bbextentt;
		cacheblock = c->cacheblock;
		cachewidth = c->cachewidth;
		SW_MT_Exec(c);
	}
	sw_mt_replaying = 0;
	sw_band_v0 = 0;
	sw_band_v1 = 0x7fffffff;
	j->ms[part] = now_ms() - t0;
}

static void
build_caches(int part, void *arg)
{
	int i;

	if (part)
	{
		/* the worker's own blocklights, as large as the main thread's */
		if (worker_lights != r_numallocatedlights)
		{
			free(blocklights);
			blocklights = malloc(r_numallocatedlights * sizeof(light_t));
			worker_lights = blocklights ? r_numallocatedlights : 0;
			blocklight_max = blocklights ? blocklights + r_numallocatedlights : NULL;
		}
		if (!blocklights)
		{
			return;	/* (the main thread takes the rest) */
		}
	}

	while ((i = __sync_fetch_and_add(&job_next, 1)) < numjobs)
	{
		R_BuildLightMap(&jobs[i]);
		R_DrawSurface(&jobs[i]);
	}
}

qboolean
SW_MT_DeferBuild(const drawsurf_t *ds)
{
	if (!recording)
	{
		return false;
	}
	if (numjobs == maxjobs)
	{
		int n = maxjobs ? maxjobs * 2 : 256;
		drawsurf_t *p = realloc(jobs, n * sizeof(*jobs));

		if (!p)
		{
			return false;	/* out of memory: built now */
		}
		jobs = p;
		maxjobs = n;
	}
	jobs[numjobs++] = *ds;
	return true;
}

static void
flush(void)
{
	replayjob_t j;
	int slot;
	float f;

	if (numjobs)
	{
		job_next = 0;
		__sync_synchronize();
		if (!SW_MT_RunBoth(build_caches, NULL))
		{
			build_caches(0, NULL);
		}
		numjobs = 0;
	}

	if (!numcmds)
	{
		mtpass++;
		return;
	}

	if (flush_frame != r_framecount)
	{
		flush_frame = r_framecount;
		flush_slot = 0;
	}
	slot = flush_slot < MAX_SPLIT_SLOTS ? flush_slot : MAX_SPLIT_SLOTS - 1;
	flush_slot++;
	f = split[slot] > 0.0f ? split[slot] : 0.5f;

	j.v[0] = rec_v0;
	j.v[1] = rec_v0 + (int)((rec_v1 - rec_v0) * f + 0.5f);
	j.v[2] = rec_v1;
	j.ms[0] = j.ms[1] = 0.0;
	if (!SW_MT_RunBoth(replay, &j))
	{
		replay(0, &j);
		replay(1, &j);
	}
	else if (j.ms[0] > 0.05 && j.ms[1] > 0.05)
	{
		/* per-row costs of the two parts in this flush: the split that would make them equal */
		const float cm = (float)(j.ms[0] / f), cw = (float)(j.ms[1] / (1.0f - f));
		f = 0.75f * f + 0.25f * (cw / (cm + cw));
		f = f < 0.1f ? 0.1f : f > 0.9f ? 0.9f : f;
	}
	split[slot] = f;

	numcmds = 0;
	mtpass++;	/* the blocks used so far may be overwritten now */
}

qboolean
SW_MT_BeginRecord(int v0, int v1)
{
	if (!sw_threads || !sw_threads->value)
	{
		return false;
	}
	recording = true;
	rec_v0 = v0;
	rec_v1 = v1;
	numcmds = 0;
	return true;
}

sw_drawcmd_t *
SW_MT_NewCmd(void)
{
	if (numcmds == maxcmds)
	{
		int n = maxcmds ? maxcmds * 2 : 1024;
		sw_drawcmd_t *p = realloc(cmds, n * sizeof(*cmds));

		if (!p)
		{
			flush();	/* out of memory: draw what there is, then use the room again */
			if (numcmds == maxcmds)
			{
				Com_Error(ERR_FATAL, "%s: out of memory", __func__);
			}
			return &cmds[numcmds++];
		}
		cmds = p;
		maxcmds = n;
	}
	return &cmds[numcmds++];
}

void
SW_MT_EndRecord(void)
{
	flush();
	recording = false;
}

void
SW_MT_CacheUse(surfcache_t *c)
{
	if (recording && c)
	{
		c->mtpass = mtpass;
	}
}

void
SW_MT_CacheOverwrite(surfcache_t *c)
{
	if (recording && c && c->mtpass == mtpass && (numcmds || numjobs))
	{
		flush();
	}
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
