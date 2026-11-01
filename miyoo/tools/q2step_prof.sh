#!/bin/sh
# On the Miyoo (2026-11-01): a profile of build $GAME/test_s3b, demo frames 60-609, both demos
# (bench.sh PROF; written to $GAME/prof_<demo>.txt, fetched over SMB).
# Copy bench.sh and this file to /tmp; start it through /tmp/cmd_to_run.sh.
BINDIR=./test_s3b PROF=60,550 sh /tmp/bench.sh
echo done > /tmp/q2step_done
