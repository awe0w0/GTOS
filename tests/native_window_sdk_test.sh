#!/bin/bash
# Verify the window SDK opt-in export as one batch.
#
#   native_window_sdk_test.sh FRESH_OUT SDK_ROOT CLANG_DIR
#
# SDK_ROOT is the original `tools/gtos_native_sdk` directory (the one holding
# `sdk/process/*.h` and `c-sdk/`). Nothing here writes to the repository, the
# original SDK or the overlay of an earlier run: every artifact lands in a fresh
# OUT, and an existing OUT is refused.
#
# What is proven, in order:
#   1. the default export still produces exactly the two graphics headers;
#   2. the opt-in export produces exactly those two plus the window three;
#   3. an existing output is refused before anything is written, and the
#      already-exported bytes are unchanged afterwards;
#   4. every exported member is byte-identical to its repository source;
#   5. the wire header compiles as C11 with no standard headers at all;
#   6. a C++ consumer compiles against the overlay plus the original SDK, its
#      `-H` output shows the window headers came from the overlay and the base
#      ABI from the original SDK, and no repository `include/` path is used;
#   7. the emitted machine code contains six `int $0x80` calls, and each opcode
#      carries its own exact request byte count (see native_window_sdk_calls.py);
#   8. the target identity gate really refuses a build without `-D__GTOS__=1`
#      and a build that also defines a host OS macro.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: native_window_sdk_test.sh FRESH_OUT SDK_ROOT CLANG_DIR" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
sdk="$2"
clangdir="$3"
if [ -e "$out" ]; then
  echo "Choose a fresh output directory: $out" >&2
  exit 2
fi
if [ ! -d "$sdk/sdk/process" ]; then
  echo "SDK_ROOT has no sdk/process: $sdk" >&2
  exit 2
fi
mkdir -p "$out/source-snapshot"
out="$(realpath "$out")"
sdk="$(realpath "$sdk")"
clangdir="$(realpath "$clangdir")"
cd "$repo"

clang="$clangdir/bin/clang"
tool="$repo/tools/export-native-graphics-sdk.py"

# The exact bytes that decide the result, kept next to the result.
snapshot="tools/export-native-graphics-sdk.py \
 include/process/window_abi.h include/process/window_client.h \
 include/process/native_window_adapter.h \
 include/process/resource_abi.h include/process/surface_abi.h \
 tests/native_window_sdk_wire_c11.c tests/native_window_sdk_client_tu.cc \
 tests/native_window_sdk_calls.py tests/native_window_sdk_test.sh"
sha256sum $snapshot > "$out/source-hashes.txt"
for f in $snapshot; do
  cp "$f" "$out/source-snapshot/$(basename "$f")"
done
{
  printf 'sdk_root=%s\n' "$sdk"
  printf 'clang=%s\n' "$clang"
  "$clang" --version | head -n 1
  sha256sum "$sdk/sdk/process/abi.h" "$sdk/sdk/process/vm_abi.h" \
    "$sdk/sdk/process/environment_abi.h" "$sdk/sdk/process/file_abi.h" \
    "$sdk/sdk/process/info_abi.h" "$sdk/sdk/process/tls.h"
} > "$out/sdk-binding.txt"

# --- 1. default export: exactly the two graphics members ----------------------
python3 "$tool" "$out/default" > "$out/default-export.log" 2>&1
members_default="$(find "$out/default/sdk/process" -type f -printf '%f\n' | sort | tr '\n' ' ')"
[ "$members_default" = "resource_abi.h surface_abi.h " ] || {
  echo "default export members changed: $members_default" >&2; exit 1; }
[ "$(find "$out/default" -type f | wc -l)" -eq 3 ] || {
  echo "default export wrote something other than two headers and a manifest" >&2
  find "$out/default" -type f >&2; exit 1; }
grep -q '"mode": "graphics-only"' "$out/default/manifest.json" || {
  echo "default manifest mode is not graphics-only" >&2; exit 1; }

# --- 2. opt-in export: the same two plus the window three ---------------------
python3 "$tool" --with-window "$out/with-window" > "$out/window-export.log" 2>&1
members_window="$(find "$out/with-window/sdk/process" -type f -printf '%f\n' | sort | tr '\n' ' ')"
[ "$members_window" = "native_window_adapter.h resource_abi.h surface_abi.h window_abi.h window_client.h " ] || {
  echo "opt-in export members changed: $members_window" >&2; exit 1; }
