#!/bin/bash
# The GTOS address layer: negative control, canonical-type reuse, i386 layout, the
# three narrow names, the independent-include gate on the real consumer headers,
# and the refusals.
#
#   native_address_test.sh FRESH_OUT REAL_SOURCE GEN_ROOT SDK_ROOT CLANG_DIR
#
# REAL_SOURCE is the locked Chromium view the reviewed M run used
# (`.../native-content-mojom-core-consumer-20261019-a/run-10/source`); GEN_ROOT is
# that run's generated-header root; SDK_ROOT is the original native SDK the 009
# binding resolved. Nothing under any of them is written, and an existing OUT is
# refused.
#
# What is proven:
#   0. the bound inputs are the pinned bytes before anything is built;
#   1. the original SDK alone cannot resolve <sys/socket.h>, and the diagnostic is
#      that missing header - not any nonzero exit;
#   2. the export overlay provides exactly three members and nothing else;
#   3. the C++ and C units state the exact i386 layout: socklen_t 4,
#      sa_family_t 2, in_addr 4, in6_addr 16, sockaddr 4/align 2/data at 2,
#      sockaddr_in 16/align 4/offsets 0,2,4,8, sockaddr_in6 28/align 4/offsets
#      0,2,4,8,24, sockaddr_storage 128/align 4/family at 0;
#   4. socklen_t is the same type as unsigned long and NOT unsigned int - the
#      check sizeof cannot make;
#   5. both include orders work: canonical-then-umbrella and umbrella-then-canonical;
#   6. -H proves the three names came from this export and the ten canonical type
#      headers came from the bound SDK, with no repository include tree and no host
#      system header in the closure;
#   7. the refusals hold, each with its own diagnostic: no GTOS identity, a host OS
#      smuggled alongside it, a conflicting AF value, a same-width typedef
#      redefinition, and a call to the socket API this layer does not declare;
#   8. the real Chromium consumer headers - net/base/sockaddr_storage.h,
#      net/base/ip_address_util.h and net/base/ip_endpoint.h - each compile from an
#      INDEPENDENT translation unit that includes that header and nothing else.
#      sockaddr_storage.h passes only because the reviewed overlay adds
#      BUILDFLAG(IS_GTOS) to its <sys/socket.h> arm; the raw header is untouched.
#
# This is a type and include layer check. It does not run Chromium, it does not
# link, and it does not claim a socket API exists. ToSockAddr/FromSockAddr
# behaviour is NOT_RUN.
set -euo pipefail
if [ "$#" -ne 5 ]; then
  echo "usage: native_address_test.sh FRESH_OUT REAL_SOURCE GEN_ROOT SDK_ROOT CLANG_DIR" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
real="$2"
gen="$3"
sdk="$4"
clangdir="$5"
if [ -e "$out" ]; then
  echo "Choose a fresh output directory: $out" >&2
  exit 2
fi
for required in "$real/net/base/sockaddr_storage.h" "$real/build/build_config.h" \
                "$real/net/base/ip_endpoint.h" "$real/net/base/ip_address_util.h" \
                "$sdk/sdk/llvm-libc-types/socklen_t.h" \
                "$sdk/sdk/llvm-libc-macros/stdint-macros.h" \
                "$gen/build/blink_buildflags.h"; do
  [ -e "$required" ] || { echo "missing required input: $required" >&2; exit 2; }
done
mkdir -p "$out/source-snapshot"
out="$(realpath "$out")"
real="$(realpath "$real")"
gen="$(realpath "$gen")"
sdk="$(realpath "$sdk")"
clangdir="$(realpath "$clangdir")"
cd "$repo"

clang="$clangdir/bin/clang"
resource_include="$(echo "$clangdir"/lib/clang/*/include)"

snapshot="tests/native_address_overlay.py tests/native_address_origins.py \
 tests/native_address_test.sh tests/native_address_types_tu.cc \
 tests/native_address_types_c.c tests/native_address_include_tu.c \
 tests/native_address_typedef_conflict_tu.cc tests/native_address_function_tu.cc \
 tests/native_address_consumer_sockaddr_storage_tu.cc \
 tests/native_address_consumer_ip_address_util_tu.cc \
 tests/native_address_consumer_ip_endpoint_tu.cc \
 tools/export-native-address-sdk.py include/net/address_types.h \
 include/sys/socket.h include/netinet/in.h"
