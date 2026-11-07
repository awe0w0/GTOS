#!/bin/sh
# The memory-type unit: the production i386 object is compiled without substitution, and the host mock compiled from the same
# source text is started only as a published read-only copy, through the shared runner.
#
# build compiles both and publishes the mock; run re-checks the production object's dependencies, starts the published copy through
# run_host_binary and lets the Python stage judge what it printed. With no argument the script does both in one go, which is what an
# ordinary host and the repository's own `make test` do.
#
# Two things this entry refuses on purpose, because both used to turn a missing check into a passing one:
#   - a run stage with no publish record: without it nothing says which bytes were published, so there is nothing to verify;
#   - a run stage with no place to write the output it is going to judge, rather than silently reusing the build directory.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: cpu_memory_types_test.sh [all|build|run]" >&2; exit 2 ;;
esac
work=${GTOS_CPU_MEMORY_WORK:-${TMPDIR:-/tmp}/gtos-cpu-memory-154}
python=${PYTHON:-python3}
if [ "$stage" = build ] || [ "$stage" = all ]; then
    "$python" -B tests/cpu_memory_types_test.py --stage build --work "$work"
fi
if [ "$stage" = build ]; then
    stage_binary cpu-memory-mock "$work/cpu_memory_types_mock"
    exit 0
fi
if [ "$stage" = run ]; then
    # A record that is configured but absent, or a binary that is not in it, is not evidence: refuse instead of running something
    # whose identity nobody decided. The old form only checked when the record happened to exist, so a missing file skipped the
    # check entirely.
    if [ -z "${GTOS_PUBLISH_RECORD:-}" ]; then
        echo "cpu_memory_types_test.sh: the run stage needs GTOS_PUBLISH_RECORD; without it the published mock has no identity" >&2
        exit 2
    fi
    if [ ! -f "$GTOS_PUBLISH_RECORD" ]; then
        echo "cpu_memory_types_test.sh: no publish record at $GTOS_PUBLISH_RECORD; refusing to start an unverified mock" >&2
        exit 2
    fi
    executable=$(staged_binary cpu-memory-mock) || exit 2
    log=${GTOS_CPU_MEMORY_RUN_LOG:-}
    if [ -z "$log" ]; then
        echo "cpu_memory_types_test.sh: the run stage needs GTOS_CPU_MEMORY_RUN_LOG to hold the output it judges" >&2
        exit 2
    fi
    mkdir -p "$(dirname "$log")"
    run_host_binary "$executable" > "$log" 2>&1 || {
        status=$?
        echo "cpu_memory_types_test.sh: the mock was not started (status $status); the refusal is in $log" >&2
        exit "$status"
    }
    "$python" -B tests/cpu_memory_types_test.py --stage run --work "$work" --executable "$executable" --run-log "$log"
    exit 0
fi
# `all` on an ordinary host: build, start what was just built through the same runner, and judge its output.
log=$work/run.log
mkdir -p "$work"
run_host_binary "$work/cpu_memory_types_mock" > "$log" 2>&1 || {
    status=$?
    echo "cpu_memory_types_test.sh: the mock failed with status $status; its output is in $log" >&2
    exit "$status"
}
"$python" -B tests/cpu_memory_types_test.py --stage all --work "$work" --executable "$work/cpu_memory_types_mock" --run-log "$log"