[ "$(find "$out/with-window" -type f | wc -l)" -eq 6 ] || {
  echo "opt-in export wrote an unexpected file" >&2
  find "$out/with-window" -type f >&2; exit 1; }
grep -q '"mode": "with-window"' "$out/with-window/manifest.json" || {
  echo "opt-in manifest mode is not with-window" >&2; exit 1; }

# --- 3. an existing output is refused, and nothing already exported changes ---
find "$out/with-window" -type f -exec sha256sum {} + | sort > "$out/with-window.before"
set +e
python3 "$tool" --with-window "$out/with-window" > "$out/refusal.log" 2>&1
refusal_rc=$?
set -e
[ "$refusal_rc" -eq 1 ] || { echo "existing output was not refused (rc=$refusal_rc)" >&2; exit 1; }
grep -q 'refused: the output already exists' "$out/refusal.log" || {
  echo "refusal message does not name the existing output" >&2; exit 1; }
find "$out/with-window" -type f -exec sha256sum {} + | sort > "$out/with-window.after"
cmp "$out/with-window.before" "$out/with-window.after" || {
  echo "the refused run changed the existing overlay" >&2; exit 1; }

# --- 4. every exported member equals its repository source --------------------
for member in resource_abi.h surface_abi.h window_abi.h window_client.h \
              native_window_adapter.h; do
  a="$(sha256sum "$out/with-window/sdk/process/$member" | cut -d' ' -f1)"
  b="$(sha256sum "$repo/include/process/$member" | cut -d' ' -f1)"
  [ "$a" = "$b" ] || { echo "exported $member differs from the repository source" >&2; exit 1; }
  printf '%s %s\n' "$member" "$a"
done > "$out/member-digests.txt"
# The default overlay must not have gained a window header by accident.
[ ! -e "$out/default/sdk/process/window_abi.h" ] &&
  [ ! -e "$out/default/sdk/process/window_client.h" ] &&
  [ ! -e "$out/default/sdk/process/native_window_adapter.h" ] || {
  echo "the default overlay contains a window header" >&2; exit 1; }

# --- 5. the wire header compiles as freestanding C11 --------------------------
# -nostdinc: only the overlay is on the search path, so the header has to be
# self-contained. No repository include/ anywhere. (-nostdinc++ is meaningless
# for a C translation unit and clang refuses it under -Werror.)
"$clang" --target=i686-unknown-none-elf -std=c11 -Oz -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -nostdinc \
  -isystem "$out/with-window/sdk" \
  -c tests/native_window_sdk_wire_c11.c -o "$out/wire-c11.o" \
  > "$out/wire-c11.log" 2>&1
"$clangdir/bin/llvm-nm" "$out/wire-c11.o" > "$out/wire-c11-nm.log"
grep -q 'gtos_window_sdk_wire_c11_check' "$out/wire-c11-nm.log" || {
  echo "the C11 wire object is empty" >&2; exit 1; }

# --- 6. a C++ consumer against the overlay plus the original SDK --------------
# -D__GTOS__=1 states the target identity the client header now requires; it is
# not a host macro, and the object still targets i686-unknown-none-elf.
"$clang" --target=i686-unknown-none-elf -std=c++11 -Oz -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics \
  -fno-pic -fno-pie -nostdinc -nostdinc++ -D__GTOS__=1 \
  -isystem "$out/with-window/sdk" -isystem "$sdk/sdk" -isystem "$sdk/c-sdk" \
  -H -c tests/native_window_sdk_client_tu.cc -o "$out/client-tu.o" \
  > "$out/client-tu.log" 2>&1
"$clangdir/bin/llvm-nm" "$out/client-tu.o" > "$out/client-tu-nm.log"
grep -q 'gtos_window_sdk_client_probe' "$out/client-tu-nm.log" || {
  echo "the client translation unit produced no function" >&2; exit 1; }

# The recorded compile line must not point at the repository include tree: the
# whole claim is that the consumer resolved its headers from the real SDK.
if grep -q "$repo/include" "$out/client-tu.log"; then
  echo "the consumer resolved a header from the repository include tree" >&2
  grep "$repo/include" "$out/client-tu.log" >&2; exit 1
