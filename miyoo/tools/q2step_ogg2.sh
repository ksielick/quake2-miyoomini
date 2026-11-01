#!/bin/sh
# On the Miyoo (2026-11-01): the music decoder on its own thread, build $GAME/test_ogg.
# 1. The chunks the music hands to the mixer, hashed (MIYOO_OGG_HASH), decoded in the frame
#    (MIYOO_OGG_THREAD=0) and on the thread: the hash lines must be the same.
# 2. Two rounds of the main build ($GAME, s3c) and test_ogg, music on, as in play (VSYNC=1,
#    max fps 60), frame times on.
# Copy bench.sh, bench_ab.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
: > /tmp/q2step_ogghash.txt
for mode in 0 1; do
	MIYOO_OGG_HASH=1 MIYOO_OGG_THREAD=$mode BINDIR=./test_ogg sh /tmp/bench.sh
	for d in demo1 demo2; do
		grep "^OGG:" /tmp/q2bench_$d.log | sed "s/^/thread=$mode $d /" >> /tmp/q2step_ogghash.txt
	done
done
rm -f /tmp/q2frames_*
VSYNC=1 FRAMES=1 ROUNDS=2 A="+set vid_maxfps 60 +set ogg_enable 1" \
	B="+set vid_maxfps 60 +set ogg_enable 1" B_BIN=./test_ogg sh /tmp/bench_ab.sh
for d in demo1 demo2; do grep "waited" /tmp/q2bench_$d.log; done >> /tmp/q2step_ogghash.txt
echo done > /tmp/q2step_done
