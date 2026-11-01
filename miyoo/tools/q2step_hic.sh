#!/bin/sh
# On the Miyoo (2026-11-01): hunting the random long frames. Build $GAME/test_hic logs each
# frame's getrusage counts with its time (MIYOO_FRAMES). Four rounds of both demos as in play
# (VSYNC=1, max fps 60, music on); meanwhile once a second the uptime and the swap / fault
# counters of /proc/vmstat go to /tmp/q2step_vmstat.txt.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
rm -f /tmp/q2frames_*
: > /tmp/q2step_vmstat.txt
(
	while [ ! -f /tmp/q2step_done ]; do
		echo "$(cut -d' ' -f1 /proc/uptime) $(grep -E '^(pswpin|pswpout|pgmajfault) ' /proc/vmstat | tr '\n' ' ')" >> /tmp/q2step_vmstat.txt
		sleep 1
	done
) &
VSYNC=1 FRAMES=1 ROUNDS=4 A="+set vid_maxfps 60" A_BIN=./test_hic sh /tmp/bench_ab.sh
grep "^#" /tmp/q2bench_ab.txt > /tmp/q2step_times.txt
echo done > /tmp/q2step_done
