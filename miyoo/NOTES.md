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
  - `mssh`/`mscp`/`mshot.sh` are in `G:\Emulacja\redriver2-miyoo\repos\REDRIVER2\miyoo\tools\`.
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
