#!/bin/sh
# On the Miyoo (2026-11-01, the speed plan's step 1.2): the shots of the -O3 + LTO build, then
# the PGO profile run of the instrumented build (build_miyoo.sh PGO=gen, in $GAME/test_pgogen;
# its profile goes to /tmp/pgo and is packed as Roms/.pgo.tar for the PC).
# Copy bench.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh (see bench.sh).
GAME=/mnt/SDCARD/Roms/PORTS/Games/Quake2
BINDIR=./test_o3lto SHOTS=50 SET=o3lto sh /tmp/bench.sh
cp /tmp/q2bench.txt /tmp/q2step_shots.txt
rm -rf /tmp/pgo
BINDIR=./test_pgogen sh /tmp/bench.sh
cp /tmp/q2bench.txt /tmp/q2step_pgo.txt
cd /tmp && tar cf "$GAME/../../.pgo.tar" pgo
echo done > /tmp/q2step_done
