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

/* The Miyoo's buttons are one game controller, a Nintendo-style one (Yamagi then names the
 * buttons A/B/X/Y as printed on the Miyoo, and A confirms in the menus). SDL's buttons are
 * positions: SDL_CONTROLLER_BUTTON_A is the bottom one (the Miyoo's B), B the right one (A),
 * X the left one (Y), Y the top one (X). L2/R2 are the triggers (axes). Only MENU stays a
 * key: Escape. (Before, all the buttons were PC keys: but Yamagi has one Ctrl key for both
 * left and right Ctrl, so B and SELECT were the same key; and the controller gives Yamagi's
 * gamepad menus, button names and its alternative bindings while SELECT is held.) */
enum { PAD_NONE = -1, PAD_TRIG_LEFT = 100, PAD_TRIG_RIGHT };

static int PadButton(int code)
{
	switch (code)
	{
	case KEY_UP:		return SDL_CONTROLLER_BUTTON_DPAD_UP;
	case KEY_DOWN:		return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
	case KEY_LEFT:		return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
	case KEY_RIGHT:		return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
	case KEY_LEFTCTRL:	return SDL_CONTROLLER_BUTTON_A;			/* B, bottom */
	case KEY_SPACE:		return SDL_CONTROLLER_BUTTON_B;			/* A, right */
	case KEY_LEFTALT:	return SDL_CONTROLLER_BUTTON_X;			/* Y, left */
	case KEY_LEFTSHIFT:	return SDL_CONTROLLER_BUTTON_Y;			/* X, top */
	case KEY_E:			return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;	/* L1 */
	case KEY_T:			return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;	/* R1 */
	case KEY_TAB:		return PAD_TRIG_LEFT;					/* L2 */
	case KEY_BACKSPACE:	return PAD_TRIG_RIGHT;					/* R2 */
	case KEY_RIGHTCTRL:	return SDL_CONTROLLER_BUTTON_BACK;		/* SELECT */
	case KEY_ENTER:		return SDL_CONTROLLER_BUTTON_START;		/* START */
	}
	return PAD_NONE;
}

#define PAD_JOYSTICK_ID 0
static Uint8 s_padButtons[SDL_CONTROLLER_BUTTON_MAX];
static Sint16 s_padTriggers[2];

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
			const int down = ev[i].value != 0;
			SDL_Event e;
			int b;

			if (ev[i].type != EV_KEY || ev[i].value == 2)	/* (2: auto-repeat) */
				continue;
			memset(&e, 0, sizeof(e));
			if (ev[i].code == KEY_ESC)	/* MENU */
			{
				e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
				e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
				e.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
				e.key.keysym.sym = SDLK_ESCAPE;
				PushEvent(&e);
				continue;
			}
			b = PadButton(ev[i].code);
			if (b == PAD_NONE)
				continue;
			if (b >= PAD_TRIG_LEFT)
			{
				const int t = b - PAD_TRIG_LEFT;
				s_padTriggers[t] = down ? SDL_JOYSTICK_AXIS_MAX : 0;
				e.type = SDL_CONTROLLERAXISMOTION;
				e.caxis.which = PAD_JOYSTICK_ID;
				e.caxis.axis = t ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT : SDL_CONTROLLER_AXIS_TRIGGERLEFT;
				e.caxis.value = s_padTriggers[t];
			}
			else
			{
				s_padButtons[b] = down;
				e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
				e.cbutton.which = PAD_JOYSTICK_ID;
				e.cbutton.button = b;
				e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
			}
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
	t->px = MiyooFb_AllocFrame(w, h);	/* (MMA memory: shown by the 2D engine) */
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
	MiyooFb_FreeFrame(texture->px);
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

/* Screenshots for bit-exact comparisons of builds (miyoo/tools/bench.sh SHOTS=N,
 * miyoo/tools/shots_compare.py): MIYOO_SHOTS=N saves every Nth frame the game presents (counted
 * from the first, whether or not the panel shows it) as <MIYOO_SHOTS_DIR>/NNNNN.argb, the frame
 * as the renderer made it (w x h, 32-bit 0xAARRGGBB little-endian). A timedemo draws the same
 * frames every run, so a build that draws the same gives the same files. Slow: not for timings. */
