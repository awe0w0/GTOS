#!/bin/sh
# The target specification the whole GTOS Rust path rests on.
#
# Every measurement in this window assumes this file says what the port needs: an x86 i686 ELF target with no operating system, soft
# float, no SSE, and a static non-PIE model. A quiet edit to one field would change codegen under tests that would still pass, so the
# fields are asserted here by name, and rustc is asked to read the file when a compiler is available.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
spec=$repo/apps/native_content_build/rust/i686-unknown-gtos.json
if ! command -v python3 >/dev/null 2>&1; then
    echo 'skip rust target spec: python3 is not available to read the JSON'
    exit 0
fi
failures=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2" >&2; failures=$((failures + 1)); fi; }

expect_field() {
    value=$(python3 - "$spec" "$1" <<'PY'
import json, sys
with open(sys.argv[1]) as handle:
    print(json.load(handle).get(sys.argv[2], ""))
PY
)
    if [ "$value" = "$2" ]; then echo "ok $1 is $2"; else
        echo "FAIL $1 is '$value', expected '$2'" >&2; failures=$((failures + 1)); fi
}

expect_field arch x86
expect_field cpu i686
expect_field llvm-target i686-unknown-none-elf
expect_field binary-format elf
expect_field os none
expect_field panic-strategy abort
expect_field relocation-model static
expect_field target-pointer-width 32
if python3 - "$spec" <<'PY'
import json, sys
with open(sys.argv[1]) as handle:
    raise SystemExit(0 if json.load(handle).get("disable-redzone") is True else 1)
PY
then echo 'ok disable-redzone is true'; else echo 'FAIL disable-redzone is not true' >&2; failures=$((failures + 1)); fi

# Soft float has to be a feature, not an abi string alone: the features are what codegen reads.
if python3 - "$spec" <<'PY'
import json, sys
with open(sys.argv[1]) as handle:
    features = json.load(handle).get("features", "").split(",")
required = {"+soft-float": True, "-sse": True, "-sse2": True, "-mmx": True}
raise SystemExit(0 if all((f in features) == want for f, want in required.items()) else 1)
PY
then echo 'ok the features ask for soft float and refuse mmx and sse'; else
    echo 'FAIL the features do not ask for soft float with mmx and sse refused' >&2; failures=$((failures + 1)); fi

# A compiler that can read the file is the only proof that the JSON is a specification rather than a document.
if [ -n "${RUSTC:-}" ] && [ "$failures" -eq 0 ]; then
    if libdir=$("$RUSTC" -Z unstable-options --target "$spec" --print target-libdir 2>&1); then
        case $libdir in
            *i686-unknown-gtos*) echo "ok rustc reads the specification and reports $libdir";;
            *) echo "FAIL rustc read the specification but reported $libdir" >&2; failures=$((failures + 1));;
        esac
    else
        echo "FAIL rustc refused the specification: $libdir" >&2; failures=$((failures + 1))
    fi
    if cfg=$("$RUSTC" -Z unstable-options --target "$spec" --print cfg 2>&1); then
        # Measured: soft float shows up as the target abi, not as a target feature, so that is what is asserted. The absence
        # checks matter as much: an sse or mmx feature would mean the spec was not read the way the port needs.
        missing=''
        for want in 'target_arch="x86"' 'target_os="none"' 'target_abi="softfloat"' 'target_pointer_width="32"'; do
            printf '%s\n' "$cfg" | grep -qx -- "$want" || missing="$missing $want"
        done
        present=''
        for unwanted in 'target_feature="sse"' 'target_feature="sse2"' 'target_feature="mmx"'; do
            if printf '%s\n' "$cfg" | grep -qx -- "$unwanted"; then present="$present $unwanted"; fi
        done
        if [ -z "$missing" ] && [ -z "$present" ]; then echo 'ok the specification yields the expected target configuration'; else
            echo "FAIL the specification is missing:$missing and unexpectedly has:$present" >&2; failures=$((failures + 1)); fi
    else
        echo "FAIL rustc could not print the target configuration: $cfg" >&2; failures=$((failures + 1))
    fi
else
    echo 'skip the compiler checks: RUSTC is not set or the JSON checks already failed'
fi

if [ "$failures" -ne 0 ]; then
    echo "rust target spec: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS rust target spec: x86 i686 ELF, no OS, soft float, static, 32-bit pointers; rustc reads it as intended'
echo 'This does not prove the port builds or that an image for it runs'
