/* The renderer's frame on the Miyoo Mini (Plus) panel, through /dev/fb0.
 *
 * From stuntcarremake-miyoo (miyoo/platform/miyoo_fb.c, itself after redriver2-miyoo's
 * src_rebuild/platform/Miyoo/miyoo_fb.c): the panel is 640x480, 32 bits per pixel, with two
 * pages in the framebuffer (virtual 640x960). Each frame is written into the page not shown and
 * then shown by panning, on a thread of its own (FBIOPAN_DISPLAY waits for the next refresh, so
 * frames are paced by the panel: at most 59.67 fps). The panel is mounted upside down, so the
 * frame is turned 180 degrees (MIYOO_ROTATE=0: not): the frame's pixels in reverse order, written
 * forwards (the mapping is uncached: written once, in order, never read). At exit the page shown
 * at the start is shown again (as tyrquake-miyoo's tqrun).
 *
 * Added here: frames of other sizes than the panel's are scaled to it, nearest pixel.
 * Next step (miyoo/NOTES.md, plan): the copy by the SoC's 2D engine (MI_GFX), as tqrun. */

#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "miyoo_platform.h"

static int s_fd = -1;
static uint32_t *s_mem;
static int s_w, s_h, s_stride;	/* stride in pixels */
static int s_pages;
static int s_page;				/* the page shown */
static int s_startYOffset;		/* shown when the game started: shown again at exit */
static int s_rotate = 1;
static struct fb_var_screeninfo s_var;
static int s_vsync = -1;		/* MIYOO_VSYNC, read at the first frame */
static int s_dropped;			/* frames left out (MIYOO_VSYNC=0) */

static pthread_t s_panThread;
static pthread_mutex_t s_panMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_panCond = PTHREAD_COND_INITIALIZER;
static int s_panPending = -1;	/* the page to show, -1: none */

static void *PanThread(void *arg)
{
	(void)arg;
	prctl(PR_SET_NAME, "fb_pan");
	for (;;)
	{
		pthread_mutex_lock(&s_panMutex);
		while (s_panPending < 0)
			pthread_cond_wait(&s_panCond, &s_panMutex);
		const int page = s_panPending;
		pthread_mutex_unlock(&s_panMutex);

		s_var.yoffset = page * s_h;
		ioctl(s_fd, FBIOPAN_DISPLAY, &s_var);

		pthread_mutex_lock(&s_panMutex);
		s_page = page;
		s_panPending = -1;
		pthread_cond_broadcast(&s_panCond);
		pthread_mutex_unlock(&s_panMutex);
	}
	return NULL;
}

static void WaitPan(void)
{
	pthread_mutex_lock(&s_panMutex);
	while (s_panPending >= 0)
		pthread_cond_wait(&s_panCond, &s_panMutex);
	pthread_mutex_unlock(&s_panMutex);
}

void MiyooFb_Close(void)
{
	if (s_fd < 0 || !s_mem)
		return;
	if (s_pages > 1)
		WaitPan();
	/* a black screen until Onion's menu draws again, on the page shown at the start */
	memset(s_mem, 0, (size_t)s_stride * 4 * s_var.yres_virtual);
	if (s_pages > 1)
	{
		s_var.yoffset = s_startYOffset;
		ioctl(s_fd, FBIOPAN_DISPLAY, &s_var);
	}
	s_mem = NULL;
}

