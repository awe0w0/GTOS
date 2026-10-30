#!/bin/sh
# The GTOS port's own mapping, checked where it lives.
#
# The mapping this window produced is not in this repository: it is three edits in the port tree, plus a gate that is deliberately
# still closed. Without a test, the next person cannot tell a mapping that was never made from one that was quietly removed. This
# unit asserts the four facts the C1 record depends on:
#
#   1. the GTOS Rust triple is mapped, and only for x86;
#   2. the triple is listed where rust.gni insists on finding it;
#   3. the GTOS clang_lib directory exists as a branch, so the template computes a path instead of asserting;
#   4. //v8 still refuses GTOS, because the runtime backend does not exist.
#
# Set GTOS_PORT_ROOT to the port's source root (the directory that contains build/, v8/ and content/). Without it the unit skips,
# because a checkout of this repository alone has no port tree.
set -eu
port=${GTOS_PORT_ROOT:-}
if [ -z "$port" ] || [ ! -d "$port" ]; then
    echo 'skip GTOS port mapping: GTOS_PORT_ROOT is not set or is not a directory'
    exit 0
fi
failures=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2" >&2; failures=$((failures + 1)); fi; }

grep -q 'i686-unknown-gtos' "$port/build/config/rust.gni"
check $? 'build/config/rust.gni maps i686-unknown-gtos'
grep -q 'GTOS has a Rust mapping only for x86' "$port/build/config/rust.gni"
check $? 'build/config/rust.gni refuses to name a triple for any other cpu'
grep -qx 'i686-unknown-gtos' "$port/build/rust/known-target-triples.txt"
check $? 'known-target-triples.txt lists i686-unknown-gtos on its own line'
grep -q 'i386-unknown-gtos' "$port/build/config/clang/BUILD.gn"
check $? 'build/config/clang/BUILD.gn has a GTOS runtime-library directory'

# Two more things the graph needed, both easy to lose in a rebase: the visibility config that Chromium's runtime targets remove,
# and the shared-library tools that make a target of that type generatable at all.
grep -q 'symbol_visibility_hidden' "$port/build/config/BUILDCONFIG.gn"
check $? 'BUILDCONFIG.gn offers the GTOS toolchain the visibility config its dependencies remove'
grep -q 'tool("solink")' "$port/build/toolchain/gtos/BUILD.gn"
check $? 'the GTOS toolchain defines a solink tool'

# The gate: if this stops matching, either the backend arrived (good, and this unit should be updated) or the gate was deleted
# without one (bad, and this must fail).
grep -q 'GTOS V8 runtime backend is not implemented' "$port/v8/BUILD.gn"
check $? 'v8/BUILD.gn still refuses a GTOS runtime backend'
grep -q 'current_os == "gtos"' "$port/v8/BUILD.gn"
check $? 'the V8 refusal is still conditional on the GTOS target'

# The gate must stay a gate: the assertion has to be the first thing the file does, not something deeper that a target could skip.
first_match=$(grep -n 'current_os == "gtos"' "$port/v8/BUILD.gn" | sed -n '1p')
line_number=${first_match%%:*}
if [ "${line_number:-999}" -lt 12 ]; then
    echo 'ok the V8 refusal is at the top of the file'
else
    echo 'FAIL the V8 refusal is no longer at the top of the file' >&2
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "GTOS port mapping: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS GTOS port mapping: the Rust triple, the clang_lib directory and the V8 refusal are all where the C1 record says'
echo 'This says nothing about whether the port builds: no object, link or guest run is exercised here'
