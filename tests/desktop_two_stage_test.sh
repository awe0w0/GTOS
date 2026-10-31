#!/bin/sh
# The desktop unit's two steps, as the suite runs them.
#
# The unit is built by one step and started by another, and the only thing the second step may start is a copy the first one published
# and recorded. This test drives exactly that interface in a directory of its own, and it holds on a host without an i386 executor: the
# wrapper names the width it is starting, so a run that skipped a width cannot pass as a run that did both.
#
# What it measures: the supply refusals (nothing published, a digest that no longer matches, a strict run with no named executor), the
# two widths actually starting, and the program's own status arriving unchanged — 2 is 2, and never a refusal.
set -eu
cd "$(dirname "$0")/.."
failures=0
check() {
    if [ "$1" = 1 ]; then
        echo "ok   $2"
    else
        echo "FAIL $2"
        failures=$((failures + 1))
    fi
}

root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop-two-stage.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM
export GTOS_HOST_PUBLISH_ROOT=$root/publish
export GTOS_PUBLISH_MANIFEST=$root/manifest.txt
export GTOS_PUBLISH_RECORD=$root/record.txt
mkdir -p "$GTOS_HOST_PUBLISH_ROOT"

start_run() {
    GTOS_HOST_PUBLISH_ROOT=$1/publish GTOS_PUBLISH_MANIFEST=$1/manifest.txt GTOS_PUBLISH_RECORD=$1/record.txt \
        ./tests/desktop_test.sh run
}

# Starting before anything was published is a supply refusal, not a passing run: there is no manifest, so nothing says what would run.
status=0
start_run "$root" >"$root/before.log" 2>&1 || status=$?
[ "$status" -eq 70 ] && before=1 || before=0
check "$before" "a run with nothing published is refused with the supply status (got $status)"

# A strict run refuses to fall back: without a named executor the 32-bit width cannot be started, and saying so is the answer.
status=0
GTOS_DESKTOP_STRICT=1 GTOS_HOST_PUBLISH_ROOT=$root/publish GTOS_PUBLISH_MANIFEST=$root/manifest.txt \
    GTOS_PUBLISH_RECORD=$root/record.txt ./tests/desktop_test.sh run >"$root/strict.log" 2>&1 || status=$?
[ "$status" -eq 70 ] && strict=1 || strict=0
check "$strict" "a strict run with no i386 executor is refused with the supply status (got $status)"

# The build step publishes both images and records them; it must not start either one.
status=0
./tests/desktop_test.sh build >"$root/build.log" 2>&1 || status=$?
[ "$status" -eq 0 ] && built=1 || built=0
check "$built" "the build step publishes without starting (rc=$status)"
recorded=$(grep -c '^desktop\(32\|64\)=' "$GTOS_PUBLISH_MANIFEST" 2>/dev/null || echo 0)
check "$([ "$recorded" -ge 2 ] && echo 1 || echo 0)" "both images are recorded in the manifest ($recorded entries)"
outside=$(grep '^desktop\(32\|64\)=' "$GTOS_PUBLISH_MANIFEST" | cut -d= -f2- | grep -cv "^$GTOS_HOST_PUBLISH_ROOT/" || true)
check "$([ "$outside" -eq 0 ] && echo 1 || echo 0)" "every recorded image is inside the publish root ($outside outside)"
started=$(grep -c 'starting the' "$root/build.log" || true)
check "$([ "$started" -eq 0 ] && echo 1 || echo 0)" "the build step starts neither width ($started starts)"

# The run step starts both widths, and says which ones it started.
status=0
./tests/desktop_test.sh run >"$root/run.log" 2>&1 || status=$?
[ "$status" -eq 0 ] && ran=1 || ran=0
check "$ran" "the run step passes with the published copies (rc=$status)"
widths=$(grep -c 'starting the .*-bit image' "$root/run.log" || true)
check "$([ "$widths" -eq 2 ] && echo 1 || echo 0)" "both widths were started ($widths starts)"

# A published copy that is no longer the image the manifest recorded is not the image the build published, so the run step refuses it.
first=$(grep '^desktop32=' "$GTOS_PUBLISH_MANIFEST" | cut -d= -f2-)
# The publication is read-only, because a run must not be able to replace it; this test is playing the part of something that can.
chmod u+w "$first"
printf '\001' >> "$first"
status=0
start_run "$root" >"$root/tampered.log" 2>&1 || status=$?
[ "$status" -eq 70 ] && tampered=1 || tampered=0
check "$tampered" "a copy whose bytes changed is refused with the supply status (got $status)"

# The program's own status arrives unchanged. The test body is built to return it; if the wrapper mapped 2 to 70, this is where it shows.
for code in 2 7 9; do
    case_root=$root/status-$code
    mkdir -p "$case_root/publish"
    status=0
    GTOS_DESKTOP_EXIT_CODE=$code GTOS_HOST_PUBLISH_ROOT=$case_root/publish GTOS_PUBLISH_MANIFEST=$case_root/manifest.txt \
        GTOS_PUBLISH_RECORD=$case_root/record.txt ./tests/desktop_test.sh build >"$case_root/build.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        check 0 "the build step for status $code (rc=$status)"
        continue
    fi
    status=0
    start_run "$case_root" >"$case_root/run.log" 2>&1 || status=$?
    [ "$status" -eq "$code" ] && same=1 || same=0
    check "$same" "a program that returns $code is reported as $code (got $status)"
done

if [ "$failures" -ne 0 ]; then
    echo "desktop_two_stage_test: $failures failure(s)"
    exit 1
fi
echo "desktop_two_stage_test: all checks pass"
