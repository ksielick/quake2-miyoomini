#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 3a): the pixel loops keep their state in
# registers (locals instead of globals reloaded after every store), build $GAME/test_s3a. Shots,
# then two rounds against the build before it ($GAME/test_mt3) as in play (VSYNC=1, max fps
# 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_s3a SHOTS=50 SET=s3a sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_mt3 \
	B="+set vid_maxfps 60" B_BIN=./test_s3a sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
