/* The part of SDL2 that Yamagi Quake II's client and ref_soft call, for the Miyoo Mini (Plus)
 * without SDL.
 *
 * After redriver2-miyoo's src_rebuild/platform/Miyoo/miyoo_sdl.cpp (MIT): the game is compiled
 * against the real SDL2 headers and this file implements the functions it calls on the Miyoo's
 * Linux directly (the device's own SDL2 needs the vendor's shared libraries):
 *   - time: POSIX clocks;
 *   - keyboard: the buttons from /dev/input/event0 (gpio-keys, reported as keyboard keys), as
 *     the same keys on a PC keyboard, so Quake II's default binds and menus work: D-pad =
 *     arrows, A = Space (jump), B = Left Ctrl (attack), X = Left Shift, Y = Left Alt,
 *     L1 = E, R1 = T, L2 = Tab, R2 = Backspace, SELECT = Right Ctrl, START = Enter,
 *     MENU = Escape (Quake's menu);
 *   - window: the panel, of the size the game asks for; renderer and texture: a 32-bit frame in
 *     memory, shown by SDL_RenderPresent through miyoo_fb.c;
 *   - game controllers, joysticks, haptics: none; clipboard, icon, mouse: no-ops;
 *   - crashes: the faulting pc and the code addresses on the stack (for addr2line).
 * The sound (SDL_OpenAudio & co.) is in miyoo_audio.c. */

#include <SDL2/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <linux/input.h>

#include "miyoo_platform.h"

/* ---- time */

static uint64_t NowNs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t s_startNs;

Uint32 SDL_GetTicks(void)
{
	if (!s_startNs)
		s_startNs = NowNs();
	return (Uint32)((NowNs() - s_startNs) / 1000000ull);
}

/* ---- misc */

const char *SDL_GetError(void) { return ""; }
void SDL_free(void *mem) { free(mem); }
void *SDL_memset(SDL_OUT_BYTECAP(len) void *dst, int c, size_t len) { return memset(dst, c, len); }
SDL_bool SDL_SetHint(const char *name, const char *value) { return SDL_TRUE; }

void SDL_GetVersion(SDL_version *ver)
{
	SDL_VERSION(ver);
}

char *SDL_GetClipboardText(void) { return calloc(1, 1); }
int SDL_SetClipboardText(const char *text) { return 0; }

/* ---- the Miyoo's buttons */

static int s_inputFd = -1;

#define EVENT_QUEUE 64
static SDL_Event s_events[EVENT_QUEUE];
static int s_eventHead, s_eventCount;

static void PushEvent(const SDL_Event *e)
{
	if (s_eventCount == EVENT_QUEUE)
		return;
	s_events[(s_eventHead + s_eventCount) % EVENT_QUEUE] = *e;
	s_eventCount++;
}

/* a Linux key code of the Miyoo's buttons as the PC key: its scancode and keycode */
static int LinuxKey(int code, SDL_Scancode *sc, SDL_Keycode *kc)
{
	switch (code)
	{
	case KEY_UP:		*sc = SDL_SCANCODE_UP;			*kc = SDLK_UP;			return 1;
	case KEY_DOWN:		*sc = SDL_SCANCODE_DOWN;		*kc = SDLK_DOWN;		return 1;
	case KEY_LEFT:		*sc = SDL_SCANCODE_LEFT;		*kc = SDLK_LEFT;		return 1;
	case KEY_RIGHT:		*sc = SDL_SCANCODE_RIGHT;		*kc = SDLK_RIGHT;		return 1;
	case KEY_SPACE:		*sc = SDL_SCANCODE_SPACE;		*kc = SDLK_SPACE;		return 1;	/* A */
	case KEY_LEFTCTRL:	*sc = SDL_SCANCODE_LCTRL;		*kc = SDLK_LCTRL;		return 1;	/* B */
	case KEY_LEFTSHIFT:	*sc = SDL_SCANCODE_LSHIFT;		*kc = SDLK_LSHIFT;		return 1;	/* X */
	case KEY_LEFTALT:	*sc = SDL_SCANCODE_LALT;		*kc = SDLK_LALT;		return 1;	/* Y */
	case KEY_E:			*sc = SDL_SCANCODE_E;			*kc = SDLK_e;			return 1;	/* L1 */
	case KEY_T:			*sc = SDL_SCANCODE_T;			*kc = SDLK_t;			return 1;	/* R1 */
	case KEY_TAB:		*sc = SDL_SCANCODE_TAB;			*kc = SDLK_TAB;			return 1;	/* L2 */
	case KEY_BACKSPACE:	*sc = SDL_SCANCODE_BACKSPACE;	*kc = SDLK_BACKSPACE;	return 1;	/* R2 */
	case KEY_RIGHTCTRL:	*sc = SDL_SCANCODE_RCTRL;		*kc = SDLK_RCTRL;		return 1;	/* SELECT */
	case KEY_ENTER:		*sc = SDL_SCANCODE_RETURN;		*kc = SDLK_RETURN;		return 1;	/* START */
	case KEY_ESC:		*sc = SDL_SCANCODE_ESCAPE;		*kc = SDLK_ESCAPE;		return 1;	/* MENU */
	}
	return 0;
}

