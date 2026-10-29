#!/bin/sh
# Real test for bringup-patch.py: it must classify its input by identity, and it must never report an input it does not recognise
# as already patched. Each case runs the real script on a real copy; the shared port tree is only read.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
port=${GTOS_PORT:-}
if [ -z "$port" ]; then
    # The test needs the original text; without the port tree it says so instead of skipping silently.
    echo 'bringup-patch test: set GTOS_PORT to the port source root (the tree that carries the original BUILD.gn)' >&2
    exit 2
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
pass=0
fail=0
check() {
    description=$1; expected=$2
    shift 2
    set +e
    "$@" > "$work/out.log" 2>&1
    status=$?
    set -e
    if [ "$status" -eq "$expected" ]; then
        printf 'PASS: %s\n' "$description"
        pass=$((pass + 1))
    else
        printf 'FAIL: %s (status %s, expected %s)\n' "$description" "$status" "$expected" >&2
        cat "$work/out.log" >&2
        fail=$((fail + 1))
    fi
}

# 1. The original text is patched, and the patch changes the file exactly once.
cp "$port/BUILD.gn" "$work/BUILD.gn"
cp "$port/.gn" "$work/dot-gn"
before=$(sha256sum "$work/BUILD.gn" | cut -d' ' -f1)
check 'original text is patched' 0 python3 "$here/bringup-patch.py" "$work/BUILD.gn" "$work/dot-gn"
after=$(sha256sum "$work/BUILD.gn" | cut -d' ' -f1)
if [ "$before" != "$after" ]; then printf 'PASS: the build file changed\n'; pass=$((pass + 1)); else printf 'FAIL: the build file did not change\n' >&2; fail=$((fail + 1)); fi
if grep -q 'gtos_frontier_bringup = false' "$work/BUILD.gn" && grep -q 'if (is_gtos && !gtos_frontier_bringup)' "$work/BUILD.gn"; then
    printf 'PASS: the switch and the guarded refusal are both present\n'; pass=$((pass + 1))
else
    printf 'FAIL: the patched file lacks the switch or the guard\n' >&2; fail=$((fail + 1))
fi
if [ "$(grep -c 'GTOS browser runtime is not implemented' "$work/BUILD.gn")" -eq 1 ]; then
    printf 'PASS: the refusal message is still exactly once\n'; pass=$((pass + 1))
else
    printf 'FAIL: the refusal message is not exactly once\n' >&2; fail=$((fail + 1))
fi
if grep -q 'script_executable = "/usr/bin/python3.8"' "$work/dot-gn"; then
    printf 'PASS: the dotfile names the host interpreter\n'; pass=$((pass + 1))
else
    printf 'FAIL: the dotfile was not patched\n' >&2; fail=$((fail + 1))
fi

# 2. Running it again reports already patched and leaves both files byte-identical.
build_before=$(sha256sum "$work/BUILD.gn" | cut -d' ' -f1)
gn_before=$(sha256sum "$work/dot-gn" | cut -d' ' -f1)
check 'a patched file is reported as already patched' 0 python3 "$here/bringup-patch.py" "$work/BUILD.gn" "$work/dot-gn"
if [ "$build_before" = "$(sha256sum "$work/BUILD.gn" | cut -d' ' -f1)" ] && [ "$gn_before" = "$(sha256sum "$work/dot-gn" | cut -d' ' -f1)" ]; then
    printf 'PASS: the second run changed nothing\n'; pass=$((pass + 1))
else
    printf 'FAIL: the second run changed a file\n' >&2; fail=$((fail + 1))
fi

# 3. A file without the block is refused, not reported as patched.
awk '/^if \(is_gtos\) \{$/{skip=1} skip==0{print} skip==1 && /^\}$/{skip=0}' "$port/BUILD.gn" > "$work/missing.gn"
cp "$port/.gn" "$work/missing-dot-gn"
check 'a file without the block is refused' 1 python3 "$here/bringup-patch.py" "$work/missing.gn" "$work/missing-dot-gn"

# 4. A block that was edited is refused too.
sed 's/GTOS browser runtime is not implemented/GTOS browser runtime is not implmented/' "$port/BUILD.gn" > "$work/corrupt.gn"
check 'a corrupted block is refused' 1 python3 "$here/bringup-patch.py" "$work/corrupt.gn" "$work/missing-dot-gn"

# 5. An empty file is refused.
: > "$work/empty.gn"
check 'an empty file is refused' 1 python3 "$here/bringup-patch.py" "$work/empty.gn" "$work/missing-dot-gn"

# 6. A file that only mentions the switch name is refused: presence of a word is not identity.
printf 'declare_args() {\n  gtos_frontier_bringup = false\n}\n' > "$work/lookalike.gn"
check 'a look-alike file is refused' 1 python3 "$here/bringup-patch.py" "$work/lookalike.gn" "$work/missing-dot-gn"

# 7. Nothing in any refused case was modified.
if [ ! -s "$work/lookalike.gn" ] || grep -q 'print(' "$work/lookalike.gn"; then
    printf 'FAIL: a refused file was modified\n' >&2; fail=$((fail + 1))
else
    printf 'PASS: refused files stay untouched\n'; pass=$((pass + 1))
fi

printf 'bringup-patch test: %s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