sha256sum $snapshot > "$out/source-hashes.txt"
for f in $snapshot; do
  cp "$f" "$out/source-snapshot/$(basename "$f")"
done

# --- 0. the bound inputs -----------------------------------------------------
{
  printf 'real_source=%s\n' "$real"
  printf 'gen_root=%s\n' "$gen"
  printf 'sdk_root=%s\n' "$sdk"
  printf 'clang=%s\n' "$clang"
  "$clang" --version | head -n 1
  sha256sum "$real/net/base/sockaddr_storage.h" "$real/net/base/ip_endpoint.h" \
    "$real/net/base/ip_address_util.h" "$real/build/build_config.h" \
    "$real/build/buildflag.h" "$gen/build/blink_buildflags.h" \
    "$sdk/sdk/llvm-libc-types/socklen_t.h" \
    "$sdk/sdk/llvm-libc-types/sa_family_t.h" \
    "$sdk/sdk/llvm-libc-types/in_addr_t.h" \
    "$sdk/sdk/llvm-libc-types/in_port_t.h" \
    "$sdk/sdk/llvm-libc-types/struct_sockaddr.h" \
    "$sdk/sdk/llvm-libc-types/struct_in_addr.h" \
    "$sdk/sdk/llvm-libc-types/struct_in6_addr.h" \
    "$sdk/sdk/llvm-libc-types/struct_sockaddr_in.h" \
    "$sdk/sdk/llvm-libc-types/struct_sockaddr_in6.h" \
    "$sdk/sdk/llvm-libc-types/struct_sockaddr_storage.h" \
    "$sdk/sdk/llvm-libc-macros/stdint-macros.h"
} > "$out/binding.txt"

# The two pins this round is allowed to rely on for the consumer layer.
grep -q '^bd185448071dbd2907bdf2896ff754573ee841e8c4cdc939595c00c53e9a5216 ' \
  "$out/binding.txt" || {
  echo "the real build_config is not the pinned 13393-byte view" >&2; exit 1; }
grep -q '^f11db8dd752961a5f65d21e6477d9e3b1f29f526a5097bbc09528a87ab7ff192 ' \
  "$out/binding.txt" || {
  echo "net/base/sockaddr_storage.h is not the pinned original" >&2; exit 1; }

# --- 1. the negative control: the SDK alone has no <sys/socket.h> -------------
# The C and C++ flag sets are separate: the C unit must not be handed -nostdinc++,
# which would be an unused argument standing in for a real check.
#
# The address layer itself is checked at -std=c++17: it must not need anything
# newer than the C++ it claims. The real consumer headers are checked at
# -std=c++23, which is what the reviewed Content -M configuration actually uses -
# base/location.h needs std::weak_ordering and base/numerics needs concepts, so
# c++17 would fail on libc++ for reasons that have nothing to do with addresses.
c_flags=(--target=i686-unknown-none-elf -std=c11 -Oz -Wall -Wextra -Werror
  -ffreestanding -fno-builtin -nostdinc
  -isystem "$sdk/sdk"
  -isystem "$resource_include")
cxx_flags=(--target=i686-unknown-none-elf -std=c++17 -Oz -Wall -Wextra -Werror
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-pic -fno-pie -nostdinc -nostdinc++
  -isystem "$real"
  -isystem "$gen"
  -isystem "$real/base/allocator/partition_allocator/src"
  -isystem "$gen/base/allocator/partition_allocator/src"
  -isystem "$sdk/sdk"
  -isystem "$real/third_party/libc++/src/include"
  -isystem "$sdk/c-sdk"
  -isystem "$resource_include")
# The same search order and the same freestanding rules, at the standard the real
# Content compile uses.
cxx23_flags=("${cxx_flags[@]/-std=c++17/-std=c++23}")

