#!/bin/sh
# The supply contract: a published image keeps the identity it was published with, a launcher loads only published images, and each
# refusal names the obligation it is refusing.
#
# Single-condition negative controls, each with its own valid precondition, so none of them can pass because some earlier,
# unrelated check already failed:
#   1. an unrecorded read-only file outside the publish root is refused before it can be loaded;
#   2. a record that is configured but absent is refused instead of skipping verification;
#   3. an unrecorded file inside the publish root is refused;
#   4. a recorded image that has become writable is refused;
#   5. a path that only looks like it is under the publish root ("<root>/../outside/x") is refused on resolved paths;
#   6. a same-size byte change is refused even though the size still matches;
#   7. a truncated image is refused by size;
# and one positive control: a really published image verifies and runs.
#
# Every refusal is observed from the target side as well: the would-be image writes a marker when it starts, so a refusal that
# still started the program cannot pass as a refusal. Only the positive control may leave a marker.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/gtos-publish-identity.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
failures=0
expect_refusal() {
    name=$1 expected_fragment=$2
    shift 2
    set +e
    message=$("$@" 2>&1 >/dev/null)
    status=$?
    set -e
    if [ "$status" -ne 2 ]; then
        echo "FAIL $name: status $status, expected 2 ($message)" >&2
        failures=$((failures + 1))
        return 0
    fi
    case $message in
        *"$expected_fragment"*) echo "ok $name" ;;
        *) echo "FAIL $name: refusal did not name '$expected_fragment': $message" >&2; failures=$((failures + 1)) ;;
    esac
}

export GTOS_HOST_PUBLISH_ROOT=$work/publish
export GTOS_PUBLISH_RECORD=$work/publish-record.txt
: > "$GTOS_PUBLISH_RECORD"
mkdir -p "$GTOS_HOST_PUBLISH_ROOT" "$work/outside"
GTOS_HOST_LAUNCHER=/usr/bin/dash
export GTOS_HOST_LAUNCHER
export GTOS_SPAWN_MARKER=$work/spawned.txt
: > "$GTOS_SPAWN_MARKER"

# make_image <path> <tag>: a program that records that it started, and prints its tag.
make_image() {
    image_path=$1 image_tag=$2
    printf '#!/bin/sh\nprintf "%%s\\n" "%s" >> "$GTOS_SPAWN_MARKER"\nprintf "%%s\\n" "%s"\n' "$image_tag" "$image_tag" > "$image_path"
    chmod 0555 "$image_path"
}
assert_no_spawn() {
    name=$1
    if [ -s "$GTOS_SPAWN_MARKER" ]; then
        echo "FAIL $name: the refused image was started anyway: $(cat "$GTOS_SPAWN_MARKER")" >&2
        failures=$((failures + 1))
    else
        echo "ok $name: the image was not started"
    fi
    : > "$GTOS_SPAWN_MARKER"
}
assert_spawn() {
    name=$1 expected_tag=$2
    if grep -qx "$expected_tag" "$GTOS_SPAWN_MARKER"; then
        echo "ok $name: the image was started"
    else
        echo "FAIL $name: no start was observed for $expected_tag: $(cat "$GTOS_SPAWN_MARKER")" >&2
        failures=$((failures + 1))
    fi
    : > "$GTOS_SPAWN_MARKER"
}

# Control 1: a read-only regular file outside the publish root was never published by this run.
make_image "$work/outside/image" outside-image
expect_refusal 'a read-only file outside the publish root is refused' 'is not recorded as published' run_host_binary "$work/outside/image"
assert_no_spawn 'a read-only file outside the publish root is refused'

# Positive control: a really published image verifies by identity and runs.
make_image "$work/fixture.sh" published-image-runs
chmod 0755 "$work/fixture.sh"
published=$(publish_host_binary "$work/fixture.sh")
size=$(stat -c %s "$published")
digest=$(sha256sum "$published" | cut -d' ' -f1)
if verify_published "$published" "$digest" "$size"; then echo 'ok a published image verifies by size and digest'; else
    echo 'FAIL a published image did not verify' >&2; failures=$((failures + 1)); fi
if run_published_binary "$published" > "$work/ran.txt" 2>&1; then
    grep -q 'published-image-runs' "$work/ran.txt" && echo 'ok the published image runs through the launcher' || {
        echo 'FAIL the published image produced no output' >&2; failures=$((failures + 1)); }
else
    echo 'FAIL the published image did not run' >&2; failures=$((failures + 1)); fi
assert_spawn 'the published image runs through the launcher' published-image-runs

# Control 2: GTOS_PUBLISH_RECORD names a file that does not exist. The old form skipped verification in exactly this case, so a
# missing record file was indistinguishable from a successful check; the image with a valid identity must still not start.
missing_record=$work/absent-record.txt
if [ -e "$missing_record" ]; then
    echo 'FAIL the missing-record control has a record file, so it does not test a missing record' >&2; failures=$((failures + 1)); fi
