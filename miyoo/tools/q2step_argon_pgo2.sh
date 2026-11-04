#!/bin/sh
# On the Miyoo (branch Argon, step 2):
# 1. 25 screenshots of test_argon (with sw_rast.c float fix) and test_argon_pgo (PGO=use + -O3 + LTO).
# 2. Two rounds of A/B/C as in play (VSYNC=1, vid_maxfps 60, frame times on):
#    A = test_s3c (baseline), B = test_argon, C = test_argon_pgo.
# 3. One round of unconstrained timedemos (VSYNC=0) to see raw speed above the 59.67 Hz ceiling.
: > /tmp/q2step_shots.txt
BINDIR=./test_argon SHOTS=50 SET=argon sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
BINDIR=./test_argon_pgo SHOTS=50 SET=argon_pgo sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 \
	A="+set vid_maxfps 60" A_BIN=./test_s3c \
	B="+set vid_maxfps 60" B_BIN=./test_argon \
	C="+set vid_maxfps 60" C_BIN=./test_argon_pgo \
	sh /tmp/bench_ab.sh
cp /tmp/q2bench_ab.txt /tmp/q2bench_ab_vsync1.txt
VSYNC=0 ROUNDS=2 \
	A=" " A_BIN=./test_s3c \
	B=" " B_BIN=./test_argon \
	C=" " C_BIN=./test_argon_pgo \
	sh /tmp/bench_ab.sh
cp /tmp/q2bench_ab.txt /tmp/q2bench_ab_vsync0.txt
echo done > /tmp/q2step_done
