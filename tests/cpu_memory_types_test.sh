#!/bin/sh
# The memory-type unit: the production i386 object is compiled without substitution, and the host mock compiled from the same
# source text is started only as a published read-only copy.
#
# build compiles both and publishes the mock; run re-checks the production object's dependencies and starts the published copy.
# With no argument the script does both in one go, which is what an ordinary host and the repository's own `make test` do.
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
executable=$work/cpu_memory_types_mock
if [ "$stage" = run ]; then
    executable=$(staged_binary cpu-memory-mock)
    # Starting the published copy is not the same as starting the compiled one: check the size and digest the publish step
    # recorded before the mock is allowed to run.
    if [ -n "${GTOS_PUBLISH_RECORD:-}" ] && [ -f "$GTOS_PUBLISH_RECORD" ]; then
        record=$(awk -v path="$executable" '$1 == path { print $2, $3 }' "$GTOS_PUBLISH_RECORD" | tail -n 1)
        if [ -z "$record" ]; then
            echo "cpu_memory_types_test.sh: $executable is not recorded as published" >&2
            exit 2
        fi
        verify_published "$executable" "${record#* }" "${record%% *}" || exit 2
    fi
fi
"$python" -B tests/cpu_memory_types_test.py --stage run --work "$work" --executable "$executable"
