#!/bin/sh
# The supply contract: a published image keeps the identity it was published with, a launcher loads only published images, and each
# refusal names the obligation it is refusing.
#
# Four single-condition negative controls, each with its own valid precondition, so none of them can pass because some earlier,
# unrelated check already failed:
#   1. a read-only regular file outside the publish root is refused before it can be loaded;
#   2. a same-size byte change is refused even though the size still matches;
#   3. a truncated image is refused by size;
#   4. an unpublished writable target is refused;
# and one positive control: a really published image verifies and runs.
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

# Control 1: a read-only regular file outside the publish root was never published by this run.
printf '#!/bin/sh\necho outside\n' > "$work/outside/image"
chmod 0555 "$work/outside/image"
expect_refusal 'a read-only file outside the publish root is refused' 'only load a published image' run_host_binary "$work/outside/image"

# Positive control: a really published image verifies by identity and runs.
printf '#!/bin/sh\necho published-image-runs\n' > "$work/fixture.sh"
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

# Control 2: a same-size byte change. The size still matches, so only the digest can catch it.
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

# Control 3: a truncated image, caught by size.
truncated=$(publish_host_binary "$work/fixture.sh")
truncated_size=$(stat -c %s "$truncated")
truncated_digest=$(sha256sum "$truncated" | cut -d' ' -f1)
chmod 0755 "$truncated"
: > "$truncated"
chmod 0555 "$truncated"
expect_refusal 'a truncated image is refused by size' 'size drifted' verify_published "$truncated" "$truncated_digest" "$truncated_size"

# Control 2, now that control 3 has its own precondition: the byte swap must be refused by digest, not by size.
expect_refusal 'a same-size byte change is refused by digest' 'bytes drifted' verify_published "$drifted" "$drifted_digest" "$drifted_size"
expect_refusal 'a same-size byte change is refused before the image is started' 'bytes drifted' run_published_binary "$drifted"

# Control 4: an unpublished writable target.
printf '#!/bin/sh\necho writable\n' > "$work/outside/writable"
chmod 0755 "$work/outside/writable"
expect_refusal 'an unpublished writable target is refused' 'only load a published image' run_host_binary "$work/outside/writable"

if [ "$failures" -ne 0 ]; then
    echo "publish identity: $failures failure(s)" >&2
    exit 1
fi
echo 'publish identity: all checks passed'
