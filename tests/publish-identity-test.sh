#!/bin/sh
# The publish identity rule, exercised against the real functions in tests/host-runner.sh.
#
# Four cases: a published image is returned; a tampered image is refused; a manifest with no digest is refused by default; the same
# manifest is accepted when the caller explicitly allows an unrecorded image. Everything happens in a temporary directory.
set -eu
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-publish-identity.XXXXXX")
trap 'rm -rf "$root"' EXIT
export GTOS_PUBLISH_MANIFEST="$root/manifest.txt"
export GTOS_HOST_PUBLISH_ROOT="$root/publish"
mkdir -p "$GTOS_HOST_PUBLISH_ROOT"
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

# A manifest from before the digest existed.
printf 'old=%s\n' "$root/source-a" > "$root/old-manifest.txt"
GTOS_PUBLISH_MANIFEST="$root/old-manifest.txt" GTOS_PUBLISH_ALLOW_UNRECORDED=0 true
status=0; GTOS_PUBLISH_MANIFEST="$root/old-manifest.txt" staged_binary old > /dev/null 2> "$root/err2.txt" || status=$?
check 'an unrecorded image is refused by default' "$status" 2
check 'the refusal names the missing digest' "$(grep -c 'no recorded digest' "$root/err2.txt")" 1
status=0; GTOS_PUBLISH_MANIFEST="$root/old-manifest.txt" GTOS_PUBLISH_ALLOW_UNRECORDED=1 staged_binary old > "$root/out3.txt" 2> "$root/err3.txt" || status=$?
check 'an unrecorded image is accepted only when allowed' "$status" 0
check 'the allowance warns' "$(grep -c 'WARNING' "$root/err3.txt")" 1

if [ "$failures" -ne 0 ]; then printf '%s cases failed\n' "$failures"; exit 1; fi
printf 'all publish-identity cases passed\n'
