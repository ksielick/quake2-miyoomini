/* A sampling profiler for the whole game on the Miyoo, from redriver2-miyoo
 * (src_rebuild/platform/Miyoo/miyoo_prof.cpp, MIT), in C.
 *
 *   MIYOO_PROF=file,first,count   profile frames first..first+count-1 (frames as presented)
 *
 * Every thread of the process gets a timer of its own on its own CPU time (timer_create on the
 * thread's CPU clock, SIGEV_THREAD_ID): each thread is interrupted after every 1 ms of its own
 * running (rounded up to the kernel's 10 ms tick on the device), and the interrupted
 * instruction's address, the return address (lr) and the thread are kept. (One timer for the
 * whole process, ITIMER_PROF, was biased between threads on the device's kernel.) Threads
 * started after the first frame aren't sampled.
 *
 * When the frames are done the samples go to the file as "pc lr thread count" lines after
 * "# thread <tid> <name>" lines, and then (added here, for the renderer and the game, which are
 * shared libraries) the process's code mappings as "# map <start> <end> <offset> <path>" lines.
 * tools-miyoo's analysis/prof_map.py (G:\Emulacja\tools-miyoo) maps them to functions with
 * the unstripped builds. */

#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* (in librt.so.1 on the Miyoo's glibc 2.28; the toolchain's are libc's GLIBC_2.34) */
__asm__(".symver timer_create,timer_create@GLIBC_2.4");
__asm__(".symver timer_settime,timer_settime@GLIBC_2.4");
__asm__(".symver timer_delete,timer_delete@GLIBC_2.4");

typedef struct { uint32_t pc, lr, tid; } ProfSample;

#define PROF_MAX		(1 << 18)
#define PROF_THREADS	32

static ProfSample *s_samples;
static volatile uint32_t s_count;
static int s_first = -1, s_last, s_frame;
static char s_file[256];
static int s_on;

static timer_t s_timers[PROF_THREADS];
static int s_timerTids[PROF_THREADS];
static int s_numTimers;

static void ProfSignal(int sig, siginfo_t *info, void *ctx)
{
	/* (the threads are sampled at once on both cores: a slot each) */
	const uint32_t n = __sync_fetch_and_add(&s_count, 1);
	(void)sig;
	(void)info;
	if (n < PROF_MAX)
	{
		const ucontext_t *uc = (const ucontext_t *)ctx;
		s_samples[n].pc = uc->uc_mcontext.arm_pc;
		s_samples[n].lr = uc->uc_mcontext.arm_lr;
		s_samples[n].tid = (uint32_t)syscall(SYS_gettid);
	}
}

/* the CPU clock of another thread of this process (the kernel's MAKE_THREAD_CPUCLOCK:
 * CPUCLOCK_SCHED | CPUCLOCK_PERTHREAD_MASK) */
static clockid_t ThreadCpuClock(int tid)
{
	return (clockid_t)((~(unsigned)tid << 3) | 6);
}

static void StartTimers(void)
{
	DIR *dir = opendir("/proc/self/task");
	struct itimerspec its;
	struct dirent *e;

	if (!dir)
	{
		perror("miyoo: profile: /proc/self/task");
		return;
	}
	memset(&its, 0, sizeof(its));
	its.it_interval.tv_nsec = 1000000;
	its.it_value = its.it_interval;

	while ((e = readdir(dir)) != NULL && s_numTimers < PROF_THREADS)
	{
		const int tid = atoi(e->d_name);
		struct sigevent sev;
		timer_t t;

		if (tid <= 0)
			continue;
		memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_THREAD_ID;
		sev.sigev_signo = SIGPROF;
		sev._sigev_un._tid = tid;	/* (glibc's sigev_notify_thread_id) */
		if (timer_create(ThreadCpuClock(tid), &sev, &t) != 0)
		{
			fprintf(stderr, "miyoo: profile: no timer for thread %d: ", tid);
			perror("timer_create");
			continue;
		}
		timer_settime(t, 0, &its, NULL);
		s_timers[s_numTimers] = t;
		s_timerTids[s_numTimers] = tid;
		s_numTimers++;
	}
	closedir(dir);
	fprintf(stderr, "miyoo: profile: sampling %d threads\n", s_numTimers);
}