set +e
"$clang" "${c_flags[@]}" -c tests/native_address_include_tu.c \
  -o "$out/neg-sdk-only.o" > "$out/neg-sdk-only.log" 2>&1
sdk_only_rc=$?
set -e
[ "$sdk_only_rc" -ne 0 ] || {
  echo "the SDK alone resolved <sys/socket.h>; the negative control is void" >&2
  exit 1; }
grep -q "'sys/socket.h' file not found" "$out/neg-sdk-only.log" || {
  echo "the SDK-only failure is not the missing-header diagnostic" >&2
  tail -n 5 "$out/neg-sdk-only.log" >&2; exit 1; }
grep -q 'error:' "$out/neg-sdk-only.log" || {
  echo "the SDK-only log carries no diagnostic" >&2; exit 1; }

# --- 2. the export overlay ---------------------------------------------------
python3 tools/export-native-address-sdk.py "$out/sdk-overlay" \
  > "$out/export.log" 2>&1
export_dir="$out/sdk-overlay/sdk"
for member in net/address_types.h sys/socket.h netinet/in.h; do
  [ -f "$export_dir/$member" ] || {
    echo "the export overlay is missing $member" >&2; exit 1; }
done
# Three members, and no fourth: the ten canonical type headers stay in the SDK.
exported_count=$(find "$export_dir" -type f | wc -l)
[ "$exported_count" -eq 3 ] || {
  echo "the export overlay carries $exported_count members, not 3" >&2
  find "$export_dir" -type f >&2; exit 1; }

# --- the reviewed Chromium overlay for net/base/sockaddr_storage.h ------------
python3 tests/native_address_overlay.py "$real" "$out/overlay-build" \
  > "$out/overlay.log" 2>&1
overlay="$out/overlay-build/overlay"
[ -f "$overlay/net/base/sockaddr_storage.h" ] || {
  echo "the overlay has no net/base/sockaddr_storage.h" >&2; exit 1; }
[ ! -e "$overlay/build/build_config.h" ] || {
  echo "the overlay must not shadow build/build_config.h" >&2; exit 1; }
grep -q 'BUILDFLAG(IS_GTOS)' "$overlay/net/base/sockaddr_storage.h" || {
  echo "the overlay header has no GTOS arm" >&2; exit 1; }
# The overlay must differ from the pinned original by that one edit and nothing
# else; the generator proves it by reverting, and the diff is kept as evidence.
[ -s "$out/overlay-build/overlay.diff" ] || {
  echo "the overlay diff is empty; the generator changed nothing" >&2; exit 1; }
# One removed line and one added line, plus the two file headers: four in total.
changed_lines=$(grep -cE '^[+-]' "$out/overlay-build/overlay.diff" || true)
[ "$changed_lines" -eq 4 ] || {
  echo "the overlay diff changes $changed_lines lines, not one line" >&2
  cat "$out/overlay-build/overlay.diff" >&2; exit 1; }

# The positive side of the negative control: the same bytes now compile.
cxx_flags_with_overlay=(-D__GTOS__=1 -isystem "$export_dir" -isystem "$overlay" "${cxx_flags[@]}")
c_flags_with_overlay=(-D__GTOS__=1 -isystem "$export_dir" "${c_flags[@]}")

"$clang" "${c_flags_with_overlay[@]}" -H -c tests/native_address_include_tu.c \
  -o "$out/include-tu.o" > "$out/include-tu.log" 2> "$out/include-tu-include.log"

# --- 3/4/5. the C++ and C units, and both include orders ---------------------
"$clang" "${cxx_flags_with_overlay[@]}" -H -c tests/native_address_types_tu.cc \
  -o "$out/address-types-tu.o" > "$out/address-types-tu.log" \
  2> "$out/address-types-include.log"
"$clang" "${c_flags_with_overlay[@]}" -H -c tests/native_address_types_c.c \
  -o "$out/address-types-c.o" > "$out/address-types-c.log" \
  2> "$out/address-types-c-include.log"

