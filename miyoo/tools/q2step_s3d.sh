#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 3d): D_DrawSpan two pixels per pass, build
# $GAME/test_s3d. Shots; shots with coloured light (sw_colorlight 1: the polyset loop with three
# light components) from test_s3b and test_s3d; then two rounds against $GAME/test_s3c as in
# play (VSYNC=1, max fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_s3d SHOTS=50 SET=s3d sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
BINDIR=./test_s3b SHOTS=50 SET=col_s3b EXTRA="+set sw_colorlight 1" sh /tmp/bench.sh
BINDIR=./test_s3d SHOTS=50 SET=col_s3d EXTRA="+set sw_colorlight 1" sh /tmp/bench.sh
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_s3c \
	B="+set vid_maxfps 60" B_BIN=./test_s3d sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
