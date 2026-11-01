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
 *
 * The copy by the SoC's 2D engine (MI_GFX), from tyrquake-miyoo's tqrun.c (gfx_init, gfx_blit,
 * gfx_shutdown): the renderer's frames live in memory the engine reads (MMA, mapped cached:
 * MiyooFb_AllocFrame), and one blit turns a frame 180 degrees (mirrored both ways) and scales it
 * into the page not shown, with no work for the cores. The vendor libraries are loaded at run
 * time; without them, or with MIYOO_GFX=0, the CPU copies as above. Everything the engine got is
 * given back at exit (MiyooFb_Close): without that, Onion's SDL apps started later (Battery
 * Monitor) showed a black page until a reboot. */

#include <dlfcn.h>
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
static unsigned long s_fbPhys;	/* the framebuffer's physical address (fix.smem_start) */
static int s_lineBytes;

/* ---- the SoC's 2D engine (tqrun.c): the types follow the SigmaStar SDK's layouts
 * (mi_gfx_datatype.h, as steward-fu's SDL2 for the Mini) */
typedef unsigned long long mi_phy;
typedef struct { int32_t x, y; uint32_t w, h; } mi_rect;
typedef struct { mi_phy phys; int fmt; uint32_t w, h, stride; } mi_surface;
typedef struct { uint8_t enable; int op, fmt; uint32_t start, end; } mi_colorkey;
typedef struct
{
	mi_rect clip;
	mi_colorkey src_key, dst_key;
	int src_blend, dst_blend, mirror, rotate, blend_flags;
	uint32_t src_const, dst_const;
} mi_opt;
enum { MI_FMT_I8 = 3, MI_FMT_ARGB8888 = 11 };
/* MI_GFX_Palette_t: 256 entries of A, R, G, B bytes, then the first and last index to set */
typedef struct { uint8_t argb[256][4]; uint16_t start, end; } mi_palette;
enum { MI_BLD_ZERO = 0, MI_BLD_ONE = 1 };
enum { MI_MIRROR_NONE = 0, MI_MIRROR_BOTH = 3 };

static struct
{
	int32_t (*sys_init)(void);
	int32_t (*mma_alloc)(uint8_t *heap, uint32_t size, mi_phy *phys);
	int32_t (*mmap)(unsigned long long phys, uint32_t size, void **virt, uint8_t cached);
	int32_t (*flush)(void *virt, uint32_t size);
	int32_t (*open)(void);
	int32_t (*blit)(mi_surface *src, mi_rect *src_rect, mi_surface *dst, mi_rect *dst_rect,
		mi_opt *opt, uint16_t *fence);
	int32_t (*wait)(uint8_t all, uint16_t fence);
	int32_t (*munmap)(void *virt, uint32_t size);
	int32_t (*mma_free)(mi_phy phys);
	int32_t (*close)(void);
	int32_t (*sys_exit)(void);
	int32_t (*set_palette)(int fmt, mi_palette *pal);	/* (MiyooFb_PresentI8) */
} mi;
static int s_gfx;	/* the engine is open */

/* the frames lent to the renderer: MMA buffers while s_gfx, else plain memory */
#define MAX_FRAMES 4
static struct { uint32_t *px; mi_phy phys; uint32_t size; int mma; } s_frames[MAX_FRAMES];
/* the 8-bit frame for MiyooFb_PresentI8 (MMA) */
static struct { uint8_t *px; mi_phy phys; uint32_t size; int w, h; } s_i8;

static void *MiLib(const char *name)
{
	static const char *dirs[] = { "", "/customer/lib/", "/config/lib/", "/lib/", "/usr/lib/" };
	char path[96];
	unsigned i;
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
	{
		void *h;
		snprintf(path, sizeof(path), "%s%s", dirs[i], name);
		h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
		if (h)
			return h;
	}
	return NULL;
}

