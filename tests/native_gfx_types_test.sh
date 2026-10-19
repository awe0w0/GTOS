#!/bin/bash
# Compile a real i686 consumer of the real Chromium gfx type header, under the
# GTOS identity, against a controlled overlay of exactly that header.
#
#   native_gfx_types_test.sh FRESH_OUT REAL_SOURCE GEN_ROOT SDK_ROOT CLANG_DIR
#
# REAL_SOURCE is the locked Chromium view (`.../run-07/source`); GEN_ROOT is that
# run's generated-header root (`.../run-07/build/gen`); SDK_ROOT is the original
# native SDK from the 009 binding. Nothing under any of them is written, and an
# existing OUT is refused.
#
# What is proven:
#   1. the two real headers are the pinned bytes before anything is built;
#   2. the overlay differs from the real header only by the reviewed insertions,
#      and removing them gives back the original bytes exactly;
#   3. the small TU really opens the overlay's `ui/gfx/native_ui_types.h` and the
#      real tree's `build/build_config.h` - recorded by -H, not assumed - and
#      opens no header from the GTOS repository;
#   4. the identity is exactly GTOS: Linux, Apple, macOS, iOS, Windows, Android,
#      ChromeOS, Fuchsia, Ozone and POSIX are all 0, and no host macro is needed;
#   5. the four gfx types match the adapter's real shapes, and the kernel handle
#      and the adapter pointer are different types;
#   6. the shortcuts are refused: no `-D__GTOS__`, `OS_LINUX` alongside OS_GTOS,
#      `USE_OZONE`, and a deliberate adapter-pointer/kernel-handle confusion all
#      fail to compile, each with its own diagnostic.
#
# This is a type-layer check. It does not run Chromium, and it does not claim the
# two old #error lines are the only thing between here and a browser.
set -euo pipefail
if [ "$#" -ne 5 ]; then
  echo "usage: native_gfx_types_test.sh FRESH_OUT REAL_SOURCE GEN_ROOT SDK_ROOT CLANG_DIR" >&2
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
for required in "$real/ui/gfx/native_ui_types.h" "$real/build/build_config.h" \
                "$sdk/sdk/process/abi.h" "$gen/build/blink_buildflags.h"; do
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
llvm_nm="$clangdir/bin/llvm-nm"
resource_include="$(echo "$clangdir"/lib/clang/*/include)"

snapshot="tests/native_gfx_types_overlay.py tests/native_gfx_types_tu.cc \
 tests/native_gfx_types_confusion_tu.cc tests/native_gfx_types_test.sh \
 include/process/native_window_adapter.h include/process/window_abi.h \
 include/process/window_client.h"
sha256sum $snapshot > "$out/source-hashes.txt"
for f in $snapshot; do
  cp "$f" "$out/source-snapshot/$(basename "$f")"
done
{
  printf 'real_source=%s\n' "$real"
  printf 'gen_root=%s\n' "$gen"
  printf 'sdk_root=%s\n' "$sdk"
  printf 'clang=%s\n' "$clang"
  "$clang" --version | head -n 1
  sha256sum "$real/ui/gfx/native_ui_types.h" "$real/build/build_config.h" \
    "$real/build/buildflag.h" "$gen/build/blink_buildflags.h"
} > "$out/binding.txt"

# --- 1/2. the controlled overlay ---------------------------------------------
python3 tests/native_gfx_types_overlay.py "$real" "$out/overlay-build" \
  > "$out/overlay.log" 2>&1
overlay="$out/overlay-build/overlay"
[ -f "$overlay/ui/gfx/native_ui_types.h" ] || {
  echo "the overlay has no ui/gfx/native_ui_types.h" >&2; exit 1; }
[ ! -e "$overlay/build/build_config.h" ] || {
  echo "the overlay must not shadow build/build_config.h" >&2; exit 1; }
grep -q 'BUILDFLAG(IS_GTOS)' "$overlay/ui/gfx/native_ui_types.h" || {
  echo "the overlay header has no GTOS branch" >&2; exit 1; }

# The native SDK side of the same story: the window headers, including the
# adapter, come from the SDK's own opt-in export rather than from the repository.
python3 tools/export-native-graphics-sdk.py --with-window "$out/sdk-overlay" \
  > "$out/sdk-overlay.log" 2>&1
sdkovl="$out/sdk-overlay/sdk"
[ -f "$sdkovl/process/native_window_adapter.h" ] || {
  echo "the SDK overlay has no process/native_window_adapter.h" >&2; exit 1; }

# The include search order is the whole claim: the overlay first for the gfx type
# header, then the real tree for everything else. The repository include/ tree is
# deliberately absent.
base_flags=(--target=i686-unknown-none-elf -std=c++17 -Oz -Wall -Wextra -Werror
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-pic -fno-pie -nostdinc -nostdinc++ 
  -isystem "$overlay"
  -isystem "$real"
  -isystem "$gen"
  -isystem "$real/base/allocator/partition_allocator/src"
  -isystem "$gen/base/allocator/partition_allocator/src"
  -isystem "$sdkovl"
  -isystem "$sdk/sdk"
  -isystem "$real/third_party/libc++/src/include"
  -isystem "$sdk/c-sdk"
  -isystem "$resource_include")
flags=(-D__GTOS__=1 "${base_flags[@]}")

# --- 3. compile the real-header TU -------------------------------------------
"$clang" "${flags[@]}" -H -c tests/native_gfx_types_tu.cc \
  -o "$out/gfx-types-tu.o" > "$out/gfx-types-tu.log" 2> "$out/gfx-types-include.log"