# --- 6. where the headers came from ------------------------------------------
# The two C units never include the Chromium build config, so they are checked in
# without-build-config mode; the C++ unit must open it from the real tree. The
# last field narrows which overlay names that unit is expected to open: the
# include unit only names <sys/socket.h>, so <netinet/in.h> is not its business.
for spec in "address-types:address-types-include.log:with-build-config:" \
            "address-types-c:address-types-c-include.log:without-build-config:" \
            "include-tu:include-tu-include.log:without-build-config:sys/socket.h"; do
  name=${spec%%:*}
  rest=${spec#*:}
  log=${rest%%:*}
  rest=${rest#*:}
  mode=${rest%%:*}
  only=${rest#*:}
  python3 tests/native_address_origins.py "$out/$log" "$export_dir" \
    "$sdk/sdk" "$real" "$repo" "$mode" ${only:+"$only"} \
    > "$out/origins-$name.txt" 2>&1 || {
    echo "the include origins for $name are wrong" >&2
    tail -n 20 "$out/origins-$name.txt" >&2; exit 1; }
  grep -q 'ORIGINS PASS' "$out/origins-$name.txt" || {
    echo "the origins check for $name did not pass" >&2
    tail -n 20 "$out/origins-$name.txt" >&2; exit 1; }
done

# --- 7. the refusals ---------------------------------------------------------
set +e
"$clang" "${cxx_flags[@]}" -isystem "$export_dir" -isystem "$overlay" \
  -c tests/native_address_types_tu.cc -o "$out/neg-no-identity.o" \
  > "$out/neg-no-identity.log" 2>&1
no_identity_rc=$?
"$clang" "${cxx_flags_with_overlay[@]}" -D__linux__=1 \
  -c tests/native_address_types_tu.cc -o "$out/neg-host-os.o" \
  > "$out/neg-host-os.log" 2>&1
host_os_rc=$?
"$clang" "${cxx_flags_with_overlay[@]}" -DAF_INET=3 \
  -c tests/native_address_types_tu.cc -o "$out/neg-af-conflict.o" \
  > "$out/neg-af-conflict.log" 2>&1
af_conflict_rc=$?
"$clang" "${cxx_flags_with_overlay[@]}" \
  -c tests/native_address_typedef_conflict_tu.cc -o "$out/neg-typedef.o" \
  > "$out/neg-typedef.log" 2>&1
typedef_rc=$?
"$clang" "${cxx_flags_with_overlay[@]}" -c tests/native_address_function_tu.cc \
  -o "$out/neg-function.o" > "$out/neg-function.log" 2>&1
function_rc=$?
set -e

[ "$no_identity_rc" -ne 0 ] || {
  echo "the address unit compiled without the GTOS identity" >&2; exit 1; }
[ "$host_os_rc" -ne 0 ] || {
  echo "the address unit compiled with __linux__ alongside GTOS" >&2; exit 1; }
[ "$af_conflict_rc" -ne 0 ] || {
  echo "a conflicting AF_INET value was accepted" >&2; exit 1; }
[ "$typedef_rc" -ne 0 ] || {
  echo "a same-width socklen_t redefinition was accepted" >&2; exit 1; }
[ "$function_rc" -ne 0 ] || {
  echo "the layer declared a socket function it does not implement" >&2; exit 1; }

grep -q 'is the GTOS address representation' "$out/neg-no-identity.log" || {
  echo "the no-identity refusal is not the address identity gate" >&2
  tail -n 5 "$out/neg-no-identity.log" >&2; exit 1; }
grep -q 'refuses a host OS identity' "$out/neg-host-os.log" || {
  echo "the host-OS refusal is not the address identity gate" >&2
  tail -n 5 "$out/neg-host-os.log" >&2; exit 1; }
grep -q 'AF_INET is already defined with a different value' \
  "$out/neg-af-conflict.log" || {
  echo "the AF refusal is not the constant-conflict gate" >&2
  tail -n 5 "$out/neg-af-conflict.log" >&2; exit 1; }
grep -q 'typedef redefinition with different types' "$out/neg-typedef.log" || {
  echo "the typedef negative did not fail on the typedef" >&2
  tail -n 5 "$out/neg-typedef.log" >&2; exit 1; }
# C and C++ word this differently: "call to undeclared function" in C,
# "use of undeclared identifier" in C++. Either is the missing declaration; a
# missing header would say something else.
grep -qE 'call to undeclared function|use of undeclared identifier' \
  "$out/neg-function.log" || {
  echo "the function negative did not fail on a missing declaration" >&2
  tail -n 5 "$out/neg-function.log" >&2; exit 1; }
grep -q 'fatal error' "$out/neg-typedef.log" && {
  echo "the typedef negative failed on a missing header, not on types" >&2
  tail -n 5 "$out/neg-typedef.log" >&2; exit 1; }
grep -q 'fatal error' "$out/neg-function.log" && {
  echo "the function negative failed on a missing header, not on the call" >&2
  tail -n 5 "$out/neg-function.log" >&2; exit 1; }

# --- 8. the real consumer headers, each from an independent unit --------------
# Each of these units includes exactly one header. That is the gate: a unit that
# pre-included <sys/socket.h> would compile even without the overlay fix, and
# would be proving the include order instead of the header.
#
# These run at -std=c++23 because the real headers need it: base/location.h wants
# std::weak_ordering and base/numerics/integral_constant_like.h wants concepts, so
# a c++17 failure here would be a libc++-standard mismatch rather than an address
# problem. The address layer itself is still proven at c++17 above.
consumer_flags=(-D__GTOS__=1 -isystem "$export_dir" -isystem "$overlay"
  "${cxx23_flags[@]}")
for unit in sockaddr_storage ip_address_util ip_endpoint; do
  set +e
  "$clang" "${consumer_flags[@]}" -fsyntax-only -H \
    "tests/native_address_consumer_${unit}_tu.cc" \
    > "$out/consumer-$unit.log" 2> "$out/consumer-$unit-include.log"
  rc=$?
  set -e
  [ "$rc" -eq 0 ] || {
    echo "the independent include of $unit failed" >&2
    grep -m1 'error:' "$out/consumer-$unit-include.log" >&2 || true
    tail -n 5 "$out/consumer-$unit-include.log" >&2; exit 1; }
  # Nothing may be pre-included: the only headers the unit itself names is one.
  grep -q '^#include "net/base/'"$unit"'.h"' \
    "tests/native_address_consumer_${unit}_tu.cc" || {
    echo "the $unit unit does not include its header directly" >&2; exit 1; }
done

# The gate is only meaningful if it would fail without the overlay: prove that by
# compiling the same unit against the raw tree, where the GTOS arm is absent.
set +e
"$clang" -D__GTOS__=1 -isystem "$export_dir" "${cxx23_flags[@]}" -fsyntax-only \
  tests/native_address_consumer_sockaddr_storage_tu.cc \
  > "$out/neg-consumer-raw.log" 2>&1
raw_rc=$?
set -e
[ "$raw_rc" -ne 0 ] || {
  echo "sockaddr_storage.h compiled without the overlay; the gate proves nothing" >&2
  exit 1; }
grep -q "unknown type name 'sockaddr_storage'\|use of undeclared identifier" \
  "$out/neg-consumer-raw.log" || {
  echo "the raw-tree failure is not the missing sockaddr type" >&2
  tail -n 5 "$out/neg-consumer-raw.log" >&2; exit 1; }

printf 'sdk-only negative: 1 (missing sys/socket.h)\n' > "$out/gates.txt"
printf 'export members: 3\n' >> "$out/gates.txt"
printf 'include orders: 2 (canonical-first, umbrella-first)\n' >> "$out/gates.txt"
printf 'origins checks: 3\n' >> "$out/gates.txt"
printf 'identity negatives refused: 2\n' >> "$out/gates.txt"
printf 'constant conflict refused: 1\n' >> "$out/gates.txt"
printf 'typedef conflict refused: 1\n' >> "$out/gates.txt"
printf 'undeclared socket call refused: 1\n' >> "$out/gates.txt"
printf 'independent consumer headers compiled: 3\n' >> "$out/gates.txt"
printf 'raw-tree consumer negative: 1\n' >> "$out/gates.txt"

printf 'NATIVE ADDRESS LAYER PASS\n' | tee "$out/result.txt"
