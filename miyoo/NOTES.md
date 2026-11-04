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
   - **Done (2026-11-01 11:00): MI_GFX converts the 8-bit frame (I8 -> ARGB8888).** The 8 ->
     32 pass left the CPU; the picture is the same (0 pixels differ, shots identical); +7 % /
     +5 %. `MIYOO_GFX_I8=0` is the way back. Details: results, step 4. (The owner's pick: try
     it, with a way back.)
   - RGB565 output as TyrQuake (half the writes; colours lose their low bits: a visible
     change). **Owner: no, the game's look must not change** (2026-11-01).
   - A resolution option (480x360, 320x240, scaled by MI_GFX): a player's choice.
5. **To check**: OGG music decoding runs in the game thread (TyrQuake: ~1 ms on a thread);
   first see whether the tracks are on the card.
6. **Hand-written NEON** (added 2026-11-01 14:30, the owner asked whether we use it).
   - So far it is only gcc's auto-vectorizer (`-O3 -mfpu=neon-vfpv4`):
     - D_DrawZSpans' fills (since 3b), R_BuildLightMap, and parts of RE_RenderFrame and
       R_DrawAliasModel;
     - stb_image, for loading.
   - The hottest loops stay scalar: the wall spans, the model spans and the surface cache
     builder read one texel per pixel from a computed address, then look it up in the
     colormap. The A7's NEON has no gather.
   - The candidates, in order:
     1. **The models' vertex transform and lighting** (R_AliasTransformFinalVerts &
        co.). Dot products 4 floats at a time. The main thread does this alone, and its
        serial part is what limits the frame now (R_DrawAliasModel ~4 % of the main thread).
        The results stay bit-exact with `-ffp-contract=off` and the same order of
        operations. Look at gcc's code first. Expected 1-2 %.
     2. **The model spans' depth test** 4 pixels at a time, the texels still fetched one by
        one for the pixels that pass. More complex, gain unsure.
     3. Memory copies and clears: libc already does them.
