# Quake II for the Miyoo Mini Plus

Quake II on the **Miyoo Mini Plus** with [OnionOS](https://github.com/OnionUI/Onion), at the
screen's full **640x480**, tuned for smooth 60 fps gameplay. It is
[Yamagi Quake II](https://github.com/yquake2/yquake2) with its software renderer, made to run
straight on the console (no RetroArch), and started from Onion's **Ports** list.

**Work in progress.** It plays, with sound, but there is no release zip yet. It will come with
[Port Manager](https://github.com/ksielick/portmanager-miyoo). Speed work is going on (see
below).

- **640x480 locked at 60 fps**: plays at the hardware panel's refresh rate (59.67 Hz) with
  even frame pacing. 30 fps and "unlimited" (100+ fps) are in the video menu.
- **Made for the buttons.** The same layout as [TyrQuake-Miyoo](https://github.com/ksielick/tyrquake-miyoo),
  with a light auto aim, since there's no mouse or stick to aim with.
- **The picture stays as Quake II drew it.** Every speed-up is checked pixel for pixel.

## Buttons

| Button | In the game | In the menus |
|---|---|---|
| D-pad up / down | walk forward / back | move up / down |
| D-pad left / right | turn left / right | change the setting |
| **Y** | fire | |
| **B** | jump (in water: swim up) | back |
| **X** | crouch (in water: swim down) | delete |
| **A** | next weapon | confirm |
| **L1 / R1** | strafe left / right | change the setting |
| **L2 / R2** | look up / down | |
| **START** | menu (pause) | close the menu |
| **MENU** | menu (pause); Onion's Game Switcher later | |

**Hold SELECT** for the second set:

| Button | With SELECT held |
|---|---|
| D-pad left / right | previous / next item in the inventory |
| D-pad up | show / hide the inventory |
| D-pad down | use the item |
| **A** | previous weapon |
| **B** | help computer: your mission objectives (the scores in deathmatch) |
| **X** | walk / run (you run by default) |
| Y, L1, R1, L2, R2 | as without SELECT: fire, strafe and look still work |

The buttons can be changed in *Options → customize gamepad*; *Options → reset defaults*
brings this layout back.

## Settings

In Quake II's own menu (START):

- *Options → Video*: **max fps** (30 / 60 / unlimited; 30 by default), **show fps** (the
  counter at the top right), brightness, field of view, ui scale, texture filter. Each
  applies at once.
- *Options*: volumes and music, **sound filter**, always run, **auto aim** (on by default),
  crosshair and its colour, customize gamepad, reset defaults.

**Sound filter** gives the sound effects the softer sound of 1997. Quake II mixed its sound at
11 kHz then (22 kHz on "high"), so the sharp edges of today's 48 kHz output never reached the
speakers. The settings are *1997 high (10 kHz)*, the default, *1997 low (5 kHz)*, as the
game's own default was, and *off*. The music isn't filtered: it came from the CD.

Settings that do nothing on the Miyoo or don't work there (other renderers, video modes,
mouse and keyboard settings) are hidden.

**Auto aim** works like Quake 1's: when the crosshair isn't on an enemy but one is close to it
(within about 20°) and in sight, your shot goes up or down to it. It never turns you left or
right, it is off in deathmatch, and it never aims at other players.

## Installing (by hand, until the release)

You need OnionOS with its **Ports** list (Apps → Package Manager → Verified → Ports
Collection) and your own Quake II data, which isn't included: `pak0.pak`, `pak1.pak` and
`pak2.pak` from the full game's `baseq2` folder (GOG, Steam or the CD), or the demo's
`pak0.pak`.

1. Put the port's files in `Roms/PORTS/Games/Quake2/` on the SD card (`quake2`,
   `ref_soft.so`, `launch.sh`, `baseq2/game.so`, `baseq2/yq2.cfg`) and the shortcut
   `Quake II.port` in `Roms/PORTS/Shortcuts/FPS/`.
2. Copy your `pak` files into `Roms/PORTS/Games/Quake2/baseq2/`. Upper or lower case doesn't
   matter.
3. Play: *Games → Ports → FPS → Quake II*. If it doesn't show, refresh the list from its menu.

Music (optional): copy the GOG version's `music` folder (`Track02.ogg` ...) next to `baseq2`,
or into `baseq2/music/`. It is decoded on the second CPU core, so it costs the game no speed.

Savegames and settings are kept in `Roms/PORTS/Games/Quake2/.yq2/`.

The mission packs (The Reckoning, Ground Zero) are not supported yet.

## How fast

Quake II's timedemos at 640x480, raw rendering speed vs locked in-play frame rate (VSYNC=1, 59.67 Hz hardware ceiling):

| Timedemo | Yamagi baseline (-O2) | 1200 MHz stock (play / raw) | 1600 MHz OC (play / raw) |
|---|---|---|---|
| demo1 | 38.5 fps | 59.4 fps / 76.1 fps | **60.0 fps** / **92.3 fps** |
| demo2 | 33.5 fps | 58.5 fps / 69.3 fps | **59.7 fps** / **84.2 fps** |

Over **+140 % to +150 % raw speedup**, with the picture pixel for pixel the same (25/25 screenshots identical): the renderer balances drawing across both CPU cores (`sw_mt.c`), caches TLS and span bounds in registers, uses Profile-Guided Optimisation (PGO) + LTO, and the console's 2D engine (MI_GFX) turns each 8-bit frame into the screen's colours. In real gameplay uncapped framerates exceed **100+ fps**, leaving plenty of headroom to hold 60 fps with zero drops. Every step and measurement is documented in [miyoo/NOTES.md](miyoo/NOTES.md).

## Credits and license

Quake II is by id Software. [Yamagi Quake II](https://github.com/yquake2/yquake2) is by the
Yamagi Quake II team (its own README: [README.yamagi.md](README.yamagi.md)). This port builds
on their work and is GPL v2 like it (see [LICENSE](LICENSE)). Quake II's game data is not
included.
