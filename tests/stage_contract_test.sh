#!/bin/sh
# The contract every staged unit shares: a stage name nobody defined is refused with usage, and a staged run that was given no
# supply refuses instead of quietly building or starting something. Nothing here compiles or starts a published image: only the
# refusal paths are exercised, which is exactly why they can be checked here at all.
set -eu
cd "$(dirname "$0")/.."
failures=0
check() {
    name=$1
    shift
    set +e
    message=$("$@" 2>&1 >/dev/null)
    status=$?
    set -e
    if [ "$status" -eq 2 ]; then
        echo "ok $name"
    else
        echo "FAIL $name: status $status, expected 2 ($message)" >&2
        failures=$((failures + 1))
    fi
}

# Every staged unit refuses a stage it does not define.
for script in elf32_test.sh host_runner_test.sh skia_alpha_probe_test.sh i18n_test.sh native_fp_qmp_test.sh cpu_memory_types_test.sh; do
    check "$script refuses an unknown stage" sh "tests/$script" nonsense
done

# The FP QMP unit keeps its stand-in emulator outside the test tree, so a run with an explicit empty directory is the way to say
# "no emulator here" without depending on whether an earlier build left one behind.
check 'native_fp_qmp_test.sh refuses a run stage with no stand-in emulator' \
    env GTOS_FAKE_QEMU_DIR=/nonexistent-gtos-fp-qmp sh tests/native_fp_qmp_test.sh run

# A staged run without its supply must refuse rather than fall back to building or starting something unrecorded.
for script in elf32_test.sh skia_alpha_probe_test.sh i18n_test.sh cpu_memory_types_test.sh; do
    check "$script refuses a run stage without its supply" \
        env -u GTOS_PUBLISH_MANIFEST -u GTOS_HOST_PUBLISH_ROOT -u GTOS_PUBLISH_RECORD -u ELF32_I386_LAUNCHER \
        sh "tests/$script" run
done

if [ "$failures" -ne 0 ]; then
    echo "stage contract: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS stage contract: unknown stages and unsupplied run stages are refused with status 2'
