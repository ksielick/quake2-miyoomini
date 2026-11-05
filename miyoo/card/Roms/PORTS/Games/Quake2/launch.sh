#!/bin/sh
# Quake II (Yamagi Quake II, software renderer) on the Miyoo Mini Plus with OnionOS.
# Started by the Ports shortcut (Roms/PORTS/Shortcuts/FPS/Quake II.port) through Onion's
# launch_standalone.sh, or over SSH: sh launch.sh [game arguments]
# After tyrquake-miyoo's miyoo/standalone/card/launch.sh. The player's data goes in baseq2/
# here (pak0.pak, pak1.pak, pak2.pak from their own copy, or the demo's pak0.pak).
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR" || exit 1

# Clean macOS metadata files (._* and .DS_Store) which break Quake II pak scanning
clean_mac_files () {
	find "$1" -name "._*" -type f -delete 2>/dev/null
	find "$1" -name ".DS_Store" -type f -delete 2>/dev/null
}
[ -d baseq2 ] && clean_mac_files baseq2
[ -d xatrix ] && clean_mac_files xatrix
[ -d rogue ] && clean_mac_files rogue

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
[ -d xatrix ] && lower_names xatrix
[ -d rogue ] && lower_names rogue

if [ ! -f baseq2/pak0.pak ]; then
	printf 'Quake II data missing: put baseq2/pak0.pak\n(and pak1.pak, pak2.pak) in\nRoms/PORTS/Games/Quake2/' > /tmp/MissingPortFile.tmp
	exit 1
fi

case "$*" in
	*xatrix*)
		if [ ! -f xatrix/pak0.pak ]; then
			printf 'The Reckoning data missing:\nput pak0.pak in\nRoms/PORTS/Games/Quake2/xatrix/' > /tmp/MissingPortFile.tmp
			exit 1
		fi
		;;
	*rogue*)
		if [ ! -f rogue/pak0.pak ]; then
			printf 'Ground Zero data missing:\nput pak0.pak in\nRoms/PORTS/Games/Quake2/rogue/' > /tmp/MissingPortFile.tmp
			exit 1
		fi
		;;
esac

# settings and saves go to $HOME/.yq2: Onion's HOME (/home/root) doesn't exist on its
# read-only root, so they are kept here, in the game's folder on the card
export HOME="$DIR"

{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
[ -x /mnt/SDCARD/.tmp_update/bin/cpuclock ] && /mnt/SDCARD/.tmp_update/bin/cpuclock 1600 >/dev/null 2>&1
{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = 0 ] && { echo 1 > "$CPU1"; } 2>/dev/null

# play log (for stutter checks): with a file playlog.on here, each frame's time and counts go
# to playlog/frames-<date>.txt, written when the game quits (miyoo_sdl.c MIYOO_FRAMES)
if [ -f playlog.on ]; then
	mkdir -p playlog
	export MIYOO_FRAMES="$DIR/playlog/frames-$(date +%Y%m%d-%H%M%S).txt"
fi

# Detect screen resolution: default to native 640x480 for Miyoo Mini / Mini Plus;
# automatically adapt to native 752x560 on the Miyoo Mini Flip (RedOnion/Onion) and Miyoo Mini v4
RES_W=640
RES_H=480

DEV_MODEL=$(cat /tmp/deviceModel 2>/dev/null)
FB_H=$(fbset 2>/dev/null | awk '/geometry/ {print $3}')

if [ "$DEV_MODEL" = "285" ] || [ "$FB_H" = "560" ]; then
	RES_W=752
	RES_H=560
fi

chmod +x ./quake2 2>/dev/null
# the software renderer at the panel's resolution ($RES_Wx$RES_H); sound at audioserver's 48 kHz
# sw_gunzposition 0 and r_gunfov -1: original 4:3 weapon position without widescreen offsets
# (exec: Onion's SIGTERM at power-off reaches the game itself)
exec ./quake2 +set vid_renderer soft +set r_mode -1 +set r_customwidth "$RES_W" +set r_customheight "$RES_H" \
	+set vid_fullscreen 0 +set s_khz 48 +set sw_gunzposition 0 +set r_gunfov -1 "$@" > log.txt 2>&1
