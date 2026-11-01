#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's steps 1.3 and 2.1): the 8 -> 32 bpp pass with
# 4 pixels per load (sw_threads 0) and on both threads (sw_threads 1), build $GAME/test_o3lto2,
# against the -O3 + LTO build before it ($GAME/test_o3lto). Shots of both, then two rounds.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
BINDIR=./test_o3lto2 SHOTS=50 SET=copy4 EXTRA="+set sw_threads 0" sh /tmp/bench.sh
cp /tmp/q2bench.txt /tmp/q2step_shots.txt
BINDIR=./test_o3lto2 SHOTS=50 SET=copymt EXTRA="+set sw_threads 1" sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
FRAMES=1 ROUNDS=2 A= A_BIN=./test_o3lto B="+set sw_threads 0" B_BIN=./test_o3lto2 \
	C="+set sw_threads 1" C_BIN=./test_o3lto2 sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
