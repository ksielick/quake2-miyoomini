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

## Plan (first ideas, nothing built yet)

1. Build the client + soft renderer + baseq2 game for the Mini with the Arm toolchain, **SDL2
   off**, behind our own small platform layer:
   - fbdev with 180° flip and page flipping on its own thread;
   - evdev buttons;
   - sound to audioserver, or OSS through libpadsp if the binary is dynamic.
   - Link the renderer and the game in (no dlopen), so one static binary works with no
     dependence on the device's glibc.
2. Run the demo `pak0.pak` from `Roms/PORTS/Games/Quake2/baseq2/`. Measure frame times like
   TyrQuake did (timedemo, `/proc/uptime`).
3. Resolution: 640x480 native if fast enough, otherwise 320x240 scaled 2x. Use both cores
   later (TyrQuake's 2-thread surface caches gained 16-23 %).
4. Release zip that mirrors the card, for Port Manager's catalog (open source, native, no PC
   program: it fits the catalog's rules).