static void GfxInit(void)
{
	const char *e = getenv("MIYOO_GFX");
	void *hs, *hg;

	if (e && *e == '0')
		return;
	hs = MiLib("libmi_sys.so");
	hg = MiLib("libmi_gfx.so");
	if (!hs || !hg)
	{
		fprintf(stderr, "miyoo: no MI_GFX libraries, the CPU copies the frames\n");
		return;
	}
	*(void **)&mi.sys_init = dlsym(hs, "MI_SYS_Init");
	*(void **)&mi.mma_alloc = dlsym(hs, "MI_SYS_MMA_Alloc");
	*(void **)&mi.mmap = dlsym(hs, "MI_SYS_Mmap");
	*(void **)&mi.flush = dlsym(hs, "MI_SYS_FlushInvCache");
	*(void **)&mi.open = dlsym(hg, "MI_GFX_Open");
	*(void **)&mi.blit = dlsym(hg, "MI_GFX_BitBlit");
	*(void **)&mi.wait = dlsym(hg, "MI_GFX_WaitAllDone");
	*(void **)&mi.munmap = dlsym(hs, "MI_SYS_Munmap");
	*(void **)&mi.mma_free = dlsym(hs, "MI_SYS_MMA_Free");
	*(void **)&mi.close = dlsym(hg, "MI_GFX_Close");
	*(void **)&mi.sys_exit = dlsym(hs, "MI_SYS_Exit");
	*(void **)&mi.set_palette = dlsym(hg, "MI_GFX_SetPalette");	/* (optional) */
	if (!mi.sys_init || !mi.mma_alloc || !mi.mmap || !mi.flush || !mi.open || !mi.blit || !mi.wait
		|| !mi.munmap || !mi.mma_free || !mi.close || !mi.sys_exit)
	{
		fprintf(stderr, "miyoo: MI_GFX functions missing, the CPU copies the frames\n");
		return;
	}
	mi.sys_init();
	mi.open();
	s_gfx = 1;
	fprintf(stderr, "miyoo: frames shown by the 2D engine (MI_GFX)\n");
}

/* a frame of w x h 32-bit pixels for the renderer: in MMA memory when the engine is open */
uint32_t *MiyooFb_AllocFrame(int w, int h)
{
	const uint32_t size = (uint32_t)w * h * 4;
	int i;

	for (i = 0; i < MAX_FRAMES && s_frames[i].px; i++)
		;
	if (i == MAX_FRAMES)
		return NULL;
	if (s_gfx)
	{
		void *virt = NULL;
		if (mi.mma_alloc(NULL, size, &s_frames[i].phys) == 0)
		{
			if (mi.mmap(s_frames[i].phys, size, &virt, 1) == 0)
			{
				memset(virt, 0, size);
				s_frames[i].px = (uint32_t *)virt;
				s_frames[i].size = size;
				s_frames[i].mma = 1;
				return s_frames[i].px;
			}
			mi.mma_free(s_frames[i].phys);
		}
		fprintf(stderr, "miyoo: no MMA memory for a %dx%d frame: the CPU copies it\n", w, h);
	}
	s_frames[i].px = (uint32_t *)calloc(size, 1);
	s_frames[i].size = size;
	s_frames[i].mma = 0;
	return s_frames[i].px;
}

void MiyooFb_FreeFrame(uint32_t *px)
{
	int i;
	for (i = 0; i < MAX_FRAMES; i++)
	{
		if (!px || s_frames[i].px != px)
			continue;
		if (s_frames[i].mma)
		{
			mi.wait(1, 0);
			mi.munmap(px, s_frames[i].size);
			mi.mma_free(s_frames[i].phys);
		}
		else
			free(px);
		memset(&s_frames[i], 0, sizeof(s_frames[i]));
	}
}

/* everything the engine got, back to the system (tqrun's gfx_shutdown) */
static void GfxShutdown(void)
{
	int i;
	if (!s_gfx)
		return;
	mi.wait(1, 0);
	if (s_i8.px)
	{
		mi.munmap(s_i8.px, s_i8.size);
		mi.mma_free(s_i8.phys);
		s_i8.px = NULL;
	}
	for (i = 0; i < MAX_FRAMES; i++)
		if (s_frames[i].mma)
			MiyooFb_FreeFrame(s_frames[i].px);
	mi.close();
	mi.sys_exit();
	s_gfx = 0;
}

/* the frame src (an MMA frame, w x h) into a page, turned 180 degrees, scaled to fill it */
static int GfxBlit(int k, int w, int h, int page)
{
	mi_surface s = { s_frames[k].phys, MI_FMT_ARGB8888, (uint32_t)w, (uint32_t)h, (uint32_t)w * 4 };
	mi_surface d = { (mi_phy)s_fbPhys + (mi_phy)page * s_h * s_lineBytes, MI_FMT_ARGB8888,
		(uint32_t)s_w, (uint32_t)s_h, (uint32_t)s_lineBytes };
	mi_rect sr = { 0, 0, (uint32_t)w, (uint32_t)h }, dr = { 0, 0, (uint32_t)s_w, (uint32_t)s_h };
	mi_opt o;
	uint16_t fence = 0;

	memset(&o, 0, sizeof(o));
	o.clip = dr;
	o.src_blend = MI_BLD_ONE;
	o.dst_blend = MI_BLD_ZERO;
	o.mirror = s_rotate ? MI_MIRROR_BOTH : MI_MIRROR_NONE;
	mi.flush(s_frames[k].px, s_frames[k].size);
	if (mi.blit(&s, &sr, &d, &dr, &o, &fence))
		return 0;
	mi.wait(1, fence);
	return 1;
}