static void ReadButtons(void)
{
	struct input_event ev[16];
	ssize_t n;
	int i;

	if (s_inputFd < 0)
		return;
	while ((n = read(s_inputFd, ev, sizeof(ev))) > 0)
	{
		for (i = 0; i < (int)(n / sizeof(ev[0])); i++)
		{
			SDL_Scancode sc;
			SDL_Keycode kc;
			SDL_Event e;

			if (ev[i].type != EV_KEY || ev[i].value == 2)	/* (2: auto-repeat) */
				continue;
			if (!LinuxKey(ev[i].code, &sc, &kc))
				continue;
			memset(&e, 0, sizeof(e));
			e.type = ev[i].value ? SDL_KEYDOWN : SDL_KEYUP;
			e.key.state = ev[i].value ? SDL_PRESSED : SDL_RELEASED;
			e.key.keysym.scancode = sc;
			e.key.keysym.sym = kc;
			PushEvent(&e);
		}
	}
}

int SDL_PollEvent(SDL_Event *event)
{
	ReadButtons();
	if (!s_eventCount)
		return 0;
	if (event)
		*event = s_events[s_eventHead];
	s_eventHead = (s_eventHead + 1) % EVENT_QUEUE;
	s_eventCount--;
	return 1;
}

void SDL_FlushEvents(Uint32 minType, Uint32 maxType)
{
	ReadButtons();
	s_eventHead = s_eventCount = 0;
}

SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
void SDL_StartTextInput(void) {}

/* ---- crashes (redriver2-miyoo's CrashHandler): map the addresses with addr2line on the
 * unstripped build (build/miyoo/quake2.unstripped) */

extern char __executable_start, etext;

static void CrashHandler(int sig, siginfo_t *info, void *ctx)
{
	const ucontext_t *uc = (const ucontext_t *)ctx;
	const mcontext_t *m = &uc->uc_mcontext;
	const uintptr_t lo = (uintptr_t)&__executable_start, hi = (uintptr_t)&etext;
	const uint32_t *sp = (const uint32_t *)m->arm_sp;
	char line[160];
	int n, i, found = 0;

	n = snprintf(line, sizeof(line), "\n*** signal %d at pc %08lx (lr %08lx, sp %08lx), address %p\n",
		sig, m->arm_pc, m->arm_lr, m->arm_sp, info->si_addr);
	write(2, line, n);

	/* words on the stack that point into the code: likely return addresses */
	write(2, "stack:", 6);
	for (i = 0; i < 2048 && found < 24; i++)
	{
		const uint32_t v = sp[i];
		if (v >= lo && v < hi)
		{
			n = snprintf(line, sizeof(line), " %08x", v);
			write(2, line, n);
			found++;
		}
	}
	write(2, "\n", 1);

	signal(sig, SIG_DFL);
	raise(sig);
}

static void InstallCrashHandler(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = CrashHandler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
}

/* ---- init */

static Uint32 s_inited;

int SDL_Init(Uint32 flags)
{
	static int crashHandler;

	/* (after Yamagi's own signal handlers, set up in main: this one tells more) */
	if (!crashHandler)
	{
		InstallCrashHandler();
		crashHandler = 1;
	}
	SDL_GetTicks();
	if ((flags & SDL_INIT_VIDEO) && s_inputFd < 0)
	{
		s_inputFd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
		if (s_inputFd < 0)
			fprintf(stderr, "miyoo: can't open /dev/input/event0 (%s)\n", strerror(errno));
	}
	s_inited |= flags;
	return 0;
}