GTOS_PUBLISH_RECORD=$missing_record
expect_refusal 'a configured but missing publish record is refused before the image starts' 'there is no record at' run_host_binary "$published"
assert_no_spawn 'a configured but missing publish record is refused before the image starts'
GTOS_PUBLISH_RECORD=$work/publish-record.txt
export GTOS_PUBLISH_RECORD

# Control 3: a read-only file inside the publish root that this run did not publish is still unrecorded.
make_image "$GTOS_HOST_PUBLISH_ROOT/unpublished-image" in-root-unpublished
expect_refusal 'an unrecorded image inside the publish root is refused' 'is not recorded as published' run_host_binary "$GTOS_HOST_PUBLISH_ROOT/unpublished-image"
assert_no_spawn 'an unrecorded image inside the publish root is refused'

# Control 4: a recorded image that has become writable. The bytes still match the record, so only the mode check stands between a
# writable file and the launcher.
writable=$(publish_host_binary "$work/fixture.sh")
chmod 0755 "$writable"
if [ "$(stat -c %a "$writable")" = '755' ]; then
    echo 'ok the recorded image is writable for this control'
else
    echo 'FAIL the recorded image is not writable, so this control does not test a writable image' >&2; failures=$((failures + 1)); fi
expect_refusal 'a recorded but writable image is refused' 'read-only published image' run_host_binary "$writable"
assert_no_spawn 'a recorded but writable image is refused'

# Control 5: a path whose characters start with the publish root but whose resolved path leaves it. The record names the alias
# spelling, so only the resolved-path check can refuse it.
make_image "$work/outside/alias-image" alias-image
alias_size=$(stat -c %s "$work/outside/alias-image")
alias_digest=$(sha256sum "$work/outside/alias-image" | cut -d' ' -f1)
alias_spelling=$GTOS_HOST_PUBLISH_ROOT/../outside/alias-image
printf '%s %s %s\n' "$alias_spelling" "$alias_size" "$alias_digest" >> "$GTOS_PUBLISH_RECORD"
if [ "$(realpath "$alias_spelling")" = "$(realpath "$work/outside/alias-image")" ]; then
    echo 'ok the alias spelling resolves outside the publish root'
else
    echo 'FAIL the alias spelling does not resolve outside the publish root' >&2; failures=$((failures + 1)); fi
expect_refusal 'a path that only looks like it is under the publish root is refused' 'only load a published image' run_host_binary "$alias_spelling"
assert_no_spawn 'a path that only looks like it is under the publish root is refused'

# Control 6: a same-size byte change. The size still matches, so only the digest can catch it.
drifted=$(publish_host_binary "$work/fixture.sh")
drifted_size=$(stat -c %s "$drifted")
drifted_digest=$(sha256sum "$drifted" | cut -d' ' -f1)
chmod 0755 "$drifted"
printf 'X' | dd of="$drifted" bs=1 seek=0 conv=notrunc 2>/dev/null
chmod 0555 "$drifted"
if [ "$(stat -c %s "$drifted")" = "$drifted_size" ]; then
    echo 'ok the drifted image still has its original size'
else
    echo 'FAIL the drifted image changed size, so this control does not test a byte swap' >&2; failures=$((failures + 1)); fi

# Control 7: a truncated image, caught by size.
truncated=$(publish_host_binary "$work/fixture.sh")
truncated_size=$(stat -c %s "$truncated")
truncated_digest=$(sha256sum "$truncated" | cut -d' ' -f1)
chmod 0755 "$truncated"
: > "$truncated"
chmod 0555 "$truncated"
expect_refusal 'a truncated image is refused by size' 'size drifted' verify_published "$truncated" "$truncated_digest" "$truncated_size"

# Control 6, now that control 7 has its own precondition: the byte swap must be refused by digest, not by size.
expect_refusal 'a same-size byte change is refused by digest' 'bytes drifted' verify_published "$drifted" "$drifted_digest" "$drifted_size"
expect_refusal 'a same-size byte change is refused before the image is started' 'bytes drifted' run_published_binary "$drifted"
assert_no_spawn 'a same-size byte change is refused before the image is started'

# A refusal must not damage what was already published: the first image still verifies and still runs afterwards.
if verify_published "$published" "$digest" "$size"; then echo 'ok the refusals left the published image intact'; else
    echo 'FAIL a refusal changed the published image' >&2; failures=$((failures + 1)); fi
if run_published_binary "$published" > "$work/ran-again.txt" 2>&1; then
    echo 'ok the published image still runs after the refusals'
else
    echo 'FAIL the published image no longer runs after the refusals' >&2; failures=$((failures + 1)); fi
assert_spawn 'the published image still runs after the refusals' published-image-runs

if [ "$failures" -ne 0 ]; then
    echo "publish identity: $failures failure(s)" >&2
    exit 1
fi
echo 'publish identity: all checks passed'