static pthread_t s_panThread;
static pthread_mutex_t s_panMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_panCond = PTHREAD_COND_INITIALIZER;
static int s_panPending = -1;	/* the page to show, -1: none */
/* the 2D engine's fence of a blit into that page still running (MiyooFb_PresentI8): the pan
   thread waits for it before showing the page, so the game thread doesn't; -1: none */
static int s_panFence = -1;

/* Max FPS (the video menu's "max fps", vid_maxfps; MiyooFb_SetMaxFps), as TyrQuake-Miyoo's:
 *   30: each page stays on the panel for two refreshes (29.83 fps): the pan thread waits for
 *       one more refresh (FBIO_WAITFORVSYNC) after showing a page before the next frame may be
 *       presented, so the game sleeps in the present and every frame is shown equally long
 *       (redriver2-miyoo's PanThread: at 30.0 by the clock, a frame was shown for three
 *       refreshes every ~6 s);
 *   60: one refresh (59.67 fps at most);
 *   unlimited: the game runs as fast as it can; a frame that comes while the last one still
 *       waits for the panel is left out (as tqrun -V 0; also MIYOO_VSYNC=0, for timedemos).
 * Only the pan thread waits for the panel: the driver wakes one waiter per refresh, a second
 * thread waiting at the same time sleeps for good. */
static volatile int s_hold = 2;		/* refreshes per frame: 2, 1, or 0 = unlimited */
static int s_holdWorks = 1;			/* FBIO_WAITFORVSYNC answered */

void MiyooFb_SetMaxFps(float fps)
{
	const int hold = fps <= 45.0f ? 2 : fps <= 100.0f ? 1 : 0;
	if (hold != s_hold)
	{
		s_hold = hold;
		fprintf(stderr, "miyoo: max fps %g: %s\n", fps,
			hold == 2 ? "each frame two refreshes" : hold == 1 ? "each frame one refresh" : "unlimited");
	}
}

/* the pans (Stats): the time between pages shown, its largest */
static double s_panIntSum, s_panIntMax, s_panLast;
static int s_panInts;

static double NowMs(void);

/* MIYOO_VSYNC=0 (benchmarks): frames are left out rather than waited for (MiyooFb_Present) */
static void ReadVsync(void)
{
	if (s_vsync < 0)
	{
		const char *e = getenv("MIYOO_VSYNC");
		s_vsync = !(e && *e == '0');
	}
}