Uint32 SDL_WasInit(Uint32 flags)
{
	return s_inited & (flags ? flags : SDL_INIT_EVERYTHING);
}

void SDL_QuitSubSystem(Uint32 flags)
{
	s_inited &= ~flags;
}

void SDL_Quit(void)
{
	if (s_inputFd >= 0)
		close(s_inputFd);
	s_inputFd = -1;
	s_inited = 0;
}

/* ---- displays and the window: the panel; the window has the size the game asks for, its
 * frames are scaled to the panel */

#define PANEL_W 640
#define PANEL_H 480

static struct { int w, h; } s_window;

const char *SDL_GetCurrentVideoDriver(void) { return "miyoo"; }
int SDL_GetNumVideoDisplays(void) { return 1; }

static void FillMode(SDL_DisplayMode *mode, int w, int h)
{
	memset(mode, 0, sizeof(*mode));
	mode->format = SDL_PIXELFORMAT_ARGB8888;
	mode->w = w;
	mode->h = h;
	mode->refresh_rate = 60;
}

int SDL_GetNumDisplayModes(int displayIndex) { return 1; }
int SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode) { FillMode(mode, PANEL_W, PANEL_H); return 0; }
int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode *mode) { FillMode(mode, PANEL_W, PANEL_H); return 0; }

SDL_DisplayMode *SDL_GetClosestDisplayMode(int displayIndex, const SDL_DisplayMode *mode, SDL_DisplayMode *closest)
{
	FillMode(closest, PANEL_W, PANEL_H);
	return closest;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	s_window.w = w > 0 ? w : PANEL_W;
	s_window.h = h > 0 ? h : PANEL_H;
	fprintf(stderr, "miyoo: window %dx%d\n", s_window.w, s_window.h);
	return (SDL_Window *)&s_window;
}

void SDL_DestroyWindow(SDL_Window *window) {}
int SDL_GetWindowDisplayIndex(SDL_Window *window) { return 0; }
void SDL_GetWindowPosition(SDL_Window *window, int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
Uint32 SDL_GetWindowFlags(SDL_Window *window) { return SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS; }

void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
	if (w > 0 && h > 0)
	{
		s_window.w = w;
		s_window.h = h;
	}
}

int SDL_GetWindowDisplayMode(SDL_Window *window, SDL_DisplayMode *mode) { FillMode(mode, s_window.w, s_window.h); return 0; }
int SDL_SetWindowDisplayMode(SDL_Window *window, const SDL_DisplayMode *mode) { return 0; }
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags) { return 0; }
void SDL_SetWindowTitle(SDL_Window *window, const char *title) {}
void SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon) {}
void SDL_SetWindowGrab(SDL_Window *window, SDL_bool grabbed) {}
/* (OpenGL attributes: set for every renderer, used by none here) */
int SDL_GL_SetAttribute(SDL_GLattr attr, int value) { return 0; }
void SDL_GL_ResetAttributes(void) {}
int SDL_ShowCursor(int toggle) { return 0; }
int SDL_SetRelativeMouseMode(SDL_bool enabled) { return 0; }

/* ---- surfaces: only the window icon's (redriver2-miyoo) */

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
	Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
	s->w = width;
	s->h = height;
	s->pitch = pitch;
	s->pixels = pixels;
	s->flags = SDL_PREALLOC;
	s->refcount = 1;
	return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
	if (!s)
		return;
	if (!(s->flags & SDL_PREALLOC))
		free(s->pixels);
	free(s);
}

/* ---- renderer and texture: the frame in memory, shown by SDL_RenderPresent */

struct SDL_Renderer { int dummy; };
struct SDL_Texture { int w, h; uint32_t *px; };

static struct SDL_Renderer s_renderer;
static SDL_Texture *s_shown;	/* the texture of the last SDL_RenderCopy */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	if (!MiyooFb_Open())
		return NULL;
	return &s_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) {}

int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
	if (w) *w = s_window.w;
	if (h) *h = s_window.h;
	return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a) { return 0; }
