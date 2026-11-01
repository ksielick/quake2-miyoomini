#!/bin/sh
# On the Miyoo (2026-11-01): the 8-bit frame converted by MI_GFX (I8 -> ARGB8888), build
# $GAME/test_i8 (the blit waited for by the pan thread). 1. MIYOO_GFX_CHECK=1: the page read
# back every 50th frame and compared with the CPU's palette lookup. 2. Shots. 3. Two rounds
# against the build before it ($GAME/test_o3lto2, sw_threads 1) as in play: every frame shown
# (VSYNC=1, max fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
BINDIR=./test_i8 MIYOO_GFX_CHECK=1 sh /tmp/bench.sh demo1
grep -E "8-bit frames|gfx I8 check|MI_GFX" /tmp/q2bench_demo1.log > /tmp/q2step_i8check.log
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
BINDIR=./test_i8 SHOTS=50 SET=i8 sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60" A_BIN=./test_o3lto2 \
	B="+set vid_maxfps 60" B_BIN=./test_i8 sh /tmp/bench_ab.sh
for f in /tmp/q2bench_demo1.log /tmp/q2bench_demo2.log; do grep "fps shown" $f; done >> /tmp/q2step_i8check.log
echo done > /tmp/q2step_done