7. **Draw one frame late** (from the profile after 3b, the owner's decision pending):
   - The worker draws frame N while the main thread sets up N+1, to overlap the ~7 ms of
     serial work per frame.
   - Cost: one frame more input latency (~17 ms).
   - Work: double-buffered records; the surface cache and the z-damage box, which depend on
     the frame before.

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
- **Where it stood (2026-11-01 13:15)**: 38.5 / 33.5 fps (-O2) -> **52.6 / 47.1 fps as in
  play (+37 % / +41 %)**, every step pixel for pixel the same.
- **3a The pixel loops' state in registers** (2026-11-01 13:50). The disassembly of the -O3 +
  LTO build showed gcc reloading globals inside the pixel loops:
  - In D_DrawSpan's vertical and diagonal loops, `cachewidth` was reloaded for every pixel:
    three dependent loads, since a `__thread` access goes through the GOT and r9.
  - R_PolysetDrawSpans8_Opaque reloaded r_zistepx, r_lstepx, the skin steps, skinwidth,
    vid_colormap, d_16to8table and the IR-goggles flags for every pixel.
  - The cause: the build has `-fno-strict-aliasing` (Yamagi's), so every store through a
    `pixel_t *` or a `zvalue_t *` may change any global as far as gcc knows. The two-thread
    work made it worse, because those globals are `__thread` now.
  - The fix takes the state into locals once per call, in the same arithmetic and order:
    - D_DrawSpansPow2: cachewidth is passed to D_DrawSpan / D_DrawSpanFiltered;
      bbextents, sadjust, the d_*divz* steps, d_viewbuffer and the width are `l_*`.
    - The five polyset span drawers became two `always_inline` templates,
      R_PolysetDrawSpansTex (_Opaque, _33, _66) and R_PolysetDrawSpansConst
      (Constant8_33, _66); errorterm and d_aspancount are written back at the end.
    - R_PolysetScanLeftEdge_C does the same.
    - R_DrawSurfaceBlock_Light keeps the tables in locals.
    - R_ApplyLight's body is now R_ApplyLightTab (light components and table pointers as
      arguments).
  - The diagonal span loop went from 11 instructions with 3 loads per pixel to 9, with none.
  - As in play (VSYNC=1, max fps 60; `build/frames_2026-11-01i/`):

    | | demo1 fps | demo2 fps | demo1 p50 / p90 / p99 ms | demo2 p50 / p90 / p99 ms |
    |---|---|---|---|---|
    | A: before (mt3) | 53.6 / 53.6 | 47.5 / 47.6 | 17.9 / 23.5 / 31.2-32.6 | 20.0 / 27.0 / 35.4-38.0 |
    | B: 3a | **56.4 / 56.3** | **51.9 / 52.2** | 17.0 / 21.3 / 30.5 | 18.2 / 24.3 / 31.7-33.6 |

    **+5 % / +9 %**; shots 25/25 identical.
  - In that session frame 187 of demo1 took 71 and 77 ms in both rounds of 3a (18.6 ms
    before).
    - Its shot (SHOTS=187) is identical to mt3's.
    - In the next session (`build/frames_2026-11-01j/`) neither build had a long frame
      there, so it was the system.
    - So a long frame at the same place in two rounds is not proof enough: run another
      session.
- **3b D_DrawZSpans' run loop** (the same session as the repeat of 3a): with a z step that
  stays below one unit over several pixels, Yamagi's loop tested two conditions per store
  (9 instructions). It now takes the run's length first and then does plain stores, which gcc
  vectorizes (4 z values per `vst1`). The values are the same; shots 25/25 identical.
  - A B C A B C as in play:

    | | demo1 fps | demo2 fps |
    |---|---|---|
    | A: 3a | 55.3 / 56.5 | 52.1 / 52.2 |
    | B: 3b | 56.8 / 56.1 | 52.0 / 52.3 |
    | C: mt3 | 53.3 / 53.2 | 47.4 / 47.1 |

  - The 3b loop is within the noise, but kept: it is simpler and exact. 3a is confirmed:
    +5-6 % / +10 % over mt3.
  - The lesson for the other ports is in miyoo.md: under `-fno-strict-aliasing` keep loop
    state in locals.
- **Profile after 3b** (2026-11-01 14:08, demo1 frames 60-609, `build/prof/s3b_demo1.txt`,
  `miyoo/tools/q2step_prof.sh`):
  - The main thread took 844 samples (~15.3 ms per frame) and the worker 476 (~8.7 ms).
  - The world and model drawing is split evenly: about 410 samples each.
  - The rest, about 430 samples or ~7 ms per frame, runs on the main thread alone while the
    worker waits:
    - R_GenerateSpans 58;
    - RE_RenderFrame 40 (its inlined setup);
    - R_DrawAliasModel 34;
    - VID_DamageZBuffer 33;
    - R_DrawTriangle 26;
    - R_EmitEdge 18;
    - the main thread's waits in flush / PolyFlush (libc);
    - the I8 present's memcpy (12).
  - All threads together: D_DrawSpansPow2 25.6 %, R_PolysetDrawSpans8_Opaque 16.9 %,
    D_DrawZSpans 10.4 %, R_DrawSurface 9.1 %.
  - So the serial part now limits the frame. Overlapping it with the drawing means drawing
    frame N while N+1 is set up, at the cost of one frame of latency. That is the owner's
    decision.
- **3c Grey model light, z damage inline** (2026-11-01 14:15):
  - With `sw_colorlight 0` (the default) every vertex has three equal light components.
    Their steps are then equal too, and so are the components along each span, where
    R_ApplyLight gives `colormap[pix + (light & LIGHTMASK)]`.
  - R_PolysetDrawSpansTex checks this (equal steps per call, equal values per span) and then
    runs a loop with one light variable instead of three: fewer registers spilled to the
    stack. Otherwise it runs the three-component loop as before.
  - VID_DamageZBuffer and VID_CheckDamageZBuffer moved from sw_main.c into header/local.h as
    `static inline`, unchanged. They are called for every span while recording. The damage
    box variables are no longer static.
  - As in play (A B A B, `build/frames_2026-11-01k/`): demo1 56.4 / 56.5 -> **57.8 / 57.6
    fps (+2 %)**, demo2 52.6 / 52.5 -> **55.0 / 54.7 fps (+4 %)**. p90 demo2 23.6-24.0 ->
    22.6-22.7 ms.
  - Shots 25/25 identical.
  - The three-component path, with `sw_colorlight 1`, gives the same shots in 3b and 3d,
    25/25. Those shots differ from `ref`, so coloured light really was drawn.
- **3d D_DrawSpan two pixels per pass: not taken.**
  - As in play: demo2 +2 %; demo1 56.8 / 57.5 against 57.8 / 57.5. demo1 is at the 60 fps
    limit there.
  - At timedemo speed (A B x3): demo1 66.2-67.3 -> 67.4-67.9 fps, demo2 60.2-60.7 ->
    60.6-61.1 fps, about +0.7 %.
  - Too little for the extra code: reverted. gcc's own loop (6 instructions a pixel for a
    row, 9 diagonal) is close to what unrolling gets on the in-order core.