int SDL_RenderClear(SDL_Renderer *renderer) { s_shown = NULL; return 0; }

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	SDL_Texture *t;

	if (format != SDL_PIXELFORMAT_ARGB8888 || w <= 0 || h <= 0)
	{
		fprintf(stderr, "miyoo: texture format 0x%x %dx%d not supported\n", (unsigned)format, w, h);
		return NULL;
	}
	t = (SDL_Texture *)calloc(1, sizeof(*t));
	t->w = w;
	t->h = h;
	t->px = (uint32_t *)calloc((size_t)w * h, 4);
	if (!t->px)
	{
		free(t);
		return NULL;
	}
	return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
	if (!texture)
		return;
	if (s_shown == texture)
		s_shown = NULL;
	free(texture->px);
	free(texture);
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
	if (!texture)
		return -1;
	*pixels = texture->px + (rect ? rect->y * texture->w + rect->x : 0);
	*pitch = texture->w * 4;
	return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture) {}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	s_shown = texture;
	return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
	if (s_shown)
		MiyooFb_Present(s_shown->px, s_shown->w, s_shown->h, s_shown->w);
}

/* ---- joysticks, game controllers, haptics: none (the buttons are the keyboard) */

int SDL_NumJoysticks(void) { return 0; }
SDL_Joystick *SDL_JoystickOpen(int device_index) { return NULL; }
void SDL_JoystickClose(SDL_Joystick *joystick) {}
const char *SDL_JoystickName(SDL_Joystick *joystick) { return NULL; }
int SDL_JoystickNumAxes(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { return 0; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick) { return -1; }

SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int device_index)
{
	SDL_JoystickGUID g;
	memset(&g, 0, sizeof(g));
	return g;
}

void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *pszGUID, int cbGUID)
{
	if (pszGUID && cbGUID > 0)
		pszGUID[0] = 0;
}

SDL_bool SDL_IsGameController(int joystick_index) { return SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int joystick_index) { return NULL; }
void SDL_GameControllerClose(SDL_GameController *gamecontroller) {}
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller) { return NULL; }
char *SDL_GameControllerMapping(SDL_GameController *gamecontroller) { return NULL; }
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *gamecontroller) { return SDL_CONTROLLER_TYPE_UNKNOWN; }
SDL_bool SDL_GameControllerHasLED(SDL_GameController *gamecontroller) { return SDL_FALSE; }
SDL_bool SDL_GameControllerHasRumble(SDL_GameController *gamecontroller) { return SDL_FALSE; }
SDL_bool SDL_GameControllerHasSensor(SDL_GameController *gamecontroller, SDL_SensorType type) { return SDL_FALSE; }
float SDL_GameControllerGetSensorDataRate(SDL_GameController *gamecontroller, SDL_SensorType type) { return 0.0f; }
int SDL_GameControllerSetSensorEnabled(SDL_GameController *gamecontroller, SDL_SensorType type, SDL_bool enabled) { return -1; }
int SDL_GameControllerSetLED(SDL_GameController *gamecontroller, Uint8 red, Uint8 green, Uint8 blue) { return -1; }
int SDL_GameControllerRumble(SDL_GameController *gamecontroller, Uint16 low, Uint16 high, Uint32 ms) { return -1; }

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) { return NULL; }
int SDL_GameControllerAddMappingsFromRW(SDL_RWops *rw, int freerw) { return 0; }

SDL_Haptic *SDL_HapticOpenFromJoystick(SDL_Joystick *joystick) { return NULL; }
SDL_Haptic *SDL_HapticOpenFromMouse(void) { return NULL; }
void SDL_HapticClose(SDL_Haptic *haptic) {}
unsigned int SDL_HapticQuery(SDL_Haptic *haptic) { return 0; }
int SDL_HapticNumAxes(SDL_Haptic *haptic) { return 0; }
int SDL_HapticNumEffects(SDL_Haptic *haptic) { return 0; }
int SDL_HapticNumEffectsPlaying(SDL_Haptic *haptic) { return 0; }
int SDL_HapticNewEffect(SDL_Haptic *haptic, SDL_HapticEffect *effect) { return -1; }
int SDL_HapticRunEffect(SDL_Haptic *haptic, int effect, Uint32 iterations) { return -1; }
void SDL_HapticDestroyEffect(SDL_Haptic *haptic, int effect) {}
