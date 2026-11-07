#!/bin/sh
# The sanitizer source-list check, and the counter-example that gives it meaning.
#
# The check's job is to fail when the ordinary build compiles something the sanitizer build does not, so a passing check on the real
# script is only half the evidence: a check that cannot fail is not a check. This runs it three times — on the real script, on a copy
# with one source removed from the sanitizer list, and on a copy with the same source removed from the ordinary list — and requires
# PASS, FAIL naming the source, and PASS respectively.
set -eu
cd "$(dirname "$0")/.."
python=${PYTHON:-python3}
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-sanitizer-sources.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM
failures=0
check() {
    name=$1 expected_status=$2 must_contain=$3
    shift 3
    set +e
    output=$("$@" 2>&1)
    status=$?
    set -e
    if [ "$status" -ne "$expected_status" ]; then
        echo "FAIL $name: status $status, expected $expected_status" >&2
        printf '%s\n' "$output" | sed 's/^/    /' >&2
        failures=$((failures + 1))
        return 0
    fi
    case $output in
        *"$must_contain"*) echo "ok $name" ;;
        *) echo "FAIL $name: output does not mention '$must_contain'" >&2
           printf '%s\n' "$output" | sed 's/^/    /' >&2
           failures=$((failures + 1)) ;;
    esac
}

check 'the real script passes' 0 'PASS desktop sanitizer sources' "$python" -B tests/desktop_sanitizer_sources_test.py

# Remove one source from the sanitizer list only. The line is the one in the sanitizer build, which links to "$1"; the ordinary build
# links to "$2", so the two lines stay distinguishable now that both live in functions.
sed 's|      src/process/native_windows.cpp src/process/native_surface.cpp -o "\$1"|      src/process/native_surface.cpp -o "$1"|' \
    tests/desktop_test.sh > "$root/missing-sanitized.sh"
if cmp -s tests/desktop_test.sh "$root/missing-sanitized.sh"; then
    echo 'FAIL the copy is identical, so this case would not exercise anything' >&2
    failures=$((failures + 1))
else
    check 'a source missing from the sanitizer build fails' 1 'does not compile src/process/native_windows.cpp' \
        "$python" -B tests/desktop_sanitizer_sources_test.py --script "$root/missing-sanitized.sh"
fi

# Remove the same source from the ordinary list instead: nothing is missing any more, so the check passes again. This is the
# direction that keeps the check from being a check on a fixed file rather than on the relationship between the two lists.
sed '0,/src\/process\/native_windows.cpp src\/process\/native_surface.cpp/s//src\/process\/native_surface.cpp/' \
    tests/desktop_test.sh > "$root/missing-ordinary.sh"
if cmp -s tests/desktop_test.sh "$root/missing-ordinary.sh"; then
    echo 'FAIL the ordinary-list copy is identical, so this case would not exercise anything' >&2
    failures=$((failures + 1))
else
    check 'a source missing from both lists is not a failure' 0 'PASS desktop sanitizer sources' \
        "$python" -B tests/desktop_sanitizer_sources_test.py --script "$root/missing-ordinary.sh"
fi

if [ "$failures" -ne 0 ]; then
    echo "desktop sanitizer sources: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS desktop sanitizer sources: the check distinguishes a stale list from a consistent one'