fi
# Where each header actually came from, straight out of -H.
python3 - "$out" "$repo" <<'PY'
import pathlib, re, sys
out = pathlib.Path(sys.argv[1])
repo = sys.argv[2]
overlay = str(out / 'with-window' / 'sdk')
text = (out / 'client-tu.log').read_text()
seen = {}
for line in text.splitlines():
    match = re.match(r'^(?:\.+ )?(/\S+)$', line)
    if not match:
        continue
    path = match.group(1)
    name = pathlib.PurePosixPath(path).name
    if '/process/' in path and name.endswith('.h'):
        seen.setdefault('process/' + name, []).append(path)
required = {
    'process/window_abi.h': 'overlay',
    'process/window_client.h': 'overlay',
    'process/native_window_adapter.h': 'overlay',
    'process/abi.h': 'sdk',
    'process/vm_abi.h': 'sdk',
}
problems = []
for name, origin in required.items():
    paths = seen.get(name)
    if not paths:
        problems.append('%s was never opened' % name)
        continue
    path = paths[0]
    if origin == 'overlay' and not path.startswith(overlay):
        problems.append('%s came from %s, not the overlay' % (name, path))
    if origin == 'sdk' and overlay in path:
        problems.append('%s came from the overlay, not the original SDK' % name)
    if path.startswith(repo + '/include'):
        problems.append('%s came from the repository include tree' % name)
for name in sorted(seen):
    print('%-28s %s' % (name, seen[name][0]))
if problems:
    for problem in problems:
        print('ORIGIN FAIL: ' + problem)
    sys.exit(1)
print('ORIGINS PASS')
PY

# --- 7. the six real calls, each bound to its own request byte count ---------
objdump -d "$out/client-tu.o" > "$out/client-tu-disassembly.log"
# The pairing helper is also its own counterexample generator: --self-test runs
# the two swaps that used to be invisible (INFO carrying 20, INPUT_READ
# carrying 8) through the same analyse() the real disassembly goes through.
python3 tests/native_window_sdk_calls.py --self-test > "$out/call-pairing-self-test.log" 2>&1
python3 tests/native_window_sdk_calls.py "$out/client-tu-disassembly.log" \
  > "$out/client-tu-calls.log" 2>&1

# --- 8. the target identity gate is a real compile-time refusal --------------
# The client header now requires -D__GTOS__=1. Two negatives, both from the same
# translation unit that just compiled: no identity, and identity plus a host OS
# macro. A build that silently accepted either would be claiming a GTOS target it
# does not have.
set +e
"$clang" --target=i686-unknown-none-elf -std=c++11 -Oz -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics \
  -fno-pic -fno-pie -nostdinc -nostdinc++ \
  -isystem "$out/with-window/sdk" -isystem "$sdk/sdk" -isystem "$sdk/c-sdk" \
  -c tests/native_window_sdk_client_tu.cc -o "$out/neg-no-identity.o" \
  > "$out/neg-no-identity.log" 2>&1
no_identity_rc=$?
"$clang" --target=i686-unknown-none-elf -std=c++11 -Oz -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics \
  -fno-pic -fno-pie -nostdinc -nostdinc++ -D__GTOS__=1 -D__linux__=1 \
  -isystem "$out/with-window/sdk" -isystem "$sdk/sdk" -isystem "$sdk/c-sdk" \
  -c tests/native_window_sdk_client_tu.cc -o "$out/neg-host-macro.o" \
  > "$out/neg-host-macro.log" 2>&1
host_macro_rc=$?
set -e
[ "$no_identity_rc" -ne 0 ] || {
  echo "the client header compiled without -D__GTOS__=1" >&2; exit 1; }
[ "$host_macro_rc" -ne 0 ] || {
  echo "the client header compiled with a host OS macro defined" >&2; exit 1; }
grep -q "requires the GTOS target identity" "$out/neg-no-identity.log" || {
  echo "the no-identity refusal did not name the missing identity" >&2
  cat "$out/neg-no-identity.log" >&2; exit 1; }
grep -q "must not be compiled for a host OS" "$out/neg-host-macro.log" || {
  echo "the host-macro refusal did not name the host OS" >&2
  cat "$out/neg-host-macro.log" >&2; exit 1; }
printf 'identity negatives refused: 2\n' > "$out/identity-gate.txt"

printf 'NATIVE WINDOW SDK BATCH PASS\n' | tee "$out/result.txt"
