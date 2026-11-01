#!/bin/sh
# On the Miyoo: bench.sh twice, with two settings, results together in /tmp/q2bench_ab.txt.
#   A="+set sw_texture_filtering 0" B="+set sw_texture_filtering 1" sh /tmp/bench_ab.sh [demo ...]
# Copy bench.sh and this file to /tmp first and start it as bench.sh says (cmd_to_run.sh).
OUT=/tmp/q2bench_ab.txt
: > "$OUT"
for set in A B; do
	eval "x=\$$set"
	EXTRA="$x" sh /tmp/bench.sh "$@"
	{ echo "## $set: $x"; cat /tmp/q2bench.txt; } >> "$OUT"
done
