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

# Every staged unit refuses a stage it does not define. Most of the list was given its stages in this window, and a unit with stages
# is exactly the kind that grows an unrefused branch if nobody checks.
for script in elf32_test.sh host_runner_test.sh skia_alpha_probe_test.sh i18n_test.sh native_fp_qmp_test.sh cpu_memory_types_test.sh \
    settings_sanitizer_test.sh boot_log_host_test.sh live_storage_host_test.sh runtime_test.sh native_surface_host_test.sh; do
    check "$script refuses an unknown stage" sh "tests/$script" nonsense
done

# The FP QMP unit keeps its stand-in emulator outside the test tree, so a run with an explicit empty directory is the way to say
# "no emulator here" without depending on whether an earlier build left one behind.
check 'native_fp_qmp_test.sh refuses a run stage with no stand-in emulator' \
    env GTOS_FAKE_QEMU_DIR=/nonexistent-gtos-fp-qmp sh tests/native_fp_qmp_test.sh run

# A staged run without its supply must refuse rather than fall back to building or starting something unrecorded.
for script in elf32_test.sh skia_alpha_probe_test.sh i18n_test.sh cpu_memory_types_test.sh settings_sanitizer_test.sh \
    boot_log_host_test.sh live_storage_host_test.sh runtime_test.sh; do
    check "$script refuses a run stage without its supply" \
        env -u GTOS_PUBLISH_MANIFEST -u GTOS_HOST_PUBLISH_ROOT -u GTOS_PUBLISH_RECORD -u ELF32_I386_LAUNCHER \
        sh "tests/$script" run
done

# The surface unit's run stage reads its static evidence from a directory the build stage names, so it refuses to start without one.
check 'native_surface_host_test.sh refuses a run stage with no build directory' \
    env -u SURFACE_BUILD_DIR sh tests/native_surface_host_test.sh run /tmp

# A staged build that was given no output directory must refuse too, or it would write into an empty path.
for script in settings_sanitizer_test.sh boot_log_host_test.sh live_storage_host_test.sh native_surface_host_test.sh; do
    check "$script refuses a build stage with no directory" sh "tests/$script" build
done

# The surface build also refuses to reuse a directory, because the evidence it writes is only meaningful for the sources it was
# built from; reusing a directory would mix two builds under one set of logs.
check 'native_surface_host_test.sh refuses to reuse an artifact directory' \
    sh tests/native_surface_host_test.sh build .

if [ "$failures" -ne 0 ]; then
    echo "stage contract: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS stage contract: unknown stages, unsupplied run stages, build stages with no directory and a reused build directory are refused with status 2'
