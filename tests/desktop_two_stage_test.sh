#!/bin/sh
# The desktop unit's two steps, as the suite runs them.
#
# The unit is built by one step and started by another, and the only thing the second step may start is a copy the first one published
# and recorded. This test drives exactly that interface in a directory of its own: it does not need an i386 executor, because the run
# step falls back the way the single step does, so what it measures is the publication boundary rather than the emulator.
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

# Starting before anything was published is a supply refusal, not a passing run: there is no manifest, so nothing says what would run.
status=0
./tests/desktop_test.sh run >"$root/before.log" 2>&1 || status=$?
[ "$status" -eq 70 ] && before=1 || before=0
check "$before" "a run with nothing published is refused with the supply status (got $status)"

# The build step publishes both images and records them; it must not start either one.
status=0
./tests/desktop_test.sh build >"$root/build.log" 2>&1 || status=$?
[ "$status" -eq 0 ] && built=1 || built=0
check "$built" "the build step publishes without starting (rc=$status)"
recorded=$(grep -c '^desktop\(32\|64\)=' "$GTOS_PUBLISH_MANIFEST" 2>/dev/null || echo 0)
check "$([ "$recorded" -ge 2 ] && echo 1 || echo 0)" "both images are recorded in the manifest ($recorded entries)"
outside=$(grep '^desktop\(32\|64\)=' "$GTOS_PUBLISH_MANIFEST" | cut -d= -f2- | grep -cv "^$GTOS_HOST_PUBLISH_ROOT/" || true)
check "$([ "$outside" -eq 0 ] && echo 1 || echo 0)" "every recorded image is inside the publish root ($outside outside)"

# The run step starts only those copies and answers with the program's own status.
status=0
./tests/desktop_test.sh run >"$root/run.log" 2>&1 || status=$?
[ "$status" -eq 0 ] && ran=1 || ran=0
check "$ran" "the run step starts the published copies (rc=$status)"

# A published copy that is no longer the image the manifest recorded is not the image the build published, so the run step refuses it.
first=$(grep '^desktop32=' "$GTOS_PUBLISH_MANIFEST" | cut -d= -f2-)
# The publication is read-only, because a run must not be able to replace it; this test is playing the part of something that can.
chmod u+w "$first"
printf '\001' >> "$first"
status=0
./tests/desktop_test.sh run >"$root/tampered.log" 2>&1 || status=$?
[ "$status" -eq 70 ] && tampered=1 || tampered=0
check "$tampered" "a copy whose bytes changed is refused with the supply status (got $status)"

if [ "$failures" -ne 0 ]; then
    echo "desktop_two_stage_test: $failures failure(s)"
    exit 1
fi
echo "desktop_two_stage_test: all checks pass"
