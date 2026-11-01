#!/bin/sh
# On the Miyoo: bench.sh once per setting (A, B, and C, D if given), results together in
# /tmp/q2bench_ab.txt. ROUNDS=2 alternates them twice (A B A B), to see the device's drift.
#   A="+set sw_texture_filtering 0" B="+set sw_texture_filtering 1" sh /tmp/bench_ab.sh [demo ...]
# A_BIN, B_BIN...: that setting runs the build in this directory (bench.sh BINDIR).
# Copy bench.sh and this file to /tmp first and start it as bench.sh says (cmd_to_run.sh).
OUT=/tmp/q2bench_ab.txt
: > "$OUT"
r=0
while [ $r -lt "${ROUNDS:-1}" ]; do
	r=$((r + 1))
	for set in A B C D; do
		eval "x=\${$set+set}"
		[ -n "$x" ] || continue
		eval "x=\$$set; b=\${${set}_BIN}"
		BINDIR="$b" EXTRA="$x" FRAMESET="${set}$r" sh /tmp/bench.sh "$@"
		{ echo "## $set (round $r): $x${b:+ (build $b)}"; cat /tmp/q2bench.txt; } >> "$OUT"
	done
done
