#!/bin/sh
# On the Miyoo (2026-11-01): screenshot sets with the bench's own settings (bench.sh HOME in
# /tmp): the -O2 build twice (the new reference, and whether two runs match), then the builds
# of the speed plan so far. Copy bench.sh and this file to /tmp; start it through cmd_to_run.sh.
: > /tmp/q2step_shots.txt
run() {
	SET=$1; shift
	env "$@" SHOTS=50 SET=$SET sh /tmp/bench.sh
	{ echo "## $SET: $*"; cat /tmp/q2bench.txt; } >> /tmp/q2step_shots.txt
}
run ref2 BINDIR=.
run ref2b BINDIR=.
run o3lto BINDIR=./test_o3lto
run pgo BINDIR=./test_pgo
run copy4 BINDIR=./test_o3lto2 EXTRA="+set sw_threads 0"
run copymt BINDIR=./test_o3lto2 EXTRA="+set sw_threads 1"
echo done > /tmp/q2step_done
