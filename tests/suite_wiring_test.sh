#!/bin/sh
# Which unit scripts tests/run.sh actually runs.
#
# Eight of the scripts in tests/ are not wired into tests/run.sh. What each one needs was measured rather than guessed: seven native_*
# units take artifact or SDK paths as arguments, and wuffs_gif_probe_test.sh takes an output path.
#
# Six units were measured and wired in this window. memory_bootstrap_test.sh needed nothing (486 checks, no failures).
# settings_sanitizer_test.sh, boot_log_host_test.sh and live_storage_host_test.sh needed nothing once they were given build/run steps,
# and were measured passing against their published images: the boot log at 541146 checks under each of two optimisation levels, and
# live storage at 3812 checks for its 64-bit module and 3826 for its freestanding 32-bit one. host_runner_test.sh needed the supply
# paths, which run.sh now gives its two steps. runtime_test.sh had a staged interface and no caller, and its twelve cases now run
# against a published image.
#
# Wiring the rest is separate work that has to verify each one, so this test does not pretend they pass. What it does is record the
# set: a script that is neither run nor recorded here fails the test, and a recorded script that has since been wired or deleted
# fails it too, so the list cannot drift either way unnoticed.
set -eu
cd "$(dirname "$0")/.."
failures=0

recorded_not_run='native_address_test.sh
native_gfx_types_test.sh
native_resource_host_test.sh
native_surface_host_test.sh
native_window_desktop_test.sh
native_window_host_test.sh
native_window_sdk_test.sh
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
