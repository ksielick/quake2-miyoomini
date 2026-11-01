#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 3b): 3a plus D_DrawZSpans' run loop, build
# $GAME/test_s3b. Shots of s3b; shots of demo frame 187 (SHOTS=187) from test_mt3 and test_s3a,
# where 3a had a 71-77 ms frame in both rounds; then two rounds of s3a, s3b and mt3 as in play
# (VSYNC=1, max fps 60), frame times on (another order than step 3a's run).
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_s3b SHOTS=50 SET=s3b sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
BINDIR=./test_mt3 SHOTS=187 SET=f187_mt3 sh /tmp/bench.sh
BINDIR=./test_s3a SHOTS=187 SET=f187_s3a sh /tmp/bench.sh
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_s3a \
	B="+set vid_maxfps 60" B_BIN=./test_s3b \
	C="+set vid_maxfps 60" C_BIN=./test_mt3 sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
