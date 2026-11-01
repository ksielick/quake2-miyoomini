# Yamagi Quake II on the Miyoo Mini Plus (OnionOS): notes

Goal: a native Quake II for the Miyoo Mini Plus. It should run in Onion's Ports list, with no
GPU (software renderer), at 640x480 with sound and the device's buttons. The player brings
their own `baseq2/` (the demo's `pak0.pak` works for testing). It should be installable later
by Port Manager (ksielick/portmanager-miyoo): a release zip that mirrors the SD card
(`Roms/PORTS/Games/Quake2/`, a shortcut in `Roms/PORTS/Shortcuts/FPS/`), no game data inside.

## Layout

- `G:\Emulacja\quake2-miyoomini`: clone of github.com/yquake2/yquake2.
  - Remote `upstream` = yquake2, branch `master`.
  - Remote `origin` = github.com/ksielick/quake2-miyoomini (private). It is not a GitHub
    fork, because GitHub doesn't allow private forks of public repos.
  - Our work is on branch `miyoo`. The base is upstream master 9dbf6992 (2026-10-15,
    "Merge pull request #1384"), 8.70 era.
- Commit as `ksielick <153522713+ksielick@users.noreply.github.com>` (set in the repo config).
- Tools shared with the other Miyoo projects:
  - Since 2026-11-01 they are in `G:\Emulacja\tools-miyoo` (private repo ksielick/tools-miyoo):
    - `device/` holds `mssh`, `mscp` and `mshot.sh`.
    - `analysis/` holds `shots_compare.py`, `frametimes.py` and `prof_map.py`; they were in
      `miyoo/tools/`, which the entries below still name.
    - `prof_map.py` now takes the binaries themselves: `py -3 G:/Emulacja/tools-miyoo/analysis/prof_map.py prof.txt build/miyoo/quake2.unstripped build/miyoo/ref_soft.so.unstripped build/miyoo/baseq2/game.so.unstripped`.
  - The toolchain is Arm GNU 13.3 (`arm-none-linux-gnueabihf`) in
    `G:\Emulacja\redriver2-miyoo\deps\`.

## Why not Apaczer/quake2-miyoo (tested on the device 2026-10-19)

- github.com/Apaczer/quake2-miyoo is a fork of gameblabla/quake2-rs97 (Yamagi 7.41pre,
  SDL 1.2). It targets **MiyooCFW**, the older Miyoo/BittBoy/PocketGo with F1C100s, ARMv5,
  320x240 and gmenu2x. It does not target the Miyoo Mini.
- Its release `m3` (2026-10-03) is `quake2.ipk`, an opkg package (`ar` archive:
  `debian-binary`, `control.tar.gz`, `data.tar.gz`). It contains:
  - `/mnt/games/quake2/quake2`: static, ARMv5TEJ, soft-float EABI5, SDL 1.2 fbcon, OSS/ALSA,
    OpenAL;
  - `baseq2/pak0.pak`: the id demo, 50 MB;
  - a gmenu2x link with `-datadir /roms/QUAKE_2`.
- On the Mini Plus (started the way Onion starts things, through `/tmp/cmd_to_run.sh`) the log
  showed:
  - ALSA fails (`/usr/share/alsa/alsa.conf` missing; Onion has no ALSA);
  - "setting mode 0: 320 240", "ref_soft version: SOFT 0.01";
  - then **Segmentation fault**, the same with `SDL_VIDEODRIVER=dummy`. So it's not (only) the
    framebuffer.
- Not usable. The test folder was removed from the card.

## Yamagi Quake II today (upstream master, checked 2026-10-19)

- CMake or Makefile. **SDL3 by default** (`SDL3_SUPPORT ON`), SDL2 optional. Renderers: GL1,
  GL3, GLES3, GLES1 (off) and **soft** (`src/client/refresh/soft`). OpenAL and cURL optional.
- **Everything modular is loaded with dlopen**:
  - renderers as `ref_<name>.so` next to the binary (`src/client/vid/vid.c`);
  - game code as `baseq2/game.so` (`src/backends/unix/system.c`).
  - A static binary can't do that, so either link dynamically against the device's glibc, or
    link the soft renderer and the game into the client (no dlopen).
- The soft renderer presents through SDL: `SDL_CreateTexture`, `SDL_UpdateTexture`,
  `SDL_RenderPresent` in `sw_main.c`. On the Mini that needs an SDL with a renderer for the
  framebuffer, or a small replacement layer.

## The Miyoo Mini Plus (verified in the REDRIVER2, dethrace and TyrQuake work)

- **CPU**: SigmaStar SSD202D, 2x Cortex-A7, NEON-VFPv4, stock 1200 MHz (Onion's
  `/mnt/SDCARD/.tmp_update/bin/cpuclock` can overclock). Flags:
  `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`. No GPU for games.
- **RAM**: ~103 MB, ~5 MB free with Onion running (plus swap). A 90 MB file can't stay in the
  page cache.
- **glibc** on the device is 2.28: a dynamic binary must not need newer symbols. TyrQuake's
  `miyoo/build/build_core.sh` checks this, and its `glibc_compat.h`/`.c` keep exp/log/pow and
  stat at 2.28. A static binary avoids the problem, but then there's no dlopen.
- **Display**: `/dev/fb0` 640x480, 32 bpp BGRA.
  - The virtual size is 640x960 (two pages, flip with FBIOPAN_DISPLAY). Under MainUI it's
    640x1440 (three pages).
  - **The panel is mounted upside down**: turn the picture 180°.
  - FBIOPAN_DISPLAY blocks until the next refresh (16.76 ms, panel 59.67 Hz; the fb_var timing
    fields claim 75 Hz, which is wrong), so call it from its own thread.
  - **Never wait for vsync from two threads at once**: both hang in D state.
- **Buttons**: `/dev/input/event0` (gpio_keys) as keyboard keys:
  - A=SPACE, B=LEFTCTRL, X=LEFTSHIFT, Y=LEFTALT;
  - L1=E, R1=T, L2=TAB, R2=BACKSPACE;
  - SELECT=RIGHTCTRL, START=ENTER, MENU=ESC, POWER=POWER.
- **Sound**: Onion's `audioserver` owns the hardware. **Never stop it.**
  - Dynamic programs get OSS `/dev/dsp` through `LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so`
    (Onion's launch scripts do this).
  - A static binary has to talk to audioserver itself, as REDRIVER2 does: PCM into the FIFO
    `/tmp/audio_fifo_server`, and the OSS ioctls via `/tmp/audio_fifo_ioctl_req` (24 B) and
    `/tmp/audio_fifo_ioctl_res` (28 B). Format S16 stereo 48 kHz. Details are in REDRIVER2's
    `miyoo/NOTES.md`, "Sound".
  - There is no ALSA.
- **SDL on the device**: the device's own SDL2 (`mmiyoo`, used by the Fallout CE ports) needs
  vendor shared libraries and the old glibc. REDRIVER2 and Port Manager avoid SDL with their
  own small fbdev + evdev layer.
- **Timers**: no high-resolution timers. Any nanosleep under ~10 ms takes ~10 ms, and itimer
  ticks are 10 ms. Pace frames by the clock and sleep only while more than 10 ms remain.
- **Onion's Ports list**:
  - game in `Roms/PORTS/Games/<dir>/`;
  - shortcut `Roms/PORTS/Shortcuts/<Category>/<Name>.port`;
  - picture `Roms/PORTS/Imgs/<Name>.png`.
  - A shortcut whose `GameDataFile` isn't found is hidden (`.notfound`) until the player adds
    the data.
  - Nothing goes in `App/` (the owner's rule).
- **Testing over SSH**:
  - Start a program the way Onion does: write the command to `/tmp/cmd_to_run.sh`, then
    `killall MainUI`. runtime.sh runs it and brings MainUI back afterwards.
  - Started next to a running or SIGSTOPped MainUI, a program that switches VTs can hang in
    `VT_WAITACTIVE`.
  - **Keep `/mnt/SDCARD/Roms` out of that command's text**, e.g. `sh /tmp/run.sh`, which then
    cd's to the game. Onion treats such a command as an app and leaves SSH and Samba running.
    For anything it sees as a game it kills dropbear and smbd until the game quits, unless
    Tweaks > Network > "Keep services alive" is on.
- **Busybox gotchas**:
  - `timeout -t`; no `time`; no `date +%N` (time with `/proc/uptime`).
  - Core dumps go to `|/etc/core.sh`.
  - Binary output through `mssh` gets corrupted: fetch files over SMB (`\\192.168.0.246\Roms`).
- The idle "Sleep timer" is off on the owner's device. It runs on battery: check the battery
  (`/customer/app/axp_test`) before and after long tests.

## Status

- **2026-11-01, step 1 (skeleton) built, not yet run on the device.**
  - `miyoo/build/build_miyoo.sh` (Git Bash, no make) builds `build/miyoo/quake2`,
    `ref_soft.so` and `baseq2/game.so`, dynamic, all at glibc <= 2.28 (the script checks).
    Unstripped copies (`*.unstripped`) are kept next to them for addr2line.
  - From tqrun: `miyoo/build/start.S` (entry point, init/fini in
    `miyoo/platform/miyoo_start.c`), `glibc_compat.h/.c` (added: `fmod` and `dl*` pins).
    `-nostartfiles` also drops crtbegin's `__dso_handle`, which `atexit` needs:
    `miyoo_start.c` defines it.
  - `miyoo/platform/`: the SDL2 subset (~95 functions, `miyoo_sdl.c`, after RR2's), the panel
    (`miyoo_fb.c`, after SCR's: CPU copy turned 180°, pan thread, pan back at exit, other frame
    sizes scaled nearest), the sound (`miyoo_audio.c`: the SDL callback driven by a thread that
    writes 256-frame blocks to audioserver's FIFO, RR2's transport; 48 kHz only).
  - Yamagi includes `<SDL2/SDL.h>`: the script copies the SDL2 headers to
    `build/miyoo/include/SDL2/`.
  - Windows traps met: the Windows compiler needs `G:/...` paths (`pwd -W`), `-pipe` fails, and
    `xargs -L 1` joins a line ending in a blank with the next one.
  - Card files in `miyoo/card/` (mirrors the SD card): `Roms/PORTS/Shortcuts/FPS/Quake II.port`
    (GameDataFile = the program, so it always shows and `launch.sh` tells if the data is
    missing, as TyrQuake's) and `Roms/PORTS/Games/Quake2/launch.sh` (lower-case names, HOME =
    the game folder, performance governor, `+set vid_renderer soft`, 640x480 custom mode,
    `s_khz 48`).
- **2026-11-01, step 1 done: it runs on the device.** Installed by hand over SMB to
  `Roms/PORTS/Games/Quake2/` (the three binaries, `launch.sh`, `baseq2/pak0-2.pak`) and
  `Roms/PORTS/Shortcuts/FPS/Quake II.port`; the owner confirmed it works from Onion's menu.
  - Yamagi refuses to run as root, and everything runs as root on Onion: the check in
    `src/backends/unix/main.c` is left out with `-DMIYOO_MINI` (set by the build script).
  - `ref_soft.so` needs `src/backends/unix/shared/hunk.c` (the Makefile adds it per OS); the
    build script now checks that every symbol the modules need outside glibc is exported by
    `quake2` (the device's loader stops with "symbol lookup error" otherwise).
  - 100 s of the attract demos (SSH smoke test under a watchdog): no errors, exit 0,
    audioserver back after; sound opened (audioserver, 48 kHz); picture right way up, colours
    right. RSS 62 MB, peak 71 MB (tight: ~3 MB free with MainUI out; swap). 27-41 fps shown,
    ~26 ms CPU per frame, the CPU copy to the panel 1.6-1.9 ms.
- **Timedemo baseline (2026-11-01 09:10, 1200 MHz, charging at 15 %, 72-77 °C):**
  `miyoo/tools/bench.sh` (on the device, started through `/tmp/cmd_to_run.sh`), 640x480,
  stock Yamagi soft renderer on one core, CPU present:
  - demo1: 688 frames, 20.6 s: **33.4 fps**
  - demo2: 617 frames, 21.0 s: **29.4 fps**
  - The bench uses `MIYOO_VSYNC=0` (`miyoo_fb.c`: a frame that would wait for the panel is left
    out, as tqrun `-V 0`) and `+set nextdemo quit +demomap demoN.dm2` (the server runs
    `nextdemo` when the demo ends).
- **2026-11-01, step 3: reference screenshot set.** `MIYOO_SHOTS=N` (`miyoo_sdl.c` SaveShot)
  saves every Nth presented frame (N, 2N, ...) as `NNNNN.argb`, the renderer's 32-bit frame;
  `bench.sh` with `SHOTS=N SET=name` puts them in `<game>/shots/<name>/<demo>/` on the card;
  `miyoo/tools/shots_compare.py` (TyrQuake's, plus `.argb`) compares two sets.
  - Two runs of the same build: identical except frame 0, which shows the console with the
    date and time (so shots start at frame N). Every other frame is bit-identical: the
    timedemo is deterministic.
  - The reference set: `build/shots/ref/` (SHOTS=50: demo1 13 shots, demo2 12), from the build
    of commit 7d2f229a (CPU present). Fetch new sets over SMB from
    `\\192.168.0.246\Roms\PORTS\Games\Quake2\shots\<name>`.
- **2026-11-01, step 4: present through MI_GFX** (tqrun's code in `miyoo_fb.c`). The texture
  (`SDL_CreateTexture`) lives in MMA memory (`MiyooFb_AllocFrame`); `SDL_RenderPresent` flushes
  the cache and blits it, mirrored both ways, into the page not shown; CPU copy as fallback
  (`MIYOO_GFX=0`, or no MI libraries). MI cleanup at exit as tqrun.
  - Checked with `MIYOO_GFX_CHECK=1` (every 50th frame the page is read back and compared with
    the CPU copy): 0 pixels differ in all 13 checks of demo1.
  - Timedemo (09:19, 1200 MHz, charging, 74-78 °C): demo1 **33.7 fps** (was 33.4), demo2
    **29.9 fps** (was 29.4). The present took ~1.3 ms of the game thread instead of ~1.65
    (cache flush + waiting for the blit). Small, as expected: ~30 ms of the frame is the
    renderer itself.
  - Not done (small gain, ~1.3 ms at most): the blit on the pan thread. It needs two textures,
    and Yamagi may copy only changed rows into the texture (`sw_partialrefresh`).
- **2026-11-01, step 5 begins: profile, then R_ApplyLight.** Sampling profiler from RR2
  (`miyoo/platform/miyoo_prof.c`, `MIYOO_PROF=file,first,count`, bench.sh `PROF=50,550`; added
  "# map" lines for the code in shared libraries; `timer_*` pinned to GLIBC_2.4, `librt.so.1`
  linked) and `miyoo/tools/prof_map.py` (maps addresses in quake2, ref_soft.so and game.so).
  - demo1, frames 50-599, all on the game thread (~29 ms a frame): D_DrawSpansPow2 22 %,
    D_CacheSurface 13 %, R_ApplyLight 12 %, R_PolysetDrawSpans8_Opaque 12 %, RE_EndFrame 10 %
    (the 8 bpp -> 32 bpp conversion), D_DrawZSpans 6 %, R_ProcessLeftEdge 3 %,
    VID_DamageZBuffer 3 %.
  - R_ApplyLight (Yamagi's colour light; `sw_colorlight` is 0 by default) is called per texel
    of the surface cache and per pixel of the models, from another file, so never inlined
    (Yamagi's own comment: "TODO: -22% fps lost"). Moved to `header/local.h` as static inline,
    unchanged, and `R_DrawSurfaceBlock_Light` (sw_surf.c) got the original's loop when all
    three light components are exactly equal at both ends of the row (then R_ApplyLight would
    give `vid_colormap[pix + (light & LIGHTMASK)]` for every texel).
  - demo1 **38.0 fps** (was 33.7), demo2 **33.7 fps** (was 29.9): +13 %. Shots: 25/25
    identical to the reference.
- **2026-11-01: max fps and show fps in the video menu, as TyrQuake-Miyoo** (owner's idea:
  30 fps = half the panel's refresh).
  - Video options (`src/client/menu/videomenu.c`, `#ifdef MIYOO_MINI`): "max fps" 30 / 60 /
    unlimited (`vid_maxfps` 30 / 60 / 300) in place of "vertical sync", and "show fps" no /
    yes / detailed (`cl_showfps` 0 / 1 / 2, Yamagi's own counter, top right). Both apply at
    once. `vid_maxfps` defaults to 30 on the Miyoo (`src/common/frame.c`).
  - `miyoo_fb.c`: at 30 the pan thread waits one more refresh (FBIO_WAITFORVSYNC, only that
    thread) after showing a page, at 60 none; unlimited leaves out frames the panel can't take
    yet (as `MIYOO_VSYNC=0`). `frame.c` passes `vid_maxfps` on every frame
    (`MiyooFb_SetMaxFps`) and, at 30 and 60, lets the client render as soon as it may
    (rfps = refresh x 1.2, as Yamagi does with vsync): its own clock at 30.0 would drift
    against the panel's 29.83 (RR2 saw a 3-refresh frame every ~6 s that way).
  - Measured (attract demos, 100 s): 29.8 fps, pages every 33.52 ms (= 2 x 16.76), longest
    33.6 ms in play (longer only at demo changes), ~24 ms CPU per frame (was ~26-29
    uncapped).
  - The card's `.yq2/baseq2/config.cfg` from the first runs had `vid_maxfps "300"` (the old
    default): set to 30 by hand. New installs get 30.
  - Note: Yamagi's main loop spins between frames by default (`busywait 1`, 5 µs steps);
    with the panel pacing the frames the game thread waits in the present instead, so it
    spins little. `busywait 0` would sleep, and sleeps last >= 10 ms on this kernel.
- **2026-11-01: controls as TyrQuake-Miyoo, auto aim, menus trimmed** (owner's choices after
  a comparison with the 1999 PlayStation port).
  - Comparison: the PS1 port's digital pad layout was D-pad move/turn, L1/R1 look, L2/R2
    strafe, X (bottom) fire, triangle/circle next/previous weapon; its analogue layouts were
    criticised ("tank controls"). No auto aim is known in it. PC Quake II has none either
    (`aimfix` only removes the drift from the gun's offset); Quake 1 has `sv_aim` 0.93,
    which TyrQuake keeps.
  - The buttons are now **one SDL game controller** (`miyoo_sdl.c` PadButton), not PC keys:
    Yamagi treats left and right Ctrl as one key, so B and SELECT had been the same key. As a
    Nintendo Switch type controller Yamagi names the buttons A/B/X/Y as printed on the Miyoo,
    A confirms and B goes back in the menus, and Options shows "customize gamepad". L2/R2 are
    the trigger axes, MENU stays Escape (Onion integration later).
  - Layout (`miyoo/card/.../baseq2/yq2.cfg`, run after default.cfg at start and on "reset
    defaults"): TyrQuake's RetroArch "classic" layout one to one: D-pad forward/back/turn,
    B jump, A next weapon, Y fire, L1/R1 strafe, L2/R2 look up/down, START menu; but X =
    crouch (TyrQuake: hold to look with the D-pad). SELECT held (Yamagi's
    `+joyaltselector`): D-pad left/right previous/next item, up show inventory, down use item;
    A previous weapon, B help computer, X walk/run (`toggle cl_run`); fire, strafe and look
    work as usual. `cl_run 1` by default (TyrQuake's SELECT toggles walking).
  - **Auto aim** (`src/game/player/weapon.c` P_AutoAim, called from P_ProjectSource): Quake
    1's PF_aim with sv_aim 0.93: when the line of sight hits no monster, the living monster in
    clear sight whose centre is nearest the crosshair (within ~22°) gets the shot's pitch; the
    heading stays. Not in deathmatch, never at players. Cvar `autoaim` (1 on the Miyoo), in
    Options as "auto aim". `aimfix 1` on the Miyoo. Demos replay without the game code, so
    timedemos and shots don't change. TODO: rogue and xatrix have their own game.so.
  - Menus (`#ifdef MIYOO_MINI`): Video keeps brightness, field of view, ui scale (now at
    once), max fps, show fps, texture filter; hidden: renderer, video mode, fullscreen, aniso
    filtering, multisampling, apply, reset (nothing left needs apply). Options keeps the
    volumes and music, always run, auto aim, crosshair and its colour, customize gamepad,
    reset defaults; hidden: sound backend (no OpenAL), mouse speed, invert mouse, lookstrafe,
    free look, customize controls (keyboard), go to console (no keyboard).
  - The card's config.cfg (from the first runs) began with `unbindall` and keyboard binds,
    which would have undone yq2.cfg's: its bind lines removed, and its old `aimfix 0` and
    `cl_run 0` set to 1 (backup in `backup/2026-11-01-controls/`, not in git). Checked on
    the device: "1 joysticks were found ... 'Miyoo Mini Plus' ... Enabled as Game Controller",
    and the game saved the pad binds to config.cfg.
  - Quake splits commands at `;` in .cfg comments too ("Unknown command" in the log):
    no semicolons in yq2.cfg's comments.
- **Texture filter (Video -> texture filter, `sw_texture_filtering`) on/off, 2026-11-01
  09:50** (`miyoo/tools/bench_ab.sh`, 1200 MHz, 74-79 °C, charging at 50 %):

  | | demo1 | demo2 |
  |---|---|---|
  | off | 38.8 fps | 33.6 fps |
  | on | 38.1 fps (-1.8 %) | 33.4 fps (-0.6 %) |

  Cheap: the menu item stays, default off (Yamagi's default).
- Game data for tests on the PC: `G:\Emulacja\Quake2\` (outside the repo, never commit it):
  the owner's GOG "Quake II - Quad Damage" 2.0.0.3, extracted with innoextract
  (`G:\Emulacja\dethrace-miyoo\deps\innoextract`, `-I baseq2 -I music -I rogue -I xatrix
  -I quake2_manual`; `-I app/baseq2` matches nothing, include names are path components).
  574 files, 900 MB: `baseq2/` (pak0-2, video, maps, players), `music/Track02-12.ogg`,
  `rogue/`, `xatrix/`, `docs/quake2_manual/`; the Windows `gamex86.dll` files removed. The
  installer was deleted afterwards (owner's request).

## Plan (2026-11-01): reuse what the other ports already have

Rule: don't write a Miyoo piece from scratch when another port has a tested one. Copy it (with
a comment naming where it came from), adapt it, and note here what changed. When the same piece
ends up in three projects, that's the time to move it into a shared library, not before.
The shared device facts and tools are in the `miyoo` skill and `G:\Emulacja\miyoo.md`.

Paths below: RR2 = `G:\Emulacja\redriver2-miyoo\repos\REDRIVER2`, SCR =
`G:\Emulacja\stuntcarremake-miyoo\repos\stuntcarremake`, TQ =
`G:\Emulacja\tyrquake-miyoo\repos\tyrquake`.

### Where each piece comes from

1. **The SDL2 layer: RR2 `src_rebuild/platform/Miyoo/miyoo_sdl.cpp`.**
   - Yamagi calls ~80 SDL2 functions, mostly the same ones REDRIVER2 implements (~70): time,
     threads, keyboard events, window, texture; game controllers and haptics as no-ops.
   - The method: compile against the real SDL2 headers (`G:\Emulacja\redriver2-miyoo\deps\SDL2-2.30.9`)
     and implement only what the link errors list.
   - SCR `miyoo/platform/miyoo_sdl.cpp` + `input_map.cpp` shows how it was adapted for another
     game.
2. **The picture: RR2's pan thread + TQ's MI_GFX.**
   - Yamagi's soft renderer draws 8 bpp and converts it itself into an ARGB8888 texture
     (`src/client/refresh/soft/sw_main.c`: `SDL_LockTexture`, `SDL_RenderPresent`).
   - Our `SDL_LockTexture` hands out a buffer in MMA memory, and `SDL_RenderPresent` is one
     MI_GFX blit (ARGB8888 into the fb0 page, mirrored both ways = 180°), with no CPU work.
     The code: TQ `miyoo/standalone/tqrun.c`, `gfx_init`/`gfx_blit`/`gfx_shutdown`, including
     the cleanup at exit (without it, Onion's SDL apps started later show black).
   - CPU fallback: SCR `miyoo/platform/miyoo_fb.c` `SoftGL_Present` (the same 32-bit format,
     rotated, written forwards into the uncached mapping).
   - Pacing: 30 fps = each frame held for two refreshes (`FBIO_WAITFORVSYNC` on the pan thread
     only, with a fallback): RR2 `miyoo_fb.c` `PanThread` or TQ `present_proc`. Pan back to the
     start page at exit: TQ `video_shutdown`.
3. **Sound: RR2's audioserver transport, or TQ's `/dev/dsp` ring.**
   - Yamagi mixes itself and pulls PCM through an `SDL_OpenAudio` callback (`s_khz 48` gives
     48 kHz): no mixer needed, only `SDL_OpenAudio`/`SDL_PauseAudio`/`SDL_LockAudio`.
   - Static binary: a thread that calls the callback and writes blocks to the FIFO, as RR2
     `al_miyoo.cpp` (`OpenServer`/`ServerIoctl`/`CloseServer`, and `MixThread`'s write loop with
     EINTR/EAGAIN and the 200 ms drop).
   - Dynamic binary: TQ `tqrun.c` `audio_init` + `ring_proc` (`/dev/dsp` through libpadsp,
     drift kept in check by leaving out single frames).
   - Before a release: the look-ahead limiter, fades around loads and the low-pass for 11/22 kHz
     samples (miyoo.md section 6; the code is in TQ's core).
4. **Buttons.**
   - Key codes: any of the ports (all agree, see above).
   - MENU = Onion's Game Switcher, POWER = resume save before sleep, the Game Switcher picture
     and recent list: TQ `tqrun.c` `key_event`, `switcher_prepare`, `write_romscreen`,
     `onion_recent_rompath`.
   - Game actions: Quake II binds them itself; ship default binds in a `default.cfg`.
5. **Static or dynamic: decide first. Recommended: dynamic, TQ's recipe.**
   - Yamagi loads `ref_soft.so` and `game.so` with dlopen, and MI_GFX needs dlopen of the
     vendor libraries; a static binary has no working dlopen.
   - TQ's recipe for the device's glibc 2.28 with the 13.3 toolchain: its own `_start`
     (`miyoo/standalone/start.S`), `.symver` pins for `dl*` and `pthread_*`, TQ's
     `glibc_compat` for exp/log/pow and stat, a symbol check as in `miyoo/build/build_core.sh`;
     no `_GNU_SOURCE`.
   - That gives MI_GFX, `/dev/dsp` through libpadsp and Yamagi's module loading unchanged.
   - Static (as RR2) is simpler but means linking the renderer and the game in and presenting
     with the CPU.
6. **Speed: TyrQuake's lessons** (Quake II's soft renderer descends from Quake's).
   - From day one: `timedemo`, a bench script as TQ `miyoo/tools/bench.sh`, and bit-exact
     screenshot sets (`shots.sh` + `shots_compare.py`) before any optimisation.
   - What paid in TyrQuake: the surface cache on two threads (+16-23 %), the 8 bpp conversion
     on both cores, PGO + LTO (`pgo.sh`, ~7 %).
   - What didn't: miyoo.md section 5, "What didn't pay".
   - Resolution: 640x480 if fast enough, otherwise lower and scaled by MI_GFX (OpenLara's
     dynamic resolution, miyoo.md section 8) - only with the owner's yes (no-visual-change rule).
7. **Tests and diagnostics.**
   - The emulator: RR2 `miyoo/emu/emu_shim.c` + `miyoo-emu.py` (the most complete copy: it
     also detects hangs): scripted buttons and screenshots under qemu, CI without the device.
   - Crash handler with pc/lr and stack addresses for addr2line: RR2 `miyoo_sdl.cpp`.
   - Watchdog for untested threaded builds, `mshot.sh`, `mssh`, SSH launch rules: the `miyoo`
     skill.
8. **Release.**
   - A zip that mirrors the card: `Roms/PORTS/Games/Quake2/`, shortcut in
     `Roms/PORTS/Shortcuts/FPS/` with `GameDataFile=pak0.pak` (hidden until the player adds
     the data), HOME set to the game folder in the launcher, no game data inside.
   - Packaging examples: TQ `miyoo/standalone/package.sh`, SCR `miyoo/package.sh`.
   - Then an entry in Port Manager's catalog (open source, native: it fits the rules).

### Order of work

1. Skeleton: Yamagi with SDL2 and the RR2 layer, CPU present, dynamic build by TQ's recipe.
   Goal: the demo `pak0.pak` from `Roms/PORTS/Games/Quake2/baseq2/` starts with a picture and
   the buttons.
2. Sound through the callback, to the FIFO or `/dev/dsp`.
3. `timedemo` timings and a reference screenshot set.
4. Present through MI_GFX, checked against the screenshot set (the picture must stay
   identical).
5. TyrQuake's optimisations, one at a time, each checked against the shots.
6. Onion integration (Game Switcher, POWER save), sound polish, release.

## Faster frames: the plan and the results (2026-11-01)

Start: demo1 38.8 fps, demo2 33.6 fps (640x480, 1200 MHz, -O2, MI_GFX present, the
R_ApplyLight fix). The profile of demo1 (step 5 above): D_DrawSpansPow2 22 %, D_CacheSurface
13 %, R_PolysetDrawSpans8_Opaque 12 %, RE_EndFrame (8 bpp -> 32 bpp) 10 %, D_DrawZSpans 6 %.
Every step: timedemos (`miyoo/tools/bench_ab.sh`, settings alternated in one session) and the
shots against `build/shots/ref` (bit-exact, the owner's rule). Owner's order, 2026-11-01.

### The ideas, in order

1. **Cheap, picture unchanged**
   1. A bigger surface cache. Yamagi's size at 640x480 is ~1.5 MB (`R_InitCaches`: 768 KB at
      320x240 plus 3 bytes per extra pixel); TyrQuake-Miyoo's is 10 MB. A cache too small
      for a frame rebuilds lit surfaces (D_CacheSurface). Test with `sw_surfcacheoverride`
      (bytes); watch the peak memory (~103 MB RAM, ~5 MB free under Onion).
   2. Compiler: -O3, LTO, PGO (tyrquake-miyoo: ~7 % together, shots identical with
      `-ffp-contract=off`).
   3. RE_CopyFrame (the 8 -> 32 bpp pass): 4 pixels per 32-bit load, width and stride in
      locals (TyrQuake's VID_Update).
2. **The second core** (TyrQuake-Miyoo: ~1.45x)
   1. A worker thread on core 1 (spin then `sched_yield`, then a condvar), first used for
      RE_CopyFrame in two halves (~1.5 ms).
   2. Spans and z spans (~28 %): record per frame, both threads replay it for the upper and
      lower band, the split row moved towards equal times.
   3. Surface cache builds on both threads.
   4. Alias models (polysets) on both threads.
3. **Specialised loops**: D_DrawSpansPow2 checks the texture filter and the adaptive step
   for every span; versions per case. Model spans: the skin as pskin + t * skinwidth
   (TyrQuake). dethrace got 25 -> 38.6 fps from such specialisations.
4. **Experiments that need the owner's yes**
   - MI_GFX with an 8-bit palette source (I8 -> ARGB8888), if the SDK has it: the 8 -> 32
     pass would leave the CPU and the picture would stay the same. Unknown: a short test.
     (Owner: try it, with a way back. Done: see the results, +7 % / +5 %.)
   - RGB565 output as TyrQuake (half the writes; colours lose their low bits: a visible
     change). **Owner: no, the game's look must not change** (2026-11-01).
   - A resolution option (480x360, 320x240, scaled by MI_GFX): a player's choice.
5. **To check**: OGG music decoding runs in the game thread (TyrQuake: ~1 ms on a thread);
   first see whether the tracks are on the card.

Estimate for all: 1.6-1.8x, demo1 near 60 fps. An estimate, to be measured step by step.

### Results

- **1.1 Surface cache size: no gain, not taken** (2026-11-01 10:05, two rounds alternated,
  1200 MHz, 74-79 °C, charging at 67 %; `bench.sh` now notes the peak memory, VmHWM):

  | sw_surfcacheoverride | demo1 | demo2 | peak memory |
  |---|---|---|---|
  | default (~1.5 MB) | 38.5 / 38.5 fps | 33.5 / 33.4 fps | 69.9 / 70.5 MB |
  | 4 MB | 38.7 / 38.6 | 34.1 / 33.7 | 72.3 / 72.4 MB |
  | 8 MB | 38.2 / 38.5 | 33.8 / 33.8 | 74.9 / 75.5 MB |

  Within the noise (at most ~1 % in demo2) for 2-5 MB more memory: the cache doesn't
  thrash in these demos; D_CacheSurface's 13 % is surfaces built because they're seen new
  or their light changed.
- **Frame pacing too (owner, 2026-11-01: frame pacing matters as much as fps).** Each
  frame's time is logged now: `MIYOO_FRAMES=file` (`miyoo_sdl.c`, kept in memory, written at
  exit), `bench.sh FRAMES=1`, `miyoo/tools/frametimes.py` (mean, p50/p90/p99/p99.9, longest,
  frames over 50/100 ms; the first 50 frames left out). Timedemo frames (no frame limit), so
  these are render times, not the panel's pacing.
- **1.1 again, with frame times, and 1.2 -O3 + LTO** (2026-11-01 10:13, two rounds A B C A B
  C, 1200 MHz, 74-78 °C, charging at 75 %; files in `build/frames_2026-11-01a/`):

  | | demo1 fps | demo2 fps | demo1 p99 / max ms, >50 ms | demo2 p99 / max ms, >50 ms |
  |---|---|---|---|---|
  | A: -O2 | 38.2 / 38.4 | 33.5 / 33.6 | 52.2 / 84.6, 10 · 43.5 / 58.9, 4 | 50.4 / 62.3, 6 · 48.2 / 58.2, 5 |
  | B: -O2, 4 MB cache | 38.4 / 38.6 | 33.9 / 33.9 | 42.6 / 53.2, 1 · 45.0 / 54.5, 2 | 49.7 / 58.5, 4 · 50.2 / 77.9, 6 |
  | C: -O3 + LTO | **40.0 / 40.3** | **34.9 / 34.9** | 40.6 / 44.0, 0 · 40.5 / 47.8, 0 | 53.8 / 62.3, 8 · 50.6 / 62.0, 6 |

  - The long frames are the demos' content: in demo2 the same frames (331-333, 345-346, 389,
    546-548, 593) take 45-62 ms in every variant and round. demo1's runs of 50-85 ms frames
    (233-241, 634-638) came only in the very first run after MainUI quit (A1): a cold start
    (files read from the card the first time). The rest are single 45-60 ms frames at random
    places: the system. So the bigger cache doesn't take the stutters away either: not
    taken. Only a faster renderer shortens the heavy frames.
  - -O3 + LTO (ref_soft.so only; `OPT=-O3 LTO=1 build_miyoo.sh`): **+4-5 %**, demo1's
    longest frames shorter. LTO needed tyrquake-miyoo's `GLIBC_COMPAT_WRAP` (the .symver
    pins are lost in LTO units: the build's glibc check caught `pow@GLIBC_2.29`), plus
    `fmod`. Shots: see the correction below (checked again: identical).
- **1.2 PGO + -O3 + LTO** (2026-11-01 10:30, A B C A B C, files in `build/frames_2026-11-01b/`):

  | | demo1 fps | demo2 fps | demo1 p50 / p90 / p99 ms | demo2 p50 / p90 / p99 ms |
  |---|---|---|---|---|
  | A: -O2 | 38.4 / 38.5 | 33.0 / 33.5 | 25.7 / 33.0 / 43.7 | 29.4 / 39.1 / 55.0 |
  | B: -O3 + LTO | 38.8 / 40.1 | 35.2 / 34.5 | 24.9 / 32.2 / 45.4 | 27.8 / 36.7 / 51.7 |
  | C: PGO + -O3 + LTO | **41.3 / 41.3** | **36.1 / 36.0** | 24.1 / 29.9 / 38.7 | 27.1 / 35.0 / 50.1 |

  - PGO + LTO: **+7.5 % / +8 %** over -O2 (as tyrquake-miyoo's ~7 %); shots identical (see
    the correction below). The binaries got smaller (ref_soft.so 229 -> 192 KB).
  - The profile: `PGO=gen OPT=-O3 build_miyoo.sh <out>`, that build's quake2, ref_soft.so and
    baseq2/game.so run from `$GAME/test_pgogen` (bench.sh BINDIR: game.so came from there too),
    demo1 and demo2 (~3.5x slower: 11.7 / 10.2 fps). `miyoo/tools/q2step_pgo.sh`.
    **The Windows-hosted gcc writes the .gcda paths with backslashes**: on the device they
    became files in /tmp named `pgo\G~\Emulacja\quake2-miyoomini\miyoo#pgo#<target>#<obj>.gcda`
    (no /tmp/pgo directory). Sorted by the part after `#pgo#` into `miyoo/pgo/<target>/<obj>.gcda`
    (149 files), which `PGO=use` reads (the per-target -dumpdir). TODO: a pgo.sh doing all.
  - A stale profile after code changes: only the changed functions lose it (-Wno-missing-
    profile, -fprofile-correction). Work goes on with -O3 + LTO; the profile is redone before
    a release.
  - **Random single long frames** (60-180 ms, one per run at most, at places that differ
    run to run: 235 in B1 demo1 121 ms, 593 in B2 demo2 178 ms, 193 in C2 demo1 84 ms): the
    system, not the game's content. To look into (frame pacing): swap (~5 MB free under
    Onion), page faults, other processes (record ru_majflt / ru_nivcsw around such frames).
- **1.3 RE_CopyFrame 4 pixels per load, and 2.1 the worker thread** (2026-11-01 10:35, build
  `-O3 + LTO`, A B C A B C, files in `build/frames_2026-11-01c/`):

  | | demo1 fps | demo2 fps |
  |---|---|---|
  | A: -O3 + LTO before | 40.1 / 39.7 | 35.2 / 34.9 |
  | B: 4 pixels per load (`sw_threads 0`) | 42.2 / 42.0 | 36.9 / 36.7 |
  | C: and on two threads (`sw_threads 1`) | **42.9 / 42.5** | **37.0 / 37.5** |

  - `RE_CopyRows` (sw_main.c, `#ifdef MIYOO_MINI`): one 32-bit load for 4 indices, the
    palette in a local: **+5 %**. On both threads (upper / lower half): +1-1.5 % more.
  - `sw_mt.c` (new, from tyrquake-miyoo's d_mt.c): the worker on core 1, `SW_MT_RunBoth`
    (spin, `sched_yield` after 2000, then a condvar); cvar `sw_threads` (1 on the Miyoo).
    ref_soft.so links `-l:libpthread.so.0`; all pthread symbols GLIBC_2.4.
- **Correction (2026-11-01 10:50): the shot checks above had checked nothing.**
  - `shots_compare.py` takes the set folders (`build/shots/ref build/shots/new`, holding
    `demo1/`, `demo2/`); given the demo folders it found no demos and printed "OK: all
    identical". It now stops with an error when there is nothing to compare.
  - Compared properly, every new set differed from the old reference in ~100 % of the pixels,
    and from each other: **bench.sh used the player's config.cfg** (HOME = the game's
    folder). After the owner played at 09:37 it had show fps on (the counter was drawn in the
    timedemos), another brightness (`vid_gamma` 1.3, was 1.4), and the frame numbers no longer
    matched the same moments. bench.sh now runs with its own settings: HOME=/tmp/q2bench_home,
    made fresh for each run (the defaults: default.cfg + yq2.cfg).
  - The timings stay valid: each table's builds ran in the same session with the same config.
    The absolute fps since 09:37 include drawing the counter (small: the -O2 build gave the
    same 37.5-38.5 / 32.5-33.5 fps with the clean settings).
  - New reference `build/shots/ref` (-O2 build, clean settings; the old one is
    `ref_old_0920`), made twice: 25/25 identical, so the runs repeat. Against it: -O3 + LTO,
    PGO + LTO, 4 pixels per load, and on two threads: **each 25/25 identical**.
- **4. MI_GFX converts the 8-bit frame (I8 -> ARGB8888)**, the owner's pick, 2026-11-01 11:00:
  - The SDK has it: `E_MI_GFX_FMT_I8` (3) and `MI_GFX_SetPalette(fmt, MI_GFX_Palette_t *)`
    (256 entries of A, R, G, B bytes, then the first and last index; steward-fu/sdl2
    `mini/inc/mi_gfx.h`, `mi_gfx_datatype.h`).
  - `miyoo_fb.c` MiyooFb_PresentI8: the 8-bit frame copied into an MMA buffer (0.3 MB),
    the palette set only when it changes, the blit I8 -> ARGB8888 turned 180°.
    `miyoo_sdl.c` Miyoo_SetIndexedFrame (exported for ref_soft), RE_FlushFrame hands
    vid_buffer and the palette over (`#ifdef MIYOO_MINI`); the shots then convert on the
    CPU (only their frames). **`MIYOO_GFX_I8=0` turns it off** (the CPU pass as before);
    without the engine or MI_GFX_SetPalette it falls back by itself.
  - **The picture is the same**: `MIYOO_GFX_CHECK=1` read the page back every 50th frame
    and compared it with the CPU's palette lookup: 0 pixels differed (8 + 7 checks); shots
    25/25 identical.
  - First version waited for the blit on the game thread: **3.4 ms per shown frame**
    (memcpy, flush, blit), no gain. The timedemo hid it (MIYOO_VSYNC=0 leaves out frames,
    and a left-out frame costs the I8 path nothing). Now the **pan thread waits for the
    blit's fence** before showing the page (`s_panFence`), the next present waits for the
    pan anyway: the game thread spends **0.58 ms** per frame on it.
  - Measured as in play (bench `VSYNC=1`, `+set vid_maxfps 60`: every frame shown;
    A B A B; `build/frames_2026-11-01e/`):

    | | demo1 fps | demo2 fps | demo1 p50 / p90 / p99 ms | demo2 p50 / p90 / p99 ms |
    |---|---|---|---|---|
    | A: CPU pass on two threads | 42.3 / 42.1 | 37.9 / 37.6 | 23.6 / 29.9 / 41.8 | 26.1 / 33.8 / 43.6 |
    | B: MI_GFX I8, pan thread waits | **45.1 / 45.2** | **39.4 / 39.8** | 21.9 / 28.1 / 37.6 | 24.7 / 32.6 / 43.4 |

    **+7 % / +5 %.** With no frame limit (timedemo speed): demo1 44.8, demo2 39.3 fps.
  - So far from 38.5 / 33.5 fps (-O2) to 45.2 / 39.8 fps: **+17 % / +19 %**, every step
    pixel for pixel the same. PGO not yet on top (its profile is from before these changes).
- **2.2 The world's spans and z spans on two threads** (2026-11-01 12:40, tyrquake-miyoo's
  d_mt.c scheme; sw_mt.c, sw_edge.c, sw_scan.c, sw_surf.c):
  - D_DrawSurfaces records each surface's span drawing (SW_OP_SPANS / SKY / TURB / NONTURB /
    FLAT) with a copy of the drawing state; SW_MT_EndRecord replays the records on both
    threads, upper and lower band, the split row moved towards equal times per flush slot
    (as tyrquake). The span drawers skip spans outside the thread's band
    (`SW_SPAN_OUTSIDE_BAND`). The state they read is per thread (`SW_TLS`: __thread,
    initial-exec): d_sdivzstepu & co, sadjust, tadjust, bbextents, bbextentt, cacheblock,
    cachewidth.
  - **Yamagi's z-damage check** (D_DrawZSpans writes z only inside the area the last frame's
    models damaged, and widens it as it goes): it depends on the order, so it is made while
    recording, in the original order, into each span's new `zwrite`; the replay only reads it.
  - **Surface cache blocks**: a block a record uses (`surfcache_t.mtpass`) is never
    overwritten before the records are drawn: D_SCAlloc (each block it frees) and
    D_CacheSurface (a rebuild in place) call SW_MT_CacheOverwrite, which draws the records
    first. (Exact, unlike tyrquake's "flush after a quarter of the cache" rule, which left
    a hit block just ahead of the rover at risk.)
  - Measured as in play (VSYNC=1, max fps 60, A B A B): demo1 44.1 / 44.6 -> **48.4 / 48.3
    fps**, demo2 38.6 / 38.9 -> **42.3 / 41.9 fps: +9 %** (timedemo speed: 48.7 / 41.6).
  - **Shots: numbered by the demo's own frame now.** The two-thread build first gave other
    moments of demo1 in every shot (the camera elsewhere, other ammo), not other pixels:
    the shots were numbered by the presents since the start, and how many frames are drawn
    while the map loads depends on the speed (the builds up to ~45 fps happened to match).
    Now `Miyoo_DemoFrame` = `cl.timedemo_frames` (set in cl_view.c, `#ifdef MIYOO_MINI`)
    numbers them (miyoo_sdl.c ShotDue). New reference `build/shots/ref` (-O2 build, one
    thread, CPU pass: `sw_threads 0`, `MIYOO_GFX_I8=0`), made twice: identical. Against it
    the two-thread build, with sw_threads 1 and 0: **25/25 identical**.
  - `/tmp` is emptied by a reboot: copy bench.sh and bench_ab.sh with every step script.
- **2.3 Surface caches built on both threads** (2026-11-01 13:00): while D_DrawSurfaces
  records, D_CacheSurface allocates the block as before but hands the build (R_BuildLightMap +
  R_DrawSurface) to sw_mt.c (SW_MT_DeferBuild: a copy of the drawsurf_t). Each flush first
  builds them on both threads (an atomic job counter, as tyrquake's build_caches), then
  replays the spans. Per thread now: `blocklights` / `blocklight_max` (the worker allocates
  its own, as large as `r_numallocatedlights`) and R_DrawSurface's statics (r_lightptr,
  r_source, r_stepback...). A block with a build still due counts as used
  (SW_MT_CacheOverwrite flushes first).
  - As in play (A B A B, `build/frames_2026-11-01g/`): demo1 48.0 / 48.1 -> **50.6 / 51.0
    fps**, demo2 42.0 / 42.0 -> **44.1 / 44.1 fps: +5-6 %**; p90 demo1 27.3 -> 24.6 ms, demo2
    31.8 -> 29.3 ms (the heavy frames gain most). Timedemo speed 51.9 / 44.0. Shots 25/25
    identical.
- **Profile after 2.3** (2026-11-01 13:00, demo1 frames 60-609, `build/prof/mt2_demo1.txt`):
  main thread 1014 samples, worker 382 (it waits most of the frame). Main thread:
  R_PolysetDrawSpans8_Opaque 22.8 %, D_DrawSpansPow2 21.0 %, D_DrawZSpans 8.0 %,
  R_DrawSurface 7.6 %, R_ProcessLeftEdge 5.8 %, R_GenerateSpans 5.1 %, libc 4.7 %: the models
  were now the largest part left on one thread.
- **2.4 Models on both threads** (2026-11-01 13:10, tyrquake-miyoo's D_PolysetBeginRecord
  scheme): R_DrawEntitiesOnList records the triangles that face the view (R_DrawTriangle:
  the 3 compact vertices, r_affinetridesc, d_pdrawspans, r_aliasblendcolor, the entity);
  SW_MT_PolyFlush (before a sprite, a beam or a null model, and at the end of each pass)
  replays them on both threads in order, each drawing only its band's rows (the span drawers
  still step their edges through every row). Per thread: all of sw_polyset.c's rasterizer
  state, triangle_spans (the worker allocates its own), r_affinetridesc, d_pdrawspans,
  r_aliasblendcolor. The edge tables pointed at r_p0..r_p2, which have no constant address
  per thread: now indices, each thread builds the one it uses (R_UseEdgeTable). The z-damage
  area the drawers widen goes into a box per thread while replaying (`sw_zdamage`), merged
  after (min / max: the order doesn't matter).
  - First version (every thread set up every triangle): demo2 +4 %, demo1 nothing. Then a
    triangle wholly outside a thread's rows is skipped before its setup: as in play
    (`build/frames_2026-11-01h/`) demo1 50.6 / 50.1 -> **52.7 / 52.5 fps (+4 %)**, demo2
    44.1 / 43.7 -> **47.0 / 47.1 fps (+7 %)**; p99 demo1 38.5-43.6 -> 31.5-33.2 ms, demo2
    41.5-46.1 -> 37.4-38.0 ms. Timedemo speed 54.8 / 47.1. Shots 25/25 identical (also with
    sw_threads 0).
  - With `sw_threads 0` the same build gives 38.4 / 33.5 fps: the two threads together
    (2.1-2.4) are **+37 % / +41 %**.
- **Where it stands (2026-11-01 13:15)**: 38.5 / 33.5 fps (-O2) -> **52.6 / 47.1 fps as in
  play (+37 % / +41 %)**, every step pixel for pixel the same. Still to do: 3 specialised
  loops, PGO (redo the profile), the random 60-180 ms system hiccups.
