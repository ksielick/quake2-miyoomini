#!/bin/sh
# Quake II (Yamagi Quake II, software renderer) on the Miyoo Mini Plus with OnionOS.
# Started by the Ports shortcut (Roms/PORTS/Shortcuts/FPS/Quake II.port) through Onion's
# launch_standalone.sh, or over SSH: sh launch.sh [game arguments]
# After tyrquake-miyoo's miyoo/standalone/card/launch.sh. The player's data goes in baseq2/
# here (pak0.pak, pak1.pak, pak2.pak from their own copy, or the demo's pak0.pak).
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR" || exit 1

# The game looks for lower case names (baseq2/pak0.pak); files copied from a PC may come in
# upper or mixed case: renamed through a temporary name (FAT may not tell the cases apart).
lower_names () {
	for f in "$1"/*; do
		[ -e "$f" ] || continue
		b=$(basename "$f")
		l=$(echo "$b" | tr 'A-Z' 'a-z')
		[ "$b" = "$l" ] && continue
		mv "$f" "$1/.rename_tmp" && mv "$1/.rename_tmp" "$1/$l"
	done
}
[ -d baseq2 ] && lower_names baseq2

if [ ! -f baseq2/pak0.pak ]; then
	printf 'Quake II data missing: put baseq2/pak0.pak\n(and pak1.pak, pak2.pak) in\nRoms/PORTS/Games/Quake2/' > /tmp/MissingPortFile.tmp
	exit 1
fi

# settings and saves go to $HOME/.yq2: Onion's HOME (/home/root) doesn't exist on its
# read-only root, so they are kept here, in the game's folder on the card
export HOME="$DIR"

{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = 0 ] && { echo 1 > "$CPU1"; } 2>/dev/null

# play log (for stutter checks): with a file playlog.on here, each frame's time and counts go
# to playlog/frames-<date>.txt, written when the game quits (miyoo_sdl.c MIYOO_FRAMES)
if [ -f playlog.on ]; then
	mkdir -p playlog
	export MIYOO_FRAMES="$DIR/playlog/frames-$(date +%Y%m%d-%H%M%S).txt"
fi

chmod +x ./quake2 2>/dev/null
# the software renderer at the panel's 640x480; sound at audioserver's 48 kHz
# (exec: Onion's SIGTERM at power-off reaches the game itself)
exec ./quake2 +set vid_renderer soft +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
	+set vid_fullscreen 0 +set s_khz 48 "$@" > log.txt 2>&1
