#!/bin/sh
# The strict FP QMP unit: the reader's failure paths and the early-exit path, driven by a stand-in emulator.
#
# The stand-in emulator is a program, so an execution allow-list that created it cannot also start it: `build` emits it and
# records where it went, and `run` starts only that already-existing file. With no argument the script emits it and runs the unit
# in one go, which is what an ordinary host and the repository's own `make test` do.
set -eu
cd "$(dirname "$0")/.."
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
    chmod 0700 "$emulator"
fi
if [ "$stage" = build ]; then
    printf 'STAGED fake-qemu %s\n' "$emulator"
    exit 0
fi
if [ ! -f "$emulator" ]; then
    echo "native_fp_qmp_test.sh: no stand-in emulator at $emulator" >&2
    exit 2
fi
GTOS_FAKE_QEMU=$emulator "$python" -B -m unittest discover -s tests -p 'native_fp_qmp_test.py'
