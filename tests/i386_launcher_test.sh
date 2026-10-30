#!/bin/sh
# Unit test for the executor choice used by the ELF32 unit.
#
# This tests the real selector the ELF32 script sources, not a model of it: the ordinary entry must fall back to a direct start,
# a restricted run must refuse without an executor, a named executor must be used exactly as named, and an unusable executor must
# be refused instead of being skipped. Nothing here compiles or starts an image.
#
# Every path this test creates lives under one root that it also removes: the project directory and the neighbouring runtime are
# both inside it, so a failing assertion can never write to, or delete, anything outside the test's own tree.
set -eu
cd "$(dirname "$0")/.."
. ./tests/i386-launcher.sh
repo=$(cd . && pwd)
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-i386-launcher.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM
failures=0
check() {
    name=$1 expected_status=$2 expected_output=$3
    shift 3
    set +e
    output=$("$@" 2>"$root/stderr")
    status=$?
    set -e
    if [ "$status" -ne "$expected_status" ]; then
        echo "FAIL $name: status $status, expected $expected_status" >&2
        failures=$((failures + 1))
        return 0
    fi
    if [ -n "$expected_output" ] && [ "$output" != "$expected_output" ]; then
        echo "FAIL $name: output '$output', expected '$expected_output'" >&2
        failures=$((failures + 1))
        return 0
    fi
    echo "ok $name"
}

# The project sits one level below the root and the neighbouring runtime beside it, which is the layout the selector looks for.
# Nothing outside "$root" is read or written.
mkdir -p "$root/proj" "$root/gtos-runtime/root/usr/bin"
cd "$root/proj"
PATH=/usr/bin:/bin
export PATH
REPO=$repo
export REPO

# choose <mode> [env arguments...] — runs the real selector in a child shell so an environment assignment or removal is what the
# selector actually sees.
choose() {
    mode=$1
    shift
    env "$@" sh -c ". \"\$REPO/tests/i386-launcher.sh\"; i386_launcher_choose $mode"
}

printf '#!/bin/sh\nexit 0\n' > "$root/launcher"
chmod 0555 "$root/launcher"
mkdir -p "$root/directory-executor"

check 'ordinary entry falls back to a direct start' 0 native choose all -u ELF32_I386_LAUNCHER
check 'restricted run without an executor is refused' 2 '' choose run -u ELF32_I386_LAUNCHER
grep -q 'needs an executor' "$root/stderr" && echo 'ok the refusal names the missing executor' || {
    echo 'FAIL the refusal does not name the missing executor' >&2; failures=$((failures + 1)); }

check 'a named executor is used as named' 0 "launcher $root/launcher" choose all ELF32_I386_LAUNCHER="$root/launcher"
check 'a named executor is used for a restricted run too' 0 "launcher $root/launcher" choose run ELF32_I386_LAUNCHER="$root/launcher"
check 'a missing named executor is refused' 2 '' choose all ELF32_I386_LAUNCHER="$root/absent"
grep -q 'not a regular executable file' "$root/stderr" && echo 'ok the refusal names the unusable executor' || {
    echo 'FAIL the refusal does not name the unusable executor' >&2; failures=$((failures + 1)); }

check 'a directory named as executor is refused' 2 '' choose all ELF32_I386_LAUNCHER="$root/directory-executor"
grep -q 'not a regular executable file' "$root/stderr" && echo 'ok the directory executor is refused as a directory' || {
    echo 'FAIL the directory executor refusal is not specific' >&2; failures=$((failures + 1)); }

printf '#!/bin/sh\nexit 0\n' > "$root/not-executable"
check 'a non-executable named executor is refused' 2 '' choose run ELF32_I386_LAUNCHER="$root/not-executable"

# The neighbouring private runtime is a real branch, not a guess: when it is present it is used without any variable, and here it
# is staged inside the test's own root so the check cannot depend on a private checkout being present.
printf '#!/bin/sh\nexit 0\n' > ../gtos-runtime/root/usr/bin/qemu-i386
chmod 0555 ../gtos-runtime/root/usr/bin/qemu-i386
check 'a neighbouring runtime is used without a variable' 0 'launcher ../gtos-runtime/root/usr/bin/qemu-i386' choose run -u ELF32_I386_LAUNCHER
rm -rf "$root/gtos-runtime"

check 'the choice line is classified as native or not' 0 'native' sh -c '. "$REPO/tests/i386-launcher.sh"; i386_launcher_is_native native && echo native || echo other'
check 'the executor path is recovered from the choice line' 0 "$root/launcher" sh -c '. "$REPO/tests/i386-launcher.sh"; i386_launcher_path "launcher '"$root"'/launcher"'

if [ "$failures" -ne 0 ]; then
    echo "i386 launcher choice: $failures failure(s)" >&2
    exit 1
fi
echo 'i386 launcher choice: all checks passed'
