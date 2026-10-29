#!/bin/sh
# A program for tests/host_runner_test.sh to publish and start. It names itself, repeats the arguments it was given and ends with
# the status it was told to use, so a test can tell which copy ran, that arguments survived, and that the status was forwarded.
echo "PROBE-A"
echo "PROBE-ARGS $*"
status=${GTOS_PROBE_STATUS:-0}
exit "$status"
