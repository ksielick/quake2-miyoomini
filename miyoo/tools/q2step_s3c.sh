#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 3c): model spans with grey light use one
# light variable, VID_DamageZBuffer / VID_CheckDamageZBuffer inline; build $GAME/test_s3c.
# Shots, then two rounds against $GAME/test_s3b as in play (VSYNC=1, max fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_s3c SHOTS=50 SET=s3c sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_s3b \
	B="+set vid_maxfps 60" B_BIN=./test_s3c sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