- **5. The music: the GOG tracks on the card, decoded on a thread of its own** (2026-11-01
  15:10, owner asked).
  - The 20 tracks (120 MB, the owner's GOG data, never in the repo) were copied to
    `Games/Quake2/music/`, next to `baseq2/` as in GOG. demo1 and demo2 play a track.
  - The cost with music in the frame (main build s3c, A B A B as in play): music off
    58.2 / 58.1 and 55.7 / 55.2 fps, on 57.8 / 57.8 and 54.4 / 54.9 fps.
    stb_vorbis took ~22 of ~1250 samples in the profile (`build/prof/ogg_demo1.txt`):
    ~0.4 ms of each frame on the game thread.
  - ogg.c (`#ifdef MIYOO_MINI`), as tyrquake-miyoo's bgmusic.c:
    - A decoder thread at SCHED_IDLE on core 1 keeps 64 decoded chunks, ~3 s.
    - The file is opened, closed, seeked and asked only on the game thread. The decoder
      reads it only while it is `dec_file`, and OGG_DetachDecoder waits for a read in
      progress first.
    - **Yamagi's chunks are kept**: SDL_RawSamples resamples each call from its first
      sample, so each slot is one `stb_vorbis_get_samples_short_interleaved` call of 4096
      shorts, handed over one by one.
    - `MIYOO_OGG_THREAD=0` decodes in the frame as before. `MIYOO_OGG_HASH=1` prints an
      FNV-1a of the chunks played every 64 chunks.
    - No `_GNU_SOURCE` in ogg.c (strtol would become `__isoc23_strtol@GLIBC_2.38`): the
      core is set with `syscall(SYS_sched_setaffinity)`, and SCHED_IDLE is defined by
      hand.
  - **The same music**: the hash lines with and without the thread are identical (chunks
    128 and 192 in both demos). The game waited for the decoder 0-4 times per demo, at a
    track's start.
  - As in play with music (A B A B, `build/frames_2026-11-01m/`): demo1 57.5 / 56.9 ->
    **58.0 / 58.2 fps**, demo2 54.4 / 54.6 -> **55.3 / 55.4 fps** (+1.6 %, the same as with
    music off). p90 demo1 20.6-21.9 -> 19.9-20.0 ms, p99 28.9-29.8 -> 26.9 ms.
  - The card's main build is now `build/miyoo_ogg`.
- **Sound ideas from tyrquake-miyoo** (2026-11-01, owner asked). The owner hears no problems,
  but finds the sound rather "sharp":
  - Already as there: a thread of our own writes to audioserver (miyoo_audio.c), so only
    the panel paces the game; the music decoder thread (above).
  - **Sharpness** (tyrquake-miyoo: the owner found the 5 kHz filter "much more pleasant and
    soft"). Quake II's causes:
    - Sound effects are resampled to 48 kHz by taking the nearest sample, with no
      interpolation (`SDL_Cache`), as is the music (`SDL_RawSamples`). That aliasing is
      harsh.
    - The 11 / 22 kHz effects play unfiltered.
    - Yamagi has a 5 kHz low-pass filter (sdl.c lpf_*), but only underwater.
    - Candidates, as options as in tyrquake-miyoo: a "Sound Filter" low-pass (4th order,
      5 kHz) on the effects, and interpolated resampling. These change the sound, so only
      with the owner's yes.
  - Fixes that remove clicks without changing the sound, all of which Quake II needs too:
    - The mix is hard-clipped at ±32767 (`SDL_TransferPaintBuffer`). tyrquake-miyoo's
      look-ahead limiter took the crackle out of explosions there.
    - `SDL_Cache` steps with 8 fractional bits (`samplefrac >> 8`, `stepscale * 256`): the
      step is truncated, every sound slightly low and its last part never played. That
      was tyrquake's ResampleSfx bug.
    - A sound cut by a new one on the same channel stops mid-wave (fast weapons, menu
      sounds): the declicker.
    - A moving source's volume jumps once a frame: volume ramps.
    - Fades around loads (to check).
- **The sound filter, at 10 kHz by default** (2026-11-01 15:40, the owner's choice after
  "the sound is rather sharp"). How Quake II sounded in 1997:
  - Its own mixer mixed at 11 kHz by default (`s_khz 11`; the menu's "high" quality was
    22 kHz), with sounds loaded as 8 bit (`s_loadas8bit 1`).
  - It had the same nearest-sample resampling and hard clip as today.
  - The music came from the CD, analogue, past the mixer.
  - So nothing above ~5.5 / ~11 kHz reached the speakers. At 48 kHz the resampling's
    images, sounds cut mid-wave and the 8-bit steps are audible.
  - sdl.c `SDL_LowpassChunk` (`#ifdef MIYOO_MINI`) is tyrquake-miyoo's S_LowpassChunk: a
    4th-order Butterworth (two biquads, Q 0.5412 and 1.3066, TDF-II) on the effects after
    painting, before the music and Yamagi's underwater filter.
  - cvar `s_lowpass` in Hz, default 10000 (1997's "high"); 0 = off.
  - Options menu "sound filter": 1997 high (10 kHz) / 1997 low (5 kHz) / off.
  - Cost, as in play with music, A B x3 in one build: off 57.3-57.9 / 55.3-55.7 fps, on
    57.4-57.8 / 55.3-55.6 fps: within the noise. The card's main build is `build/miyoo_lp`.
- **The random long frames** (2026-11-01 15:45, owner asked). Earlier this day: 60-180 ms,
  at most one per run, at places that differed from run to run (1.2 above, 10:30).
  - On the device: a 128 MB swap file on the SD card (`/mnt/SDCARD/cachefile`, 9.5 MB used,
    swappiness 60); 103 MB RAM, 37 MB free while Onion's menu runs. This kernel's
    /proc/vmstat has no pswpin / pswpout / pgmajfault.
  - `MIYOO_FRAMES` now logs, after each frame's time, the process's getrusage counts
    during that frame: major and minor page faults, voluntary and involuntary context
    switches (miyoo_sdl.c FrameTime). `frametimes.py --long MS` (tools-miyoo) lists the
    long frames with them. Old files (time only) still read.
  - 4 rounds of both demos as in play with music (`build/frames_2026-11-01n/`,
    `miyoo/tools/q2step_hic.sh`): **no random long frame at all**. demo1's only frames over
    40 ms are 675, 681 and 687 (52-58 ms) in every round, so they are the demo's content
    (its end). demo2's longest is 34-42 ms.
  - Major faults: 0.00-0.07 per frame on average, none in a long frame.
  - Involuntary switches: 5-7 per frame, 7-14 in the long ones (the two threads).
  - The random hiccups seen in the morning did not come back in any session since ~13:00.
    Those runs had the player's config (show fps on) until 10:50, and the shots and
    timings ran right after MainUI quit.
  - Next: catch them in real play, where the timedemos may not show them (level loads,
    saves, Onion's background tasks): a play log as in tyrquake-miyoo (MIYOO_FRAMES from
    launch.sh while the owner plays, written at exit).
  - Done in the repo (a1461afb), **not yet on the card**: `launch.sh` sets `MIYOO_FRAMES`
    to `playlog/frames-<date>.txt` when a file `playlog.on` is in the game's folder;
    FRAMES_MAX is 216000 (an hour at 60 fps, 2.6 MB). Build: `build/miyoo_play`. The file is
    written only when the game quits through its menu (atexit); a power-off loses it.
  - The owner first plays without the log (2026-11-01 ~16:00), then with it. To start the
    logged play: back up the card's `launch.sh`, `quake2` and `ref_soft.so` to the PC, copy
    `build/miyoo_play`'s two binaries and the repo `launch.sh`, create `playlog.on`. After
    it: fetch `playlog/frames-*.txt` and run `frametimes.py --skip 0 --long 40` on it; for
    each long frame say whether it read from the card or swap (majflt), or lost the CPU
    (nivcsw), or neither (the game's own work: a level load, a save, a big fight).
- **Where it stood (2026-11-01 14:25)**: 38.5 / 33.5 fps (-O2) -> **57.7 / 54.9 fps as in
  play (+50 % / +64 %)**; at timedemo speed 67 / 60.5 fps. Pixel for pixel the same. The
  card's main build is `build/miyoo_s3c`.
  - Still to do: PGO with a new profile, the random system hiccups.
  - The owner's decision: drawing a frame late, to overlap the serial ~7 ms.
- **Where it stood (2026-11-01 16:00)**: the card runs `build/miyoo_hic` (everything above:
  OGG thread, 10 kHz filter, getrusage frame log; FRAMES_MAX still 20000 there). Music: the
  20 GOG tracks in `Games/Quake2/music/` (card only, never in the repo).
  - Next: the owner's logged play (above), then PGO, cleaning the card's `test_*` folders,
    rogue/xatrix game.so, Onion integration, a release.
  - Waiting for the owner: drawing a frame late (+1 frame latency); the TyrQuake sound
    fixes (limiter, 16-bit resample step, declicker, ramps; the owner hears no problems).
- **4:3 weapon position and FOV** (2026-11-03): Yamagi's widescreen defaults (`sw_gunzposition 8`,
  `r_gunfov 80`) pushed weapon models 8 units forward in view space, causing noticeable perspective
  distortion (especially on the railgun) on the Miyoo's 4:3 panel. Restored original 1997 id Software
  behavior via `sw_gunzposition 0` and `r_gunfov -1` in `sw_main.c` (C defaults), `yq2.cfg` and `launch.sh`.
- **6. Branch `Argon`: zero-copy 8-bit MMA, async MI_GFX pan thread, serial & TLS hot-loop pass, and fresh PGO** (2026-11-04):
  - **Zero-copy 8-bit MMA buffers & async MI_GFX blit on `PanThread`** (`miyoo_fb.c`, `miyoo_sdl.c`, `sw_main.c`):
    - Allocated two 8-bit frame buffers (`w * h * 2` = 600 KB) directly in cached MMA memory (`MiyooFb_GetI8Buffers` / `Miyoo_GetIndexedBuffers`) and pointed `swap_frames[0..1]` (`vid_buffer`) at them, eliminating the 307,200-byte `memcpy` in `MiyooFb_PresentI8` and skipping the redundant `RE_BufferDifferenceEnd` scan in `RE_EndFrame` when `swap_buffers_mma && !texture_high_color`.
    - Offloaded `mi.flush` + `mi.blit` + `mi.wait` onto `PanThread` (`s_panI8Virt` / `s_panI8Phys` / `s_panBlitBusy`) for frames `n > 1`, so the game thread returns from `MiyooFb_PresentI8` immediately after signaling `s_panCond`.
  - **Surface cache & lightmap hot paths** (`sw_surf.c`, `sw_light.c`):
    - Removed `SW_TLS` globals (`sourcetstep`, `prowdestbase`, `pbasesource`, `r_stepback`, `r_lightwidth`, `r_numvblocks`, `r_source`, `r_sourcemax`, `r_lightptr`) in `sw_surf.c`, keeping them in local registers across `R_DrawSurface`.
    - Added a fast 1-channel grey-light block path in `R_DrawSurface` when the 4 block corners have grey light (`l[0] == l[1] && l[0] == l[2]`, 100 % of blocks under `sw_colorlight 0`), and split the cold 3-channel RGB block loop into `noinline` `R_DrawSurfaceBlockRGB` — stack references in `R_DrawSurface` dropped from **145 to 82 (-43 %)**.
    - Cached `blocklights` / `blocklight_max` in local pointers in `R_BuildLightMap` and `R_AddDynamicLights`; skipped the initial `memset` in `R_BuildLightMap` when `lightmap` is present by writing `maps == 0` directly, and added 1-channel fast paths for `r_colorlight->value == 0`.
  - **Alias models, triangle setup & multi-threaded replay** (`sw_polyset.c`, `sw_alias.c`, `sw_mt.c`):
    - Enabled hardware integer division (`sdiv`, `__ARM_ARCH_EXT_IDIV__`) in `FloorDivMod` and removed the 4 debug `assert()` calls that prevented inlining; `FloorDivMod` now inlines directly into `R_PolysetSetUpForLineScan` as 2 `sdiv` instructions (verified 0 diffs across all `tm in [-640..640], tn in [1..480]`).
    - Replaced `ceil@plt` calls in `R_PolysetCalcGradients` with inline `R_PolysetCeilF`, added a 1-channel grey-light gradient fast path, and cached `SW_TLS` variables in local registers in `R_PolysetCalcGradients` and `R_ProcessLeftEdge`.
    - Stored `xdenom` in `sw_tricmd_t`, added `R_DrawRecordedTriangle`, and tightened band culling in `poly_replay` to `vmax <= band_v0 || vmin >= band_v1` (since row `vmax` itself is never drawn) — `poly_replay` shrank from **378 to 252 instructions (-33 %)** and stack references from **54 to 25 (-54 %)**.
    - Marked `R_AliasTransformFinalVerts` `noinline` with `__restrict` pointers, inlined `R_AliasProjectAndClipTestFinalVert`, and added a 1-channel grey vertex light path (preserving C's exact `temp += (r_shadelight * lightcos)` float addition before truncation).
  - **BSP edge emission & span generation** (`sw_edge.c`, `sw_rast.c`):
    - Kept `surfaces`, `&surfaces[1]`, `span_p`, `current_iv`, `edge_tail_u_shift20`, `shift_size` and `fv` in locals across `R_GenerateSpans` and `R_GenerateSpansBackward` (**295 -> 253 instructions, -14 %**), and bounded the surface clear loop in `R_ScanEdges` to `s < surface` instead of `s < surf_max`.
    - Replaced `ceil@plt` in `R_EmitEdge` with inline `R_FastCeilF` and cached globals in locals (**0 `@plt` calls vs 2, 0 stack refs vs 16**, preserving `eu = u * l_shift + l_shift - 1` float rounding).
  - **Fresh PGO profile** (`miyoo/pgo/{client,ref_soft,game}/*.gcda`, 150 files collected on `demo1` + `demo2`):
    - `ref_soft.so` shrank from 306 KB to **207 KB (-32 %)** and `quake2` from 782 KB to **605 KB (-23 %)**.
  - **Pixel-exactness**: both `test_argon` (`-O3 + LTO`) and `test_argon_pgo` (`-O3 + LTO + PGO`) match `ref_s3c` (`test_s3c` with the 4:3 weapon config) **25/25 screenshots (100 %, 0 pixels differing)**.
  - **Measured as in play (`VSYNC=1`, `+set vid_maxfps 60`, 59.67 Hz panel ceiling; A B C A B C, `build/frames_argon_pgo2/`) and at unconstrained timedemo speed (`VSYNC=0`, A B C A B C)**:

    | Build | `demo1` play fps (`VSYNC=1`) | `demo2` play fps (`VSYNC=1`) | `demo1` p50 / p90 / p99 / max ms (`>50`) | `demo2` p50 / p90 / p99 / max ms (`>50`) | `demo1` raw fps (`VSYNC=0`) | `demo2` raw fps (`VSYNC=0`) |
    |---|---|---|---|---|---|---|
    | A: `s3c` (baseline) | 57.5 / 57.3 (**57.4**) | 54.5 / 54.6 (**54.6**) | 17.0 / 20.8 / 27.8 / 56.4 (3) | 17.9 / 22.4 / 28.8 / 33.4 (0) | 65.1 / 65.7 (**65.4**) | 58.7 / 58.6 (**58.7**) |
    | B: `argon` (`-O3+LTO`) | 59.1 / 59.1 (**59.1**) | 56.6 / 57.3 (**57.0**) | 16.8 / 19.2 / 24.4 / 52.0 (3) | 17.0 / 20.8 / 26.9 / 42.2 (0) | 71.8 / 71.3 (**71.6**) | 64.9 / 64.5 (**64.7**) |
    | C: `argon_pgo` (`PGO+LTO`) | **59.5 / 59.5 (59.5)** | **58.0 / 58.0 (58.0)** | **16.7 / 19.0 / 23.9 / 50.6 (1)** | **16.8 / 20.1 / 26.7 / 38.3 (0)** | **72.6 / 74.8 (73.7)** | **67.7 / 67.2 (67.5)** |

  - **Summary of gains over `s3c`**:
    - Unconstrained rendering speed (`VSYNC=0`): **+12.7 % on `demo1` (65.4 -> 73.7 fps)** and **+15.0 % on `demo2` (58.7 -> 67.5 fps)**.
    - In-play frame rate (`VSYNC=1`, capped by the 59.67 Hz panel): **57.4 -> 59.5 fps on `demo1`** (99.7 % of the hardware refresh rate) and **54.6 -> 58.0 fps (+6.3 %) on `demo2`**, with median frame time (`p50`) locked to a single 16.76 ms panel refresh (`16.7 / 16.8 ms`) in both demos and `p99` reduced by **3.9 ms** on `demo1` (`27.8 -> 23.9 ms`) and **2.1 ms** on `demo2` (`28.8 -> 26.7 ms`).
  - **Total speedup since initial port (`38.5 / 33.5 fps` at `-O2`)**: **+55 % / +73 % as in play** (`59.5 / 58.0 fps`, vsync-limited) and **+91 % / +101 % raw rendering speed** (`73.7 / 67.5 fps`), 100 % pixel-identical.
- **6b. Profile of `Argon` and weighted `SW_MT_PolyFlush` row split + local band/z-damage registers** (2026-11-04):
  - **Sampling profile of `Argon` (`PROF=60,550`, `build/prof/argon_pgo_demo1.txt`, `build/prof/argon_pgo_demo2.txt`)**:
    - `demo1` (1170 samples, 73.2 fps): main thread **13.0 ms/frame** (716 samples, down from 15.3 ms in `s3b`), worker **7.7 ms/frame** (422 samples, down from 8.7 ms), OGG thread **0.38 ms/frame** (21 samples), `fb_pan` **0.20 ms/frame** (11 samples; **0 samples** in `MiyooFb_PresentI8` on the game thread).
    - `demo2` (1334 samples, 66.5 fps): main thread **15.0 ms/frame** (823 samples), worker **8.7 ms/frame** (476 samples).
    - `R_AliasTransformFinalVerts` is down to **1.4 % (`0.29 ms/frame`) in `demo1` and 0.9 % (`0.22 ms/frame`) in `demo2`**, making hand-written NEON vertex transforms unnecessary (~0.1 ms ceiling).
    - However, in `demo2`, `[libc-2.28.so]` (`sched_yield` inside `SW_MT_RunBoth`) accounted for **69 samples (8.4 % of the main thread, ~1.25 ms/frame)**: `SW_MT_PolyFlush` split the full screen `[0, 480]` at `0.5f` (`y = 240`), and whenever a batch of triangles lay entirely in the lower half of the screen (e.g. the view weapon or floor-level monsters), `j.ms[0]` was `< 0.05 ms`, so `if (j.ms[0] > 0.05 && j.ms[1] > 0.05)` never triggered and `poly_split[slot]` stayed frozen at `0.5f` while thread 1 drew 100 % of the triangles.
  - **Fix (`sw_mt.c`, `sw_polyset.c`, `sw_scan.c`)**:
    - Stored `short vmin, vmax` in `sw_tricmd_t` during `SW_MT_RecordTriangle` (avoiding recomputing `vmin`/`vmax` on both threads in `poly_replay`) and accumulated a 1D row-weight difference array `poly_row_diff[cvmin] += w; poly_row_diff[cvmax] -= w;` (`w = (umax - umin) + 16`) over the batch's exact vertical bounding box `[poly_v0, poly_v1]`.
    - In `SW_MT_PolyFlush`, scanned `poly_row_diff[poly_v0 .. poly_v1]` to place `j.v[1]` at the exact scanline where cumulative triangle weight reaches `poly_total_weight * f` (cleaning `poly_row_diff` in the same pass), and relaxed the adaptive update check in both `flush()` and `SW_MT_PolyFlush()` to `j.ms[0] + j.ms[1] > 0.1`.
    - Cached `sw_band_v0` and `sw_band_v1 - sw_band_v0` in local registers before the span loops in `R_PolysetDrawSpansTex`, `R_PolysetDrawSpansConst`, `D_DrawSpansPow2` and `D_DrawZSpans`, and accumulated `zminu, zmaxu, zminv, zmaxv` in local registers across `R_PolysetDrawSpansTex` / `R_PolysetDrawSpansConst` so `VID_DamageZBuffer` (`sw_zdamage`) is written once per call instead of twice per scanline.
  - **Results (refreshed PGO profile, 25/25 screenshots pixel-identical)**:
    - In `demo2` profile (`build/prof/argon_poly_demo2.txt`), `SW_MT_RunBoth` wait samples in `[libc-2.28.so]` dropped by **43 %** (`56 -> 32` samples) and `poly_replay` split **15 vs 15 samples** across both threads.
    - Raw `demo2` speed (`VSYNC=0`) rose from `67.5 fps` to **`68.3 / 67.8 fps` (68.1 fps, +16.0 % over `s3c`)** (`demo1`: `73.9 / 74.0 fps`), and on the cleaned card (`q2step_oc.sh`, 1200 MHz) reached **`76.2 / 75.9 fps` (76.1 fps)** in `demo1` and **`69.0 / 69.5 fps` (69.3 fps)** in `demo2`, with in-play (`VSYNC=1`) at **`59.2 / 59.6 fps`** (`demo1`) and **`58.4 / 58.5 fps`** (`demo2`, `p99 = 26.2 / 25.2 ms`).
    - Deployed to the SD card (`/mnt/SDCARD/Roms/PORTS/Games/Quake2/`).
- **6c. 1600 MHz overclock benchmark (`cpuclock 1600`, `miyoo/tools/q2step_oc.sh`, `build/frames_argon_oc/`)** (2026-11-04):
  - Measured 2 alternated rounds (`1200 MHz` vs `1600 MHz`) on the deployed `Argon` PGO build (`VSYNC=1, +set vid_maxfps 60` as in play, and `VSYNC=0` raw timedemo speed; temperature 61–66 °C and battery rail 3547–3554 mV identical between 1200 and 1600 MHz):

    | Clock | `demo1` play (`VSYNC=1`) | `demo2` play (`VSYNC=1`) | `demo1` p50 / p90 / p99 / max ms (`>50`) | `demo2` p50 / p90 / p99 / max ms (`>50`) | `demo1` raw (`VSYNC=0`) | `demo2` raw (`VSYNC=0`) |
    |---|---|---|---|---|---|---|
    | **1200 MHz** | 59.2 / 59.6 (**59.4 fps**) | 58.4 / 58.5 (**58.5 fps**) | 16.7 / 18.7 / 24.0 / 52.2–63.4 (1) | 16.7 / 19.7 / 25.7 / 32.6–42.7 (0) | 76.2 / 75.9 (**76.1 fps**) | 69.0 / 69.5 (**69.3 fps**) |
    | **1600 MHz** | **59.9 / 60.0 (60.0 fps)** | **59.6 / 59.7 (59.7 fps)** | **16.7 / 18.1 / 24.3 / 45.0 (0)** *(r2)* | **16.7 / 18.4 / 22.0 / 30.1 (0)** | **91.9 / 92.6 (92.3 fps)** | **84.0 / 84.3 (84.2 fps)** |

  - At **1600 MHz** (+33 % CPU clock), raw rendering speed scales by **+21.3 % in `demo1` (`76.1 -> 92.3 fps`)** and **+21.5 % in `demo2` (`69.3 -> 84.2 fps`)**.
  - In-play (`VSYNC=1`), **both `demo1` (`60.0 fps`) and `demo2` (`59.7 fps`) saturate the hardware 59.67 Hz panel refresh rate**, with mean frame time locked at `16.8 ms` (`p50 = 16.7 ms`), `demo2` `p99` down to **`21.6–22.4 ms`**, and the longest frame in all of `demo2` at **`29.8–30.3 ms`** (every single frame under 2 panel refreshes).
- **6d. Fix in-game framerate clamp when `vid_maxfps > 100` (`src/common/frame.c`)** (2026-11-04):
  - **Issue**: In the video options menu, selecting "unlimited" max FPS (`vid_maxfps 300`) while `cl_showfps 1` resulted in the in-game counter capping around ~71 fps (instead of 85–95 fps).
  - **Root Cause**: In `Qcommon_Frame` (`src/common/frame.c`), the `#ifdef MIYOO_MINI` block only handled `vid_maxfps <= 100` (panel refresh pacing). When `vid_maxfps > 100`, execution fell through to Yamagi's upstream `if (R_IsVSyncActive())` block. Because `r_vsync` was active (1) by default in config, `rfps = refreshrate * 1.2f = 59.67 * 1.2 = 71.6 fps`, clamping frame times to ~71.6 fps.
  - **Fix**: Wrapped the entire upstream `if (R_IsVSyncActive()) ... else ...` block in `#else` and handled `vid_maxfps > 100` directly under `#ifdef MIYOO_MINI` (`rfps = vid_maxfps->value`), so on Miyoo, setting unlimited FPS bypasses the software vsync clamp as intended.
  - **Build & Deploy**: Recompiled `build/miyoo_pgo` and deployed to `/mnt/SDCARD/Roms/PORTS/Games/Quake2/`.
- **7. Official Mission Packs (*The Reckoning* / `xatrix`, *Ground Zero* / `rogue`), shortcut icons & release package** (2026-11-05):
  - **Mission packs compilation**: Integrated sources into `src/xatrix` and `src/rogue`, updated `miyoo/build/build_miyoo.sh` to compile `xatrix/game.so` (582 KB) and `rogue/game.so` (689 KB) linked against `glibc_compat.c` with zero unresolved symbols.
  - **Nintendo D-pad controls & auto-aim**: Added `autoaim` (light vertical auto aim) and `aimfix 1` defaults into both `src/xatrix/` and `src/rogue/`, ensuring weapons in all expansion packs aim smoothly on handheld controls without analogue sticks.
  - **Port shortcuts & icons**: Added dedicated `.port` shortcuts (`Quake II - The Reckoning.port`, `Quake II - Ground Zero.port`) in `Roms/PORTS/Shortcuts/FPS/` and created high-quality 250x360 cover art icons for all three titles in `Roms/PORTS/Imgs/` and next to each shortcut.
  - **Launcher enhancements**: Extended `launch.sh` with automatic directory lower-casing and missing `.pak` file error reporting for each expansion pack.
  - **Standalone release packaging**: Created `miyoo/tools/package_release.sh` producing clean `dist/Quake2-Miyoo-v1.0.0.zip` (1.9 MB) and debug symbols `dist/Quake2-Symbols-v1.0.0.zip` (1.3 MB) ready for 100 % stock OnionOS.


