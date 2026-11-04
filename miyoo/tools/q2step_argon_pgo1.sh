#!/bin/sh
# On the Miyoo (branch Argon, step 1):
# 1. Capture 25 reference screenshots from baseline test_s3c with current yq2.cfg (SET=ref_s3c).
# 2. Capture 25 screenshots from test_argon (SET=argon).
# 3. Collect PGO profile from test_pgogen on demo1 + demo2 and archive /tmp/pgo to Roms/.pgo.tar.
GAME=/mnt/SDCARD/Roms/PORTS/Games/Quake2
: > /tmp/q2step_shots.txt
BINDIR=./test_s3c SHOTS=50 SET=ref_s3c sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
BINDIR=./test_argon SHOTS=50 SET=argon sh /tmp/bench.sh
cat /tmp/q2bench.txt >> /tmp/q2step_shots.txt
rm -rf /tmp/pgo
BINDIR=./test_pgogen sh /tmp/bench.sh
cp /tmp/q2bench.txt /tmp/q2step_pgo.txt
cd /tmp && tar cf "$GAME/../../.pgo.tar" pgo
echo done > /tmp/q2step_done
