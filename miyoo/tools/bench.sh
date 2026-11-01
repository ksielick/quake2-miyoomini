#!/bin/sh
# On the Miyoo: Quake II timedemos, one run per demo, results to /tmp/q2bench.txt.
#   sh bench.sh [demo ...]          (default: demo1 demo2, the two demos in pak0.pak)
# Start it the way Onion starts things, so MainUI is out of the way and comes back after:
#   echo "sh /tmp/bench.sh" > /tmp/cmd_to_run.sh; killall MainUI
# (copy it to /tmp first: keep /mnt/SDCARD/Roms out of the command's text, or Onion cuts SSH.)
# Ask the owner before running it (miyoo skill).
#
# Each run: the game at 640x480 with timedemo 1 (frames as fast as they come, the demo's own
# time ignored) and MIYOO_VSYNC=0 (a frame that would wait for the panel is left out of the
# picture, not waited for), quitting at the demo's end (nextdemo quit). The game's own line
# "N frames, S seconds: F fps" is the result; the clock, temperature and battery are noted.
# EXTRA: more console settings for every run (e.g. EXTRA="+set sw_drawflat 1").
GAME=/mnt/SDCARD/Roms/PORTS/Games/Quake2
OUT=/tmp/q2bench.txt
DEMOS=${*:-demo1 demo2}
cd "$GAME" || exit 1
export HOME="$GAME" MIYOO_VSYNC=0

{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = 0 ] && { echo 1 > "$CPU1"; } 2>/dev/null

{
	echo "# $(date '+%Y-%m-%d %H:%M:%S') up $(cut -d' ' -f1 /proc/uptime) s," \
		"$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) kHz," \
		"$(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)," \
		"battery $(/customer/app/axp_test 2>/dev/null)${EXTRA:+, $EXTRA}"
} > "$OUT"

for d in $DEMOS; do
	# a watchdog: a run that hangs is stopped after 300 s
	( sleep 300; killall -9 quake2 2>/dev/null ) &
	W=$!
	./quake2 +set vid_renderer soft +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
		+set vid_fullscreen 0 +set s_khz 48 $EXTRA +set timedemo 1 +set nextdemo quit \
		+demomap "$d.dm2" > "/tmp/q2bench_$d.log" 2>&1
	rc=$?
	kill $W 2>/dev/null
	r=$(grep -E '^[0-9]+ frames, ' "/tmp/q2bench_$d.log" | tail -n 1)
	echo "$d: ${r:-no result (exit $rc), see /tmp/q2bench_$d.log}" >> "$OUT"
done
echo "# end $(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)" >> "$OUT"
