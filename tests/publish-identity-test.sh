#!/bin/sh
# The publish identity rule, exercised against the real functions in tests/host-runner.sh.
#
# The rule has two halves: an image is started only if the manifest records the digest of its bytes, and a publication is only a
# publication if the run that produced it cannot rewrite it. Both halves are checked here with their refusals, and each refusal is
# paired with a control that proves the same mechanism accepts a good image, so a check that always refuses cannot pass this file.
set -eu
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-publish-identity.XXXXXX")
trap 'rm -rf "$root"' EXIT
export GTOS_PUBLISH_MANIFEST="$root/manifest.txt"
export GTOS_HOST_PUBLISH_ROOT="$root/publish"
export GTOS_RUN_ROOT="$root/run"
mkdir -p "$GTOS_HOST_PUBLISH_ROOT" "$GTOS_RUN_ROOT"
: > "$GTOS_PUBLISH_MANIFEST"
runner=${1:?usage: publish-identity-test.sh <host-runner.sh>}
. "$runner"
failures=0
check() {
    if [ "$2" = "$3" ]; then printf 'ok   %s (%s)\n' "$1" "$3"; else printf 'FAIL %s: expected %s, got %s\n' "$1" "$3" "$2"; failures=$((failures+1)); fi
}

printf 'first image\n' > "$root/source-a"
stage_binary desktop64 "$root/source-a" > "$root/stage.log"
published=$(staged_binary desktop64)
check 'published image is returned' "$(cat "$published")" 'first image'
check 'the manifest records a digest' "$(grep -c '^desktop64\.sha256=[0-9a-f]\{64\}$' "$GTOS_PUBLISH_MANIFEST")" 1

# The very failure the digest exists for: the bytes at the recorded path change after publication.
chmod u+w "$published"
printf 'second image\n' > "$published"
status=0; staged_binary desktop64 > "$root/out.txt" 2> "$root/err.txt" || status=$?
check 'a tampered image is refused' "$status" 2
check 'the refusal says which image it was' "$(grep -c 'is not the image that was published' "$root/err.txt")" 1
check 'nothing is printed for the caller to start' "$(wc -c < "$root/out.txt")" 0

# A manifest from before the digest existed. There is no setting that trades the refusal for a start.
printf 'old=%s\n' "$root/source-a" > "$root/old-manifest.txt"
status=0; GTOS_PUBLISH_MANIFEST="$root/old-manifest.txt" staged_binary old > /dev/null 2> "$root/err2.txt" || status=$?
check 'an unrecorded image is refused' "$status" 2
check 'the refusal names the missing digest' "$(grep -c 'no recorded digest' "$root/err2.txt")" 1
status=0; GTOS_PUBLISH_MANIFEST="$root/old-manifest.txt" GTOS_PUBLISH_ALLOW_UNRECORDED=1 staged_binary old > "$root/out3.txt" 2> "$root/err3.txt" || status=$?
check 'the old allowance cannot start an unrecorded image either' "$status" 2
check 'the allowance is not honoured with a warning' "$(grep -c 'WARNING' "$root/err3.txt")" 0
check 'nothing is printed for the caller to start' "$(wc -c < "$root/out3.txt")" 0

# A publish root the run can write is not a publication. The control is the same root accepted once it is outside the run root.
printf 'third image\n' > "$root/source-b"
mkdir -p "$GTOS_RUN_ROOT/inside"
status=0; GTOS_HOST_PUBLISH_ROOT="$GTOS_RUN_ROOT/inside" publish_host_binary "$root/source-b" > /dev/null 2> "$root/err4.txt" || status=$?
check 'a publish root inside the run root is refused' "$status" 2
check 'the refusal explains why' "$(grep -c 'inside the run' "$root/err4.txt")" 1
status=0; GTOS_HOST_PUBLISH_ROOT="$GTOS_RUN_ROOT" publish_host_binary "$root/source-b" > /dev/null 2> "$root/err5.txt" || status=$?
check 'a publish root equal to the run root is refused' "$status" 2
status=0; publish_host_binary "$root/source-b" > "$root/out6.txt" 2> "$root/err6.txt" || status=$?
check 'a publish root outside the run root is accepted' "$status" 0
check 'the accepted publication printed one path' "$(wc -l < "$root/out6.txt")" 1
check 'the accepted publication has no diagnostic' "$(wc -c < "$root/err6.txt")" 0

# A path that only looks outside the run root is inside it once resolved.
mkdir -p "$root/outside"
status=0; GTOS_HOST_PUBLISH_ROOT="$GTOS_RUN_ROOT/../outside" publish_host_binary "$root/source-b" > /dev/null 2> "$root/err7.txt" || status=$?
check 'a publish root that resolves outside is accepted' "$status" 0

if [ "$failures" -ne 0 ]; then printf '%s cases failed\n' "$failures"; exit 1; fi
printf 'all publish-identity cases passed\n'
