#!/bin/sh
# On the Miyoo: 1200 MHz vs 1600 MHz comparison on the deployed Argon PGO build.
: > /tmp/q2bench_oc_play.txt
for r in 1 2; do
	MHZ=1200 VSYNC=1 EXTRA='+set vid_maxfps 60' FRAMES=1 FRAMESET="A$r" sh /tmp/bench.sh
	{ echo "## A (round $r): 1200 MHz"; cat /tmp/q2bench.txt; } >> /tmp/q2bench_oc_play.txt
	MHZ=1600 VSYNC=1 EXTRA='+set vid_maxfps 60' FRAMES=1 FRAMESET="B$r" sh /tmp/bench.sh
	{ echo "## B (round $r): 1600 MHz"; cat /tmp/q2bench.txt; } >> /tmp/q2bench_oc_play.txt
done
: > /tmp/q2bench_oc_raw.txt
for r in 1 2; do
	MHZ=1200 VSYNC=0 sh /tmp/bench.sh
	{ echo "## A (round $r): 1200 MHz"; cat /tmp/q2bench.txt; } >> /tmp/q2bench_oc_raw.txt
	MHZ=1600 VSYNC=0 sh /tmp/bench.sh
	{ echo "## B (round $r): 1600 MHz"; cat /tmp/q2bench.txt; } >> /tmp/q2bench_oc_raw.txt
done
/mnt/SDCARD/.tmp_update/bin/cpuclock 1200 >/dev/null 2>&1
echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo done > /tmp/q2oc_done