static void SaveShot(const SDL_Texture *t)
{
	static int every = -1;
	static unsigned frame;
	static const char *dir;
	unsigned k;
	char path[512];
	FILE *f;

	if (every < 0)
	{
		const char *e = getenv("MIYOO_SHOTS");
		every = e ? atoi(e) : 0;
		dir = getenv("MIYOO_SHOTS_DIR");
		if (!dir)
			dir = ".";
	}
	/* frames N, 2N, ... (frame 0 shows the console with the date and time, different every run) */
	k = frame++;
	if (every <= 0 || k == 0 || k % every)
		return;
	snprintf(path, sizeof(path), "%s/%05u.argb", dir, k);
	f = fopen(path, "wb");
	if (f)
	{
		fwrite(t->px, 4, (size_t)t->w * t->h, f);
		fclose(f);
	}
}

/* Frame times for stutter checks (miyoo/tools/bench.sh FRAMES=1, miyoo/tools/frametimes.py):
 * MIYOO_FRAMES=file writes the time from each present to the next, in microseconds, one per
 * line, kept in memory and written at exit (no file writes while it runs). */
#define FRAMES_MAX 20000
static uint32_t *s_frameUs;
static int s_frameCount;
static const char *s_framesPath;

static void SaveFrames(void)
{
	FILE *f = fopen(s_framesPath, "w");
	int i;

	if (!f)
		return;
	for (i = 0; i < s_frameCount; i++)
		fprintf(f, "%u\n", (unsigned)s_frameUs[i]);
	fclose(f);
}

static void FrameTime(void)
{
	static int init;
	static uint64_t last;
	const uint64_t now = NowNs();

	if (!init)
	{
		init = 1;
		s_framesPath = getenv("MIYOO_FRAMES");
		if (s_framesPath && (s_frameUs = malloc(FRAMES_MAX * sizeof(*s_frameUs))))
			atexit(SaveFrames);
	}
	if (s_frameUs && last && s_frameCount < FRAMES_MAX)
		s_frameUs[s_frameCount++] = (uint32_t)((now - last) / 1000);
	last = now;
}

void MiyooProf_Frame(void);	/* miyoo_prof.c (MIYOO_PROF) */

void SDL_RenderPresent(SDL_Renderer *renderer)
{
	MiyooProf_Frame();
	FrameTime();
	if (!s_shown)
		return;
	SaveShot(s_shown);
	MiyooFb_Present(s_shown->px, s_shown->w, s_shown->h, s_shown->w);
}

/* ---- joysticks, game controllers: one, the Miyoo's buttons (see PadButton); no haptics */

struct _SDL_Joystick { int id; };
struct _SDL_GameController { int id; };
static struct _SDL_Joystick s_padJoystick = { PAD_JOYSTICK_ID };
static struct _SDL_GameController s_padController = { PAD_JOYSTICK_ID };

int SDL_NumJoysticks(void) { return 1; }
SDL_Joystick *SDL_JoystickOpen(int device_index) { return device_index == 0 ? &s_padJoystick : NULL; }
void SDL_JoystickClose(SDL_Joystick *joystick) {}
const char *SDL_JoystickName(SDL_Joystick *joystick) { return "Miyoo Mini Plus"; }
int SDL_JoystickNumAxes(SDL_Joystick *joystick) { return 2; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { return 12; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { return 0; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick) { return joystick ? joystick->id : -1; }

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

SDL_bool SDL_IsGameController(int joystick_index) { return joystick_index == 0 ? SDL_TRUE : SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int joystick_index) { return joystick_index == 0 ? &s_padController : NULL; }
void SDL_GameControllerClose(SDL_GameController *gamecontroller) {}
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller) { return gamecontroller ? &s_padJoystick : NULL; }
char *SDL_GameControllerMapping(SDL_GameController *gamecontroller) { return NULL; }
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *gamecontroller) { return SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO; }
Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller, SDL_GameControllerButton button)
{
	return (unsigned)button < SDL_CONTROLLER_BUTTON_MAX ? s_padButtons[button] : 0;
}
Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller, SDL_GameControllerAxis axis)
{
	return axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? s_padTriggers[0] :
		axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? s_padTriggers[1] : 0;
}
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
