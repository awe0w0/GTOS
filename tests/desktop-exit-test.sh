#!/bin/sh
# The started program's exit status must reach the caller unchanged.
#
# The desktop suite used to end a run with `|| exit 2`, so a program that returned 2, a program the supply refused, and a program that
# crashed were all reported as 2. This file pins the two facts that make the status meaningful:
#
#   * a published image's own status is returned verbatim by run_published_binary;
#   * a refusal to start returns the supply status, and the image is not started at all.
#
# "Not started" is checked by a side effect the image writes and by the same image writing it when it is started: a refusal that also
# counts as a start would leave the marker behind, and an always-refusing supply would not leave it in the control.
set -eu
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop-exit.XXXXXX")
trap 'rm -rf "$root"' EXIT
export GTOS_PUBLISH_MANIFEST="$root/manifest.txt"
# The publish step binds each copy's path, size and digest into this record; the start step refuses anything it does not name.
export GTOS_PUBLISH_RECORD="$root/publish-record.txt"
export GTOS_HOST_PUBLISH_ROOT="$root/publish"
export GTOS_RUN_ROOT="$root/run"
mkdir -p "$GTOS_HOST_PUBLISH_ROOT" "$GTOS_RUN_ROOT"
: > "$GTOS_PUBLISH_MANIFEST"
: > "$GTOS_PUBLISH_RECORD"
runner=${1:?usage: desktop-exit-test.sh <host-runner.sh>}
. "$runner"
failures=0
check() {
    if [ "$2" = "$3" ]; then printf 'ok   %s (%s)\n' "$1" "$3"; else printf 'FAIL %s: expected %s, got %s\n' "$1" "$3" "$2"; failures=$((failures+1)); fi
}

# An image that writes a marker and then returns the status it was given.
image() {
    name=$1
    status=$2
    printf '#!/bin/sh\nprintf started > "%s/marker-%s"\nexit %s\n' "$root" "$name" "$status" > "$root/$name"
    chmod 0755 "$root/$name"
}

image exits-42 42
stage_binary desktop64 "$root/exits-42" > /dev/null
published=$(staged_binary desktop64)
status=0; run_published_binary "$published" > /dev/null 2> "$root/err.txt" || status=$?
check 'a published image returns its own status' "$status" 42
check 'the image ran, so the marker exists' "$(cat "$root/marker-exits-42")" 'started'

image exits-0 0
stage_binary desktop32 "$root/exits-0" > /dev/null
published=$(staged_binary desktop32)
status=0; run_published_binary "$published" > /dev/null 2>&1 || status=$?
check 'a zero status is returned as zero' "$status" 0

image exits-2 2
stage_binary desktop-sanitized "$root/exits-2" > /dev/null
published=$(staged_binary desktop-sanitized)
status=0; run_published_binary "$published" > /dev/null 2>&1 || status=$?
check 'a program that returns 2 is not confused with a refusal' "$status" 2
check 'and it did run' "$(cat "$root/marker-exits-2")" 'started'

# The refusal case: after the digest check fails, the image must not be started, so its marker must not appear. The replacement is
# the same length as the source and a different byte, so the refusal has to come from the digest rather than from a size check, and
# writing the source's own content back would leave a file the record still names.
image tampered-77 77
stage_binary desktop64 "$root/tampered-77" > /dev/null
published=$(staged_binary desktop64)
chmod u+w "$published"
printf '#!/bin/sh\nprintf started > "%s/marker-tampered-77"\nexit 76\n' "$root" > "$published"
status=0; run_published_binary "$published" > /dev/null 2> "$root/err2.txt" || status=$?
check 'a tampered image is refused' "$status" 2
check 'the refusal is not a run' "$(test -e "$root/marker-tampered-77" && echo present || echo absent)" absent
check 'the refusal says the bytes drifted' "$(grep -c 'bytes drifted' "$root/err2.txt")" 1

# A launch through a launcher must keep the launched program's status too.
image launched-9 9
stage_binary desktop64s "$root/launched-9" > /dev/null
published=$(staged_binary desktop64s)
status=0; GTOS_HOST_LAUNCHER=/bin/sh run_published_binary "$published" > /dev/null 2>&1 || status=$?
check 'a status survives the launcher path' "$status" 9

if [ "$failures" -ne 0 ]; then printf '%s cases failed\n' "$failures"; exit 1; fi
printf 'all desktop exit-status cases passed\n'
