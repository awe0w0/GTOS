#!/bin/sh
# Which unit scripts tests/run.sh actually runs.
#
# Eleven of the scripts in tests/ are not wired into tests/run.sh. What each one needs was measured rather than guessed: seven
# native_* units and runtime_test.sh take artifact or SDK paths as arguments, wuffs_gif_probe_test.sh takes an output path,
# host_runner_test.sh needs the supply's publish root and manifest, and live_storage_host_test.sh builds a host program and starts it
# in the same step, so it cannot pass inside an execution allow-list that only runs files that existed when the step began.
# memory_bootstrap_test.sh needed nothing and was measured at 486 checks with no failures; settings_sanitizer_test.sh and
# boot_log_host_test.sh needed nothing once they were given build/run steps, and both were measured passing against their published
# images, the boot log at 541146 checks under each of two optimisation levels. Those three are now wired.
#
# Wiring the rest is separate work that has to verify each one, so this test does not pretend they pass. What it does is record the
# set: a script that is neither run nor recorded here fails the test, and a recorded script that has since been wired or deleted
# fails it too, so the list cannot drift either way unnoticed.
set -eu
cd "$(dirname "$0")/.."
failures=0

recorded_not_run='host_runner_test.sh
live_storage_host_test.sh
native_address_test.sh
native_gfx_types_test.sh
native_resource_host_test.sh
native_surface_host_test.sh
native_window_desktop_test.sh
native_window_host_test.sh
native_window_sdk_test.sh
runtime_test.sh
wuffs_gif_probe_test.sh'

is_recorded() {
    printf '%s\n' "$recorded_not_run" | grep -qx -- "$1"
}
is_wired() {
    grep -q -- "tests/$1" tests/run.sh
}

missing=''
stale=''
wired_count=0
not_run_count=0
for script in tests/*_test.sh; do
    name=${script##*/}
    if is_wired "$name"; then
        wired_count=$((wired_count + 1))
        if is_recorded "$name"; then stale="$stale $name(now wired)"; fi
    else
        not_run_count=$((not_run_count + 1))
        if ! is_recorded "$name"; then missing="$missing $name"; fi
    fi
done
for name in $recorded_not_run; do
    if [ ! -f "tests/$name" ]; then stale="$stale $name(deleted)"; fi
done

if [ -n "$missing" ]; then
    echo "FAIL a unit script is neither run by tests/run.sh nor recorded here:$missing" >&2
    failures=$((failures + 1))
else
    echo 'ok no unit script is silently unwired'
fi
if [ -n "$stale" ]; then
    echo "FAIL this list records scripts that have changed state:$stale" >&2
    failures=$((failures + 1))
else
    echo 'ok every recorded script is still present and still unwired'
fi
echo "ok tests/run.sh runs $wired_count unit scripts and $not_run_count are recorded as not run"

if [ "$failures" -ne 0 ]; then
    echo "suite wiring: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS suite wiring: every unit script is either run or recorded; this says nothing about whether the recorded ones pass'
