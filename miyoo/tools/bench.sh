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
# BINDIR: run another build's quake2 and ref_soft.so from that directory (e.g. $GAME/test_o3).
# FRAMES=1: each frame's time to /tmp/q2frames_<FRAMESET>_<demo>.txt (tools-miyoo analysis/frametimes.py).
# SHOTS=N: every Nth frame saved to <game>/shots/<SET>/<demo>/NNNNN.argb (SET default "new"),
# for tools-miyoo analysis/shots_compare.py on the PC (fetch them over SMB: Roms\PORTS\Games\Quake2\shots).
# The shots cost time: such runs are not timings.
# PROF=first,count: a profile of those frames of each demo (miyoo/platform/miyoo_prof.c) to
# <game>/prof_<demo>.txt; map it on the PC with tools-miyoo analysis/prof_map.py.
GAME=/mnt/SDCARD/Roms/PORTS/Games/Quake2
OUT=/tmp/q2bench.txt
DEMOS=${*:-demo1 demo2}
cd "$GAME" || exit 1
# The runs use their own settings, the defaults (default.cfg, yq2.cfg), not the player's:
# the player's config.cfg (show fps, brightness...) changed the shots and the timings
# (2026-11-01). Made fresh for each run in /tmp.
# VSYNC=1: every frame waits for the panel and is shown, as in play (with +set vid_maxfps 60:
# one refresh a frame); default 0: frames the panel can't take yet are left out (timedemo speed)
export HOME=/tmp/q2bench_home MIYOO_VSYNC=${VSYNC:-0}

{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
[ -n "$MHZ" ] && /mnt/SDCARD/.tmp_update/bin/cpuclock "$MHZ" >/dev/null 2>&1
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = 0 ] && { echo 1 > "$CPU1"; } 2>/dev/null

{
	echo "# $(date '+%Y-%m-%d %H:%M:%S') up $(cut -d' ' -f1 /proc/uptime) s," \
		"$(/mnt/SDCARD/.tmp_update/bin/cpuclock 2>/dev/null || cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) MHz," \
		"$(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)," \
		"battery $(/customer/app/axp_test 2>/dev/null)${EXTRA:+, $EXTRA}${SHOTS:+, shots every $SHOTS (not a timing)}"
} > "$OUT"

for d in $DEMOS; do
	# a watchdog: a run that hangs is stopped after 300 s
	( sleep 300; killall -9 quake2 2>/dev/null ) &
	W=$!
	[ -n "$PROF" ] && export MIYOO_PROF="$GAME/prof_$d.txt,$PROF"
	if [ -n "$SHOTS" ]; then
		export MIYOO_SHOTS="$SHOTS" MIYOO_SHOTS_DIR="$GAME/shots/${SET:-new}/$d"
		rm -rf "$MIYOO_SHOTS_DIR"
		mkdir -p "$MIYOO_SHOTS_DIR"
	fi
	rm -rf "$HOME"
	mkdir -p "$HOME"
	[ -n "$FRAMES" ] && export MIYOO_FRAMES="/tmp/q2frames_${FRAMESET:-new}_$d.txt"
	# BINDIR: another build (its quake2 and ref_soft.so; the data stays the current dir's)
	"${BINDIR:-.}/quake2" +set vid_renderer soft +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
		+set vid_fullscreen 0 +set s_khz 48 $EXTRA +set timedemo 1 +set nextdemo quit \
		+demomap "$d.dm2" > "/tmp/q2bench_$d.log" 2>&1 &
	P=$!
	# the peak memory (VmHWM), read once a second while it runs
	( h=; while kill -0 $P 2>/dev/null; do
		x=$(awk '/^VmHWM/ { print $2 }' /proc/$P/status 2>/dev/null); [ -n "$x" ] && h=$x
		sleep 1; done; echo "$h" > /tmp/q2bench_hwm ) &
	S=$!
	wait $P
	rc=$?
	wait $S
	kill $W 2>/dev/null
	r=$(grep -E '^[0-9]+ frames, ' "/tmp/q2bench_$d.log" | tail -n 1)
	echo "$d: ${r:-no result (exit $rc), see /tmp/q2bench_$d.log}, peak $(cat /tmp/q2bench_hwm) kB" >> "$OUT"
done
echo "# end $(cat /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)" >> "$OUT"
