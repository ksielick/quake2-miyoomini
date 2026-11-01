#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 1.2): the shots of the PGO + LTO build
# ($GAME/test_pgo), then -O2 / -O3 + LTO / PGO + LTO alternated, two rounds, frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
BINDIR=./test_pgo SHOTS=50 SET=pgo sh /tmp/bench.sh
cp /tmp/q2bench.txt /tmp/q2step_shots.txt
rm -f /tmp/q2frames_*
FRAMES=1 ROUNDS=2 A= B= B_BIN=./test_o3lto C= C_BIN=./test_pgo sh /tmp/bench_ab.sh
echo done > /tmp/q2step_done
