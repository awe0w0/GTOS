#!/bin/sh
# Tests for the host program supply: what it publishes, what it starts, and what it refuses before it starts anything.
#
# The supply has two steps, so its test has two steps. `prepare` runs where the artifacts can be written: it checks every refusal,
# publishes the probe, then replaces the probe's source and publishes again, so the two copies differ on purpose. `verify` runs
# where only the published copies may start: it starts the first copy and proves it is still the pre-replacement program, forwards
# arguments and status, and reads the staged keys. Splitting the test is not a convenience: a file created in one step cannot be
# started in that step under an execution allow-list, which is the very condition the supply exists for.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

mode=${1:-all}
work=${TMPDIR:-/tmp}/gtos-host-runner.$$
failures=0
pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1" >&2; failures=$((failures + 1)); }
expect_status() {  # <wanted> <actual> <what>
    if [ "$1" = "$2" ]; then pass "$3 (exit $2)"; else fail "$3 (wanted $1, got $2)"; fi
}
finish() {
    if [ "$failures" -ne 0 ]; then printf 'host runner: %s check(s) failed\n' "$failures" >&2; exit 1; fi
    printf 'host runner PASS\n'
    exit 0
}

# ---------------------------------------------------------------- prepare
if [ "$mode" = prepare ] || [ "$mode" = all ]; then
    work=$work-prepare
    mkdir -p "$work/publish"
    probe=$work/probe.sh
    cp tests/host_runner_probe.sh "$probe"
    chmod 0755 "$probe"

    # Refusals: none of these may start anything.
    status=0; run_host_binary "$work" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a directory is refused'
    status=0; run_host_binary "$work/absent" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a missing file is refused'
    ln -s "$probe" "$work/link"
    status=0; run_host_binary "$work/link" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a symlink is refused'
    status=0; GTOS_HOST_PUBLISH_ROOT=$work/absent publish_host_binary "$probe" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a publish root that does not exist is refused'
    status=0; GTOS_HOST_PUBLISH_ROOT= publish_host_binary "$probe" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'publishing without a publish root is refused'

    # The two publications: the first from the probe as it is, the second after the source has been replaced.
    publish_root=${GTOS_HOST_PUBLISH_ROOT:?prepare needs GTOS_HOST_PUBLISH_ROOT}
    manifest=${GTOS_PUBLISH_MANIFEST:?prepare needs GTOS_PUBLISH_MANIFEST}
    first=$(publish_host_binary "$probe")
    mode_bits=$(stat -c %a "$first")
    if [ $((0$mode_bits & 0222)) -eq 0 ]; then pass "the published copy is read-only ($mode_bits)"; else fail "the published copy is writable ($mode_bits)"; fi
    if [ "$(sha256sum "$probe" | cut -d' ' -f1)" = "$(sha256sum "$first" | cut -d' ' -f1)" ]; then
        pass 'the published copy is byte for byte its source'
    else
        fail 'the published copy differs from its source'
    fi
    printf '#!/bin/sh\necho PROBE-B\n' > "$probe"
    second=$(publish_host_binary "$probe")
    if [ "$(sha256sum "$first" | cut -d' ' -f1)" != "$(sha256sum "$second" | cut -d' ' -f1)" ]; then
        pass 'replacing the source does not change the copy already published'
    else
        fail 'the second publication overwrote or reused the first'
    fi
    printf 'first=%s\nsecond=%s\n' "$first" "$second" >> "$manifest"
    # A launcher may not load a writable image, and may not run without a publish root to bind it to.
    cp tests/host_runner_probe.sh "$work/writable.sh"
    chmod 0755 "$work/writable.sh"
    status=0
    GTOS_HOST_LAUNCHER=/bin/true GTOS_HOST_PUBLISH_ROOT= run_host_binary "$first" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a launcher without a publish root is refused'
    status=0
    GTOS_HOST_LAUNCHER=/bin/true GTOS_HOST_PUBLISH_ROOT=$publish_root run_host_binary "$work/writable.sh" >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a launcher pointed at a writable image is refused'
    printf 'PUBLISHED %s\n' "$first"
    if [ "$mode" = prepare ]; then finish; exit 0; fi
fi

# ---------------------------------------------------------------- verify
if [ "$mode" = verify ] || [ "$mode" = all ]; then
    manifest=${GTOS_PUBLISH_MANIFEST:?verify needs GTOS_PUBLISH_MANIFEST}
    first=$(sed -n 's/^first=//p' "$manifest" | tail -n 1)
    if [ -z "$first" ]; then fail 'the manifest records no first publication'; fi

    output=$(GTOS_PROBE_STATUS=0 run_host_binary "$first" one two)
    case $output in
        *PROBE-A*) pass 'the published copy runs and is the program published before the replacement' ;;
        *PROBE-B*) fail 'the second publication was started instead of the first' ;;
        *) fail "the published copy did not run: $output" ;;
    esac
    case $output in
        *'PROBE-ARGS one two'*) pass 'the arguments reached the program unchanged' ;;
        *) fail "the arguments were not forwarded: $output" ;;
    esac
    # The assignment is exported rather than written on the call: a POSIX shell passes a temporary assignment to a function's own
    # commands unevenly, and the status this check reads must come from the program, not from the shell.
    GTOS_PROBE_STATUS=7
    export GTOS_PROBE_STATUS
    status=0
    run_host_binary "$first" >/dev/null 2>&1 || status=$?
    unset GTOS_PROBE_STATUS
    expect_status 7 "$status" 'the program status is the runner status'

    # The staged form: the key that prepare recorded resolves to the same published copy, and an unrecorded key does not resolve.
    staged=$(GTOS_PUBLISH_MANIFEST=$manifest staged_binary first)
    if [ "$staged" = "$first" ]; then pass 'a staged key resolves to the published copy'; else fail "the staged key resolved to $staged"; fi
    status=0; GTOS_PUBLISH_MANIFEST=$manifest staged_binary absent >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a key that was never staged is refused'
    status=0; GTOS_PUBLISH_MANIFEST=$manifest/absent staged_binary first >/dev/null 2>&1 || status=$?
    expect_status 2 "$status" 'a manifest that does not exist is refused'
    finish
fi

echo "usage: $0 [prepare|verify]" >&2
exit 2
