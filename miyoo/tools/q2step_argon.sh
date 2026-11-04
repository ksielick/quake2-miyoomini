#!/bin/sh
# On the Miyoo (branch Argon):
# 1. 25 screenshots from demo1 + demo2 (SHOTS=50 SET=argon) for bit-exact verification against ref.
# 2. Two rounds of A/B benchmark against test_s3c (VSYNC=1, vid_maxfps 60, frame times on).
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_argon SHOTS=50 SET=argon sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_s3c \
	B="+set vid_maxfps 60" B_BIN=./test_argon sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
