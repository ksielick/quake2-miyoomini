#!/bin/sh
# On the Miyoo (2026-11-01): what the OGG music costs. The GOG tracks are in $GAME/music/ now.
# Two rounds of the main build with music off (ogg_enable 0) and on, as in play (VSYNC=1, max
# fps 60), frame times on; then a profile with music on (PROF), and the logs' music lines.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60 +set ogg_enable 0" \
	B="+set vid_maxfps 60 +set ogg_enable 1" sh /tmp/bench_ab.sh
grep -i "ogg\|music\|track" /tmp/q2bench_demo1.log /tmp/q2bench_demo2.log > /tmp/q2step_ogglog.txt
PROF=60,550 EXTRA="+set ogg_enable 1" sh /tmp/bench.sh
echo done > /tmp/q2step_done