"$llvm_nm" "$out/gfx-types-tu.o" > "$out/gfx-types-tu-nm.log"
grep -q 'gtos_gfx_types_probe' "$out/gfx-types-tu-nm.log" || {
  echo "the type translation unit produced no function" >&2; exit 1; }

# Where the headers actually came from.
python3 - "$out" "$real" "$repo" <<'PY'
import pathlib, re, sys
out = pathlib.Path(sys.argv[1])
real = sys.argv[2]
repo = sys.argv[3]
overlay = str(out / 'overlay-build' / 'overlay')
sdkovl = str(out / 'sdk-overlay' / 'sdk')
text = (out / 'gfx-types-include.log').read_text()
seen = {}
for line in text.splitlines():
    match = re.match(r'^(?:\.+ )?(/\S+)$', line)
    if not match:
        continue
    path = match.group(1)
    seen[path] = seen.get(path, 0) + 1
problems = []
gfx = [path for path in seen if path.endswith('/ui/gfx/native_ui_types.h')]
build_config = [path for path in seen if path.endswith('/build/build_config.h')]
adapter = [path for path in seen
           if path.endswith('/process/native_window_adapter.h')]
if len(gfx) != 1:
    problems.append('ui/gfx/native_ui_types.h opened %d times: %s' % (len(gfx), gfx))
elif not gfx[0].startswith(overlay):
    problems.append('ui/gfx/native_ui_types.h came from %s, not the overlay' % gfx[0])
if len(build_config) != 1:
    problems.append('build/build_config.h opened %d times: %s'
                    % (len(build_config), build_config))
elif not build_config[0].startswith(real):
    problems.append('build/build_config.h came from %s, not the real tree'
                    % build_config[0])
if len(adapter) != 1:
    problems.append('process/native_window_adapter.h opened %d times: %s'
                    % (len(adapter), adapter))
elif not adapter[0].startswith(sdkovl):
    problems.append('the adapter header came from %s, not the SDK overlay'
                    % adapter[0])
leaked = [path for path in seen if path.startswith(repo + '/include')]
if leaked:
    problems.append('headers came from the GTOS repository include tree: %s' % leaked)
for path in sorted(seen):
    print('%s' % path)
if problems:
    for problem in problems:
        print('ORIGIN FAIL: ' + problem)
    sys.exit(1)
print('ORIGINS PASS')
PY

# --- 6. the shortcuts are refused --------------------------------------------
# Four negatives: three identity ones from the same translation unit that just
# compiled, and one deliberate type confusion.
set +e
"$clang" "${base_flags[@]}" -c tests/native_gfx_types_tu.cc \
  -o "$out/neg-no-identity.o" > "$out/neg-no-identity.log" 2>&1
no_identity_rc=$?
"$clang" "${flags[@]}" -DOS_LINUX=1 -c tests/native_gfx_types_tu.cc \
  -o "$out/neg-host-os.o" > "$out/neg-host-os.log" 2>&1
host_os_rc=$?
"$clang" "${flags[@]}" -DUSE_OZONE=1 -c tests/native_gfx_types_tu.cc \
  -o "$out/neg-ozone.o" > "$out/neg-ozone.log" 2>&1
ozone_rc=$?
"$clang" "${flags[@]}" -c tests/native_gfx_types_confusion_tu.cc \
  -o "$out/neg-confusion.o" > "$out/neg-confusion.log" 2>&1
confusion_rc=$?
set -e
[ "$no_identity_rc" -ne 0 ] || {
  echo "the type unit compiled without the GTOS identity" >&2; exit 1; }
[ "$host_os_rc" -ne 0 ] || {
  echo "the type unit compiled with OS_LINUX alongside OS_GTOS" >&2; exit 1; }
[ "$ozone_rc" -ne 0 ] || {
  echo "the type unit compiled with USE_OZONE set" >&2; exit 1; }
[ "$confusion_rc" -ne 0 ] || {
  echo "the adapter pointer and the kernel handle were interchangeable" >&2; exit 1; }
grep -q 'Please add support for your platform in build/build_config.h' \
  "$out/neg-no-identity.log" || {
  echo "the no-identity refusal is not the real build-config refusal" >&2
  tail -n 5 "$out/neg-no-identity.log" >&2; exit 1; }
grep -q 'GTOS cannot be combined with another target OS' "$out/neg-host-os.log" || {
  echo "the host-OS refusal is not the real build-config refusal" >&2
  tail -n 5 "$out/neg-host-os.log" >&2; exit 1; }
grep -q 'GTOS is not Ozone' "$out/neg-ozone.log" || {
  echo "the Ozone refusal is not the IS_OZONE assertion" >&2
  tail -n 5 "$out/neg-ozone.log" >&2; exit 1; }
# A missing header is not a refused conversion, so a fatal error here means the
# negative proved nothing and the run stops.
grep -q 'fatal error' "$out/neg-confusion.log" && {
  echo "the type-confusion negative failed on a missing header, not on types" >&2
  tail -n 5 "$out/neg-confusion.log" >&2; exit 1; }
grep -q 'error:' "$out/neg-confusion.log" || {
  echo "the type-confusion negative produced no diagnostic" >&2
  tail -n 5 "$out/neg-confusion.log" >&2; exit 1; }
printf 'identity negatives refused: 3\ntype confusion refused: 1\n' \
  > "$out/identity-gate.txt"

printf 'NATIVE GFX TYPES PASS\n' | tee "$out/result.txt"
