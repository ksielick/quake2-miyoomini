#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 2.3): surface caches built on both threads,
# build $GAME/test_mt2. Shots, then two rounds against the build before it ($GAME/test_mt)
# as in play (VSYNC=1, max fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_mt2 SHOTS=50 SET=mt2 sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_mt \
	B="+set vid_maxfps 60" B_BIN=./test_mt2 sh /tmp/bench_ab.sh
for f in /tmp/q2bench_demo1.log /tmp/q2bench_demo2.log; do grep "fps shown" $f; done > /tmp/q2step_i8check.log
echo done > /tmp/q2step_done