int MiyooFb_Open(void)
{
	struct fb_fix_screeninfo fix;
	const char *rot = getenv("MIYOO_ROTATE");

	if (s_mem)
		return 1;
	s_fd = open("/dev/fb0", O_RDWR);
	if (s_fd < 0)
	{
		perror("miyoo: /dev/fb0");
		return 0;
	}
	if (ioctl(s_fd, FBIOGET_VSCREENINFO, &s_var) < 0 || ioctl(s_fd, FBIOGET_FSCREENINFO, &fix) < 0)
	{
		perror("miyoo: framebuffer info");
		return 0;
	}
	if (s_var.bits_per_pixel != 32)
	{
		/* 32 bpp is what the panel takes */
		s_var.bits_per_pixel = 32;
		ioctl(s_fd, FBIOPUT_VSCREENINFO, &s_var);
		ioctl(s_fd, FBIOGET_VSCREENINFO, &s_var);
		ioctl(s_fd, FBIOGET_FSCREENINFO, &fix);
	}

	s_w = s_var.xres;
	s_h = s_var.yres;
	s_stride = fix.line_length / 4;
	s_pages = s_var.yres_virtual >= s_var.yres * 2 ? 2 : 1;
	s_mem = (uint32_t *)mmap(NULL, (size_t)fix.line_length * s_var.yres_virtual, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
	if (s_mem == MAP_FAILED)
	{
		perror("miyoo: mmap of the framebuffer");
		s_mem = NULL;
		return 0;
	}
	s_startYOffset = s_var.yoffset;
	s_page = s_var.yoffset >= (unsigned)s_h ? 1 : 0;
	if (rot)
		s_rotate = atoi(rot);

	fprintf(stderr, "miyoo: framebuffer %dx%d, %d bpp, %d page(s), rotate %d\n", s_w, s_h, s_var.bits_per_pixel, s_pages, s_rotate);

	if (s_pages > 1)
		pthread_create(&s_panThread, NULL, PanThread, NULL);
	atexit(MiyooFb_Close);
	return 1;
}

/* every 300 frames to stderr: frames per second, the process's CPU time per frame (all its
 * threads) and the time of this copy to the panel (as stuntcarremake-miyoo's Stats) */
static double NowMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double CpuMs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void Stats(double copyMs)
{
	static double t0, c0, copySum;
	static int frames;
	if (!frames && !t0)
	{
		t0 = NowMs();
		c0 = CpuMs();
	}
	copySum += copyMs;
	if (++frames < 300)
		return;
	const double t = NowMs(), c = CpuMs();
	fprintf(stderr, "miyoo: %.1f fps shown, cpu %.2f ms/frame, copy %.2f ms, %d left out\n",
		frames * 1000.0 / (t - t0), (c - c0) / frames, copySum / frames, s_dropped);
	t0 = t;
	c0 = c;
	copySum = 0;
	frames = 0;
	s_dropped = 0;
}

/* a 0xAARRGGBB frame (w x h, pitch in pixels) onto the panel */
void MiyooFb_Present(const uint32_t *src, int w, int h, int pitch)
{
	static int xmap[2048], xmapW = -1;
	int x, y;

	if (!s_mem || !src || w <= 0 || h <= 0)
		return;

	/* MIYOO_VSYNC=0 (benchmarks: timedemo): a frame that comes while the last one is still
	 * waiting for the panel is left out instead of waiting (as tqrun -V 0), so the game runs at
	 * its own speed */
	if (s_vsync < 0)
	{
		const char *e = getenv("MIYOO_VSYNC");
		s_vsync = !(e && *e == '0');
	}
	if (!s_vsync && s_pages > 1)
	{
		int busy;
		pthread_mutex_lock(&s_panMutex);
		busy = s_panPending >= 0;
		pthread_mutex_unlock(&s_panMutex);
		if (busy)
		{
			s_dropped++;
			return;
		}
	}

	/* the last frame's page must be on the panel before this one is drawn into the other */
	if (s_pages > 1)
		WaitPan();
	const double copyStart = NowMs();
	const int target = s_pages > 1 ? 1 - s_page : 0;
	uint32_t *page = s_mem + (size_t)target * s_h * s_stride;

	if (w == s_w && h == s_h)
	{
		for (y = 0; y < s_h; y++)
		{
			uint32_t *d = page + y * s_stride;
			if (s_rotate)
			{
				/* panel row y = frame row h-1-y, right to left */
				const uint32_t *s = src + (h - 1 - y) * pitch + (w - 1);
				for (x = 0; x < w; x++)
					d[x] = s[-x];
			}
			else
				memcpy(d, src + y * pitch, w * 4);
		}
	}
	else
	{
		/* any other size: nearest pixel over the whole panel */
		if (xmapW != w)
		{
			for (x = 0; x < s_w && x < 2048; x++)
				xmap[x] = x * w / s_w;
			xmapW = w;
		}
		for (y = 0; y < s_h; y++)
		{
			const int sy = s_rotate ? s_h - 1 - y : y;
			const uint32_t *s = src + (sy * h / s_h) * pitch;
			uint32_t *d = page + y * s_stride;
			if (s_rotate)
			{
				for (x = 0; x < s_w; x++)
					d[x] = s[xmap[s_w - 1 - x]];
			}
			else
			{
				for (x = 0; x < s_w; x++)
					d[x] = s[xmap[x]];
			}
		}
	}

	if (s_pages > 1)
	{
		pthread_mutex_lock(&s_panMutex);
		s_panPending = target;
		pthread_cond_signal(&s_panCond);
		pthread_mutex_unlock(&s_panMutex);
	}
	Stats(NowMs() - copyStart);
}
