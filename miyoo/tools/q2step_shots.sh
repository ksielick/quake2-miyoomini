#!/bin/sh
# On the Miyoo (2026-11-01): screenshot sets, numbered by the demo's own frame now. The new
# reference: the -O2 build ($GAME/test_o2) with the CPU pass on one thread (sw_threads 0,
# MIYOO_GFX_I8=0), twice; then the threaded build ($GAME/test_mt) with sw_threads 1 and 0.
# Copy bench.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_shots.txt
run() {
	SET=$1; shift
	env "$@" SHOTS=50 SET=$SET sh /tmp/bench.sh
	{ echo "## $SET: $*"; cat /tmp/q2bench.txt; } >> /tmp/q2step_shots.txt
}
run ref3 BINDIR=./test_o2 MIYOO_GFX_I8=0 EXTRA="+set sw_threads 0"
run ref3b BINDIR=./test_o2 MIYOO_GFX_I8=0 EXTRA="+set sw_threads 0"
run mt BINDIR=./test_mt EXTRA="+set sw_threads 1"
run mt0 BINDIR=./test_mt EXTRA="+set sw_threads 0"
echo done > /tmp/q2step_done
