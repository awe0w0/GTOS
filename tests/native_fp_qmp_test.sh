#!/bin/sh
# The strict FP QMP unit: the reader's failure paths and the early-exit path, driven by a stand-in emulator.
#
# The stand-in emulator is a program, so an execution allow-list that created it cannot also start it: `build` emits it, makes it
# read-only and publishes it with its size and digest recorded; `run` starts only that already-published file, after checking the
# bytes are the ones that were published. With no argument the script emits it and runs the unit in one go, which is what an
# ordinary host and the repository's own `make test` do.
#
# The emulator is never written by the test at the moment it is needed: a file the test creates for itself is a file whose identity
# nothing decided, and the point of the staged form is that the program started in `run` is the program `build` published.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: native_fp_qmp_test.sh [all|build|run]" >&2; exit 2 ;;
esac
work=${GTOS_FAKE_QEMU_DIR:-${TMPDIR:-/tmp}/gtos-fp-qmp-154}
emulator=$work/fake-qemu
python=${PYTHON:-python3}
if [ "$stage" = build ] || [ "$stage" = all ]; then
    mkdir -p "$work"
    "$python" -B tests/native_fp_qmp_test.py --emit-emulator "$emulator"
    chmod 0555 "$emulator"
fi
if [ "$stage" = build ]; then
    stage_binary fake-qemu "$emulator"
    exit 0
fi
if [ "$stage" = run ]; then
    if [ -z "${GTOS_PUBLISH_RECORD:-}" ]; then
        echo "native_fp_qmp_test.sh: the run stage needs GTOS_PUBLISH_RECORD; without it the stand-in has no identity" >&2
        exit 2
    fi
    if [ ! -f "$GTOS_PUBLISH_RECORD" ]; then
        echo "native_fp_qmp_test.sh: no publish record at $GTOS_PUBLISH_RECORD; refusing to start an unverified stand-in" >&2
        exit 2
    fi
    emulator=$(staged_binary fake-qemu) || exit 2
    record=$(awk -v path="$emulator" '$1 == path { print $2, $3 }' "$GTOS_PUBLISH_RECORD" | tail -n 1)
    if [ -z "$record" ]; then
        echo "native_fp_qmp_test.sh: $emulator is not recorded as published" >&2
        exit 2
    fi
    verify_published "$emulator" "${record#* }" "${record%% *}" || exit 2
    mode=$(stat -c %a "$emulator" 2>/dev/null || echo '')
    if [ -z "$mode" ] || [ $((0$mode & 0222)) -ne 0 ]; then
        echo "native_fp_qmp_test.sh: the stand-in is writable: $emulator ($mode)" >&2
        exit 2
    fi
fi
if [ ! -f "$emulator" ]; then
    echo "native_fp_qmp_test.sh: no stand-in emulator at $emulator" >&2
    exit 2
fi
GTOS_FAKE_QEMU=$emulator "$python" -B -m unittest discover -s tests -p 'native_fp_qmp_test.py'
