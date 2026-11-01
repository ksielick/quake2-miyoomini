#!/bin/sh
# On the Miyoo (2026-11-01): what the sound filter costs. Build $GAME/test_lp, the filter off
# (s_lowpass 0) and at its default (10 kHz), music on, three rounds as in play (VSYNC=1, max
# fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=3 A="+set vid_maxfps 60 +set s_lowpass 0" A_BIN=./test_lp \
	B="+set vid_maxfps 60" B_BIN=./test_lp sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