static void *PanThread(void *arg)
{
	(void)arg;
	prctl(PR_SET_NAME, "fb_pan");
	for (;;)
	{
		int i;
		double t;

		pthread_mutex_lock(&s_panMutex);
		while (s_panPending < 0)
			pthread_cond_wait(&s_panCond, &s_panMutex);
		const int page = s_panPending;
		const int fence = s_panFence;
		s_panFence = -1;
		pthread_mutex_unlock(&s_panMutex);

		if (fence >= 0)
			mi.wait(1, (uint16_t)fence);

		s_var.yoffset = page * s_h;
		ioctl(s_fd, FBIOPAN_DISPLAY, &s_var);
		t = NowMs();

		/* (shown from this refresh on: held for the others) */
		for (i = 1; i < s_hold && s_holdWorks; i++)
		{
			unsigned int zero = 0;
			if (ioctl(s_fd, FBIO_WAITFORVSYNC, &zero) != 0)
			{
				fprintf(stderr, "miyoo: no FBIO_WAITFORVSYNC: pages not held\n");
				s_holdWorks = 0;
			}
		}

		pthread_mutex_lock(&s_panMutex);
		if (s_panLast > 0)
		{
			const double d = t - s_panLast;
			s_panIntSum += d;
			if (d > s_panIntMax)
				s_panIntMax = d;
			s_panInts++;
		}
		s_panLast = t;
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
	GfxShutdown();
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
	s_fbPhys = fix.smem_start;
	s_lineBytes = fix.line_length;
	if (rot)
		s_rotate = atoi(rot);
	GfxInit();

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
	pthread_mutex_lock(&s_panMutex);
	fprintf(stderr, "miyoo: %.1f fps shown, cpu %.2f ms/frame, copy %.2f ms, %d left out, pages every %.2f ms (longest %.1f)\n",
		frames * 1000.0 / (t - t0), (c - c0) / frames, copySum / frames, s_dropped,
		s_panInts ? s_panIntSum / s_panInts : 0.0, s_panIntMax);
	s_panIntSum = s_panIntMax = 0;
	s_panInts = 0;
	pthread_mutex_unlock(&s_panMutex);
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

	/* unlimited, or MIYOO_VSYNC=0 (benchmarks: timedemo): a frame that comes while the last one
	 * is still waiting for the panel is left out instead of waiting (as tqrun -V 0), so the game
	 * runs at its own speed */
	ReadVsync();
	if ((!s_vsync || !s_hold) && s_pages > 1)
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
	int k;

	for (k = 0; k < MAX_FRAMES && !(s_frames[k].mma && s_frames[k].px == src); k++)
		;
	if (k < MAX_FRAMES && pitch == w && GfxBlit(k, w, h, target))
	{
		/* MIYOO_GFX_CHECK=1: every 50th frame, the page read back and compared with what the
		 * CPU copy writes (panel-sized frames turned 180 degrees). Reading the uncached page is
		 * slow: a test, not for timings. */
		static int check = -1, n;
		if (check < 0)
		{
			const char *e = getenv("MIYOO_GFX_CHECK");
			check = e && *e == '1';
		}
		if (check && w == s_w && h == s_h && s_rotate && ++n % 50 == 0)
		{
			int bad = 0, first = -1;
			for (y = 0; y < s_h; y++)
				for (x = 0; x < s_w; x++)
					if ((page[y * s_stride + x] & 0xFFFFFF) != (src[(h - 1 - y) * pitch + (w - 1 - x)] & 0xFFFFFF))
					{
						if (first < 0)
							first = y * s_w + x;
						bad++;
					}
			fprintf(stderr, "miyoo: gfx check, frame %d: %d pixels differ from the CPU copy%s", n, bad, bad ? "" : "\n");
			if (bad)
				fprintf(stderr, " (first at %d,%d: %08x, CPU %08x)\n", first % s_w, first / s_w,
					page[(first / s_w) * s_stride + first % s_w], src[(h - 1 - first / s_w) * pitch + (w - 1 - first % s_w)]);
		}
	}
	else if (w == s_w && h == s_h)
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

/* ---- 8-bit frames with their palette, converted by the 2D engine (I8 -> ARGB8888 with
 * MI_GFX_SetPalette; tried 2026-11-01, the owner's idea: the renderer's 8 -> 32 bpp pass off
 * the CPU, the same picture). MIYOO_GFX_I8=0: not used (the CPU pass, as before).
 * MIYOO_GFX_CHECK=1: every 50th frame the page is read back and compared with the palette
 * lookup on the CPU. */
static uint32_t s_i8Pal[256];
static int s_i8PalSet;

int MiyooFb_I8Usable(void)
{
	static int usable = -1;
	if (usable < 0)
	{
		const char *e = getenv("MIYOO_GFX_I8");
		usable = s_gfx && mi.set_palette && !(e && *e == '0');
		fprintf(stderr, "miyoo: 8-bit frames converted by %s\n", usable ? "the 2D engine (MI_GFX I8)" : "the CPU");
	}
	return usable && s_gfx;
}

static int I8Buffer(int w, int h)
{
	const uint32_t size = (uint32_t)w * h;
	void *virt = NULL;

	if (s_i8.px && s_i8.w == w && s_i8.h == h)
		return 1;
	if (s_i8.px)
	{
		mi.wait(1, 0);
		mi.munmap(s_i8.px, s_i8.size);
		mi.mma_free(s_i8.phys);
		s_i8.px = NULL;
	}
	if (mi.mma_alloc(NULL, size, &s_i8.phys) != 0)
		return 0;
	if (mi.mmap(s_i8.phys, size, &virt, 1) != 0)
	{
		mi.mma_free(s_i8.phys);
		return 0;
	}
	s_i8.px = virt;
	s_i8.size = size;
	s_i8.w = w;
	s_i8.h = h;
	return 1;
}

/* idx: w x h palette indices; pal: 256 entries 0x00RRGGBB. 0: not shown (the caller converts) */
int MiyooFb_PresentI8(const uint8_t *idx, const uint32_t *pal, int w, int h)
{
	int x, y;

	if (!s_mem || !MiyooFb_I8Usable() || w != s_w || h != s_h || !I8Buffer(w, h))
		return 0;
	ReadVsync();

	if ((!s_vsync || !s_hold) && s_pages > 1)
	{
		int busy;
		pthread_mutex_lock(&s_panMutex);
		busy = s_panPending >= 0;
		pthread_mutex_unlock(&s_panMutex);
		if (busy)
		{
			s_dropped++;
			return 1;
		}
	}
	if (s_pages > 1)
		WaitPan();
	const double copyStart = NowMs();
	const int target = s_pages > 1 ? 1 - s_page : 0;
	uint32_t *page = s_mem + (size_t)target * s_h * s_stride;

	if (!s_i8PalSet || memcmp(s_i8Pal, pal, sizeof(s_i8Pal)))
	{
		mi_palette p;
		int i;
		memset(&p, 0, sizeof(p));
		for (i = 0; i < 256; i++)
		{
			p.argb[i][0] = 0xFF;
			p.argb[i][1] = (uint8_t)(pal[i] >> 16);
			p.argb[i][2] = (uint8_t)(pal[i] >> 8);
			p.argb[i][3] = (uint8_t)pal[i];
		}
		p.start = 0;
		p.end = 255;
		mi.wait(1, 0);	/* (a blit in flight keeps the old palette) */
		if (mi.set_palette(MI_FMT_I8, &p) != 0)
		{
			fprintf(stderr, "miyoo: MI_GFX_SetPalette failed: the CPU converts the frames\n");
			mi.set_palette = NULL;
			return 0;
		}
		memcpy(s_i8Pal, pal, sizeof(s_i8Pal));
		s_i8PalSet = 1;
	}

	static int check = -1, n;
	uint16_t fence = 0;
	if (check < 0)
	{
		const char *e = getenv("MIYOO_GFX_CHECK");
		check = e && *e == '1';
	}
	n++;

	memcpy(s_i8.px, idx, s_i8.size);
	{
		mi_surface s = { s_i8.phys, MI_FMT_I8, (uint32_t)w, (uint32_t)h, (uint32_t)w };
		mi_surface d = { (mi_phy)s_fbPhys + (mi_phy)target * s_h * s_lineBytes, MI_FMT_ARGB8888,
			(uint32_t)s_w, (uint32_t)s_h, (uint32_t)s_lineBytes };
		mi_rect sr = { 0, 0, (uint32_t)w, (uint32_t)h }, dr = { 0, 0, (uint32_t)s_w, (uint32_t)s_h };
		mi_opt o;

		memset(&o, 0, sizeof(o));
		o.clip = dr;
		o.src_blend = MI_BLD_ONE;
		o.dst_blend = MI_BLD_ZERO;
		o.mirror = s_rotate ? MI_MIRROR_BOTH : MI_MIRROR_NONE;
		mi.flush(s_i8.px, s_i8.size);
		if (mi.blit(&s, &sr, &d, &dr, &o, &fence))
		{
			fprintf(stderr, "miyoo: MI_GFX I8 blit failed: the CPU converts the frames\n");
			mi.set_palette = NULL;
			return 0;
		}
		/* the pan thread waits for the blit (s_panFence); the next present waits for the pan
		   (WaitPan), so the buffer and the palette are free again by then. Checks, and a
		   single page, wait here. */
		if (check || s_pages <= 1)
			mi.wait(1, fence);
	}

	{
		if (check && s_rotate && n % 50 == 0)
		{
			int bad = 0, first = -1;
			for (y = 0; y < s_h; y++)
				for (x = 0; x < s_w; x++)
					if ((page[y * s_stride + x] & 0xFFFFFF) != (pal[idx[(h - 1 - y) * w + (w - 1 - x)]] & 0xFFFFFF))
					{
						if (first < 0)
							first = y * s_w + x;
						bad++;
					}
			fprintf(stderr, "miyoo: gfx I8 check, frame %d: %d pixels differ from the CPU lookup%s", n, bad, bad ? "" : "\n");
			if (bad)
				fprintf(stderr, " (first at %d,%d: %08x, CPU %08x)\n", first % s_w, first / s_w,
					page[(first / s_w) * s_stride + first % s_w],
					pal[idx[(h - 1 - first / s_w) * w + (w - 1 - first % s_w)]]);
		}
	}

	if (s_pages > 1)
	{
		pthread_mutex_lock(&s_panMutex);
		s_panPending = target;
		s_panFence = check ? -1 : fence;
		pthread_cond_signal(&s_panCond);
		pthread_mutex_unlock(&s_panMutex);
	}
	Stats(NowMs() - copyStart);
	return 1;
}