static void StopTimers(void)
{
	int i;
	for (i = 0; i < s_numTimers; i++)
		timer_delete(s_timers[i]);
}

static int CompareSample(const void *a, const void *b)
{
	const ProfSample *x = (const ProfSample *)a, *y = (const ProfSample *)b;
	if (x->pc != y->pc) return x->pc < y->pc ? -1 : 1;
	if (x->lr != y->lr) return x->lr < y->lr ? -1 : 1;
	if (x->tid != y->tid) return x->tid < y->tid ? -1 : 1;
	return 0;
}

static void Write(void)
{
	uint32_t n = s_count, i;
	FILE *fp, *maps;
	char line[512];
	int t;

	if (n > PROF_MAX)
		n = PROF_MAX;
	qsort(s_samples, n, sizeof(ProfSample), CompareSample);

	fp = fopen(s_file, "w");
	if (!fp)
	{
		perror("miyoo: profile file");
		return;
	}
	fprintf(fp, "# frames %d..%d, %u samples: pc lr thread count\n", s_first, s_last - 1, n);

	/* the threads' names */
	for (t = 0; t < s_numTimers; t++)
	{
		char path[64], name[32] = "?";
		FILE *c;
		snprintf(path, sizeof(path), "/proc/self/task/%d/comm", s_timerTids[t]);
		c = fopen(path, "r");
		if (c)
		{
			if (fgets(name, sizeof(name), c))
				name[strcspn(name, "\n")] = 0;
			fclose(c);
		}
		fprintf(fp, "# thread %d %s\n", s_timerTids[t], name);
	}

	/* the code mappings: where the executable and the libraries are */
	maps = fopen("/proc/self/maps", "r");
	if (maps)
	{
		while (fgets(line, sizeof(line), maps))
		{
			unsigned long start, end, off;
			char perms[8], path[400] = "";
			if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %399s", &start, &end, perms, &off, path) >= 4 && perms[2] == 'x')
				fprintf(fp, "# map %08lx %08lx %08lx %s\n", start, end, off, path[0] ? path : "?");
		}
		fclose(maps);
	}

	for (i = 0; i < n;)
	{
		uint32_t j = i;
		while (j < n && !CompareSample(&s_samples[j], &s_samples[i]))
			j++;
		fprintf(fp, "%08x %08x %u %u\n", s_samples[i].pc, s_samples[i].lr, s_samples[i].tid, j - i);
		i = j;
	}
	fclose(fp);
	fprintf(stderr, "miyoo: profile of frames %d..%d: %u samples in %s\n", s_first, s_last - 1, n, s_file);
}

static void Stop(void)
{
	s_on = 0;
	StopTimers();
	Write();
}

/* the game quit before the last frame: what was sampled */
static void AtExit(void)
{
	if (!s_on)
		return;
	s_last = s_frame;
	Stop();
}

void MiyooProf_Frame(void)
{
	if (s_first < 0)
	{
		const char *env = getenv("MIYOO_PROF");
		const char *c1, *c2;
		int first = 0, count = 0;
		struct sigaction sa;

		s_first = 0x7fffffff;
		if (!env)
			return;
		c1 = strchr(env, ',');
		c2 = c1 ? strchr(c1 + 1, ',') : NULL;
		if (!c1 || !c2 || c1 - env >= (int)sizeof(s_file) || sscanf(c1 + 1, "%d", &first) != 1 ||
			sscanf(c2 + 1, "%d", &count) != 1 || count <= 0)
		{
			fprintf(stderr, "miyoo: MIYOO_PROF=file,first,count\n");
			return;
		}
		memcpy(s_file, env, c1 - env);
		s_file[c1 - env] = 0;
		s_first = first;
		s_last = first + count;
		s_samples = (ProfSample *)malloc(PROF_MAX * sizeof(ProfSample));

		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = ProfSignal;
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sigaction(SIGPROF, &sa, NULL);
	}

	if (s_frame == s_first)
	{
		s_on = 1;
		atexit(AtExit);
		StartTimers();
	}
	else if (s_on && s_frame == s_last)
		Stop();
	s_frame++;
}
