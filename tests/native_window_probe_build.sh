#!/bin/bash
# Build the real GTOS i386 native window consumer from the qualified PNG archive.
#
# The pixel source is the same unchanged Wuffs/Skia object graph that the
# published PNG qualification proved; only the consumer translation unit and the
# presentation layer differ. Every digest of the reused objects is re-checked
# against the qualified manifest before the link, so nothing here is "trusted"
# because it happens to be on disk.
#
# The `<process/...>` headers are resolved the way a real application would see
# them: from the SDK overlay produced by `tools/export-native-graphics-sdk.py
# --with-window` plus the original `gtos_native_sdk`, never from the repository
# include tree. The `-H` log is kept and checked, so a header that quietly fell
# back to `include/` is a build failure rather than a hidden success.
#
#   build-native-window-probe.sh FRESH_OUT QUALIFIED_PNG_DIR CLANG_DIR [SDK_ROOT]
set -euo pipefail
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: build-native-window-probe.sh FRESH_OUT QUALIFIED_PNG_DIR CLANG_DIR [SDK_ROOT]" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
reference="$2"
clangdir="$3"
sdk="${4:-/mnt/f/GTOS-Chromium/artifacts/native-platform-file-handle-20261017-a/gn-01/source/tools/gtos_native_sdk}"
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
reference="$(realpath "$reference")"
clangdir="$(realpath "$clangdir")"
sdk="$(realpath "$sdk")"
cd "$repo"
clang="$clangdir/bin/clang"
linker="$clangdir/bin/ld.lld"
nm="$clangdir/bin/llvm-nm"
objcopy="$clangdir/bin/llvm-objcopy"
app="$repo/apps/native_window_probe"
png="$repo/apps/png_image_codec"
resource_app="$repo/apps/png_resource_probe"
objects=(decode memory ashldi3 lshrdi3 udivdi3 udivmoddi4 pixel skia)

"$clang" --version > "$out/clang.txt"
sha256sum "$reference/manifest.json" "$reference/libgtos_png_pixels.a" > "$out/reference-hashes.txt"

# Every reused object must be byte-identical to the qualified build, and the
# archive must be the qualified archive.
python3 - "$reference" "$out" <<'PY'
import hashlib, json, pathlib, shutil, subprocess, sys
reference = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
repo = pathlib.Path('/mnt/f/GTOS-Chromium/sources/GTOS')
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
proven = json.loads((reference / 'manifest.json').read_text())
assert proven['native_build_pass'] and proven['host_asan_ubsan_pass'], 'PNG qualification not passing'
assert proven['wuffs_profile'] == 'gtos-release', 'Unexpected Wuffs profile'
assert proven['component_undefined'] == '', 'PNG component has unresolved symbols'
archive = reference / 'libgtos_png_pixels.a'
assert sha(archive) == proven['archive_sha256'], 'Qualified archive digest differs'
imported = {}
for name in ('decode', 'memory', 'ashldi3', 'lshrdi3', 'udivdi3', 'udivmoddi4', 'pixel', 'skia'):
    obj = reference / (name + '.o')
    member = subprocess.check_output(['ar', 'p', str(archive), obj.name])
    assert member == obj.read_bytes(), 'Archive/object ownership mismatch: ' + name
    shutil.copyfile(obj, out / obj.name)
    imported[obj.name] = sha(obj)
shutil.copyfile(archive, out / archive.name)
# The headers, entry stub and linker geometry this build compiles against must be
# the exact bytes the PNG qualification bound, not merely files of the same name.
# Every name below is REQUIRED: a missing key is a failure, so the binding can
# never silently skip an input and pass because nothing was compared.
bound = proven['source_sha256']
required = ('apps/png_image_codec/png_decode.h', 'apps/png_image_codec/png_pixel.h',
            'apps/png_image_codec/start.s', 'apps/png_image_codec/linker.ld',
            'apps/png_image_codec/png_decode.c', 'apps/png_image_codec/png_pixel.cc')
missing = [name for name in required if name not in bound]
assert not missing, 'Qualification does not bind required inputs: ' + ', '.join(missing)
reused = {}
for name in required:
    current = sha(repo / name)
    assert current == bound[name], 'Reused PNG input differs from qualification: ' + name
    reused[name] = current
assert len(reused) == len(required), 'Reused PNG input binding is incomplete'
(out / 'qualified-objects.json').write_text(json.dumps(
    dict(archive_sha256=proven['archive_sha256'], objects=imported, reused_sources=reused),
    indent=2, sort_keys=True) + '\n')
print('qualified PNG objects verified:', len(imported), 'reused sources:', len(reused))
PY

common=(--target=i686-unknown-none-elf -Oz -Wall -Wextra -Werror -ffreestanding
  -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables
  -fno-asynchronous-unwind-tables -mno-mmx -mno-sse -mno-sse2
  -ffunction-sections -fdata-sections -fstack-usage -DGTOS_NATIVE_COMPONENT=1
  -D__GTOS__=1
  "-I$app" "-I$resource_app" "-I$png"
  "-I$repo/apps/wuffs_gif_probe/freestanding")

# The window headers must come from the exported overlay, exactly as an
# application that consumes the SDK would see them. The overlay is built here,
# into this OUT, so the build is reproducible from the two committed headers.
python3 tools/export-native-graphics-sdk.py --with-window "$out/sdk-overlay" \
  > "$out/sdk-overlay.log" 2>&1
[ -f "$out/sdk-overlay/sdk/process/window_client.h" ] || {
  echo "the SDK overlay is missing process/window_client.h" >&2; exit 1; }
[ -f "$out/sdk-overlay/sdk/process/native_window_adapter.h" ] || {
  echo "the SDK overlay is missing process/native_window_adapter.h" >&2; exit 1; }
[ -f "$out/sdk-overlay/sdk/process/window_abi.h" ] || {
  echo "the SDK overlay is missing process/window_abi.h" >&2; exit 1; }
# Byte-for-byte the committed headers, or the build below would prove nothing
# about the headers that are actually going to be reviewed.
for member in window_abi.h window_client.h native_window_adapter.h \
              resource_abi.h surface_abi.h; do
  a="$(sha256sum "$out/sdk-overlay/sdk/process/$member" | cut -d' ' -f1)"
  b="$(sha256sum "$repo/include/process/$member" | cut -d' ' -f1)"
  [ "$a" = "$b" ] || { echo "overlay $member differs from the committed header" >&2; exit 1; }
done
{
  printf 'sdk_root=%s\n' "$sdk"
  sha256sum "$sdk/sdk/process/abi.h" "$sdk/sdk/process/vm_abi.h" \
    "$sdk/sdk/process/environment_abi.h" "$sdk/sdk/process/file_abi.h" \
    "$sdk/sdk/process/info_abi.h" "$sdk/sdk/process/tls.h"
} > "$out/sdk-binding.txt"

sdk_flags=(-isystem "$out/sdk-overlay/sdk" -isystem "$sdk/sdk" -isystem "$sdk/c-sdk")
"$clang" -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics "${common[@]}" \
  "${sdk_flags[@]}" -H \
  -c "$app/native_main.cc" -o "$out/main.o" \
  > "$out/native-main.log" 2> "$out/native-main-include.log"
# Every <process/...> header the consumer opened, and where it came from.
grep -E '^\.+ /.*/process/[a-z_]+\.h$' "$out/native-main-include.log" \
  > "$out/native-main-origins.log" || true
if [ ! -s "$out/native-main-origins.log" ]; then
  echo "the consumer opened no <process/...> header; the -H check proved nothing" >&2
  cat "$out/native-main-include.log" >&2
  exit 1
fi
if grep -q "$repo/include" "$out/native-main-origins.log"; then
  echo "a production header was resolved from the repository include tree" >&2
  grep "$repo/include" "$out/native-main-origins.log" >&2
  exit 1
fi
for header in window_abi.h window_client.h native_window_adapter.h; do
  grep -F " $out/sdk-overlay/sdk/process/$header" "$out/native-main-origins.log" \
    > /dev/null || {
      echo "process/$header did not come from the exported overlay" >&2
      cat "$out/native-main-origins.log" >&2; exit 1; }
done
for header in abi.h vm_abi.h; do
  grep -F " $sdk/sdk/process/$header" "$out/native-main-origins.log" \
    > /dev/null || {
      echo "process/$header did not come from the original SDK" >&2
      cat "$out/native-main-origins.log" >&2; exit 1; }
done
"$clang" --target=i686-unknown-none-elf -c "$png/start.s" -o "$out/start.o" \
  > "$out/native-start.log" 2>&1
"$clang" --target=i686-unknown-none-elf -c "$resource_app/register_probe.s" \
  -o "$out/register-probe.o" > "$out/native-register-probe.log" 2>&1

# The unchanged qualified linker geometry, plus the diagnostic end-of-RW symbol.
{
  printf 'INCLUDE "%s"\n' "$png/linker.ld"
  printf 'resource_oracles_rw_limit = ALIGN(ADDR(.bss) + SIZEOF(.bss), 4096);\n'
} > "$out/window-linker.ld"
elf="$out/native-window-probe.elf"
"$linker" -m elf_i386 --gc-sections --build-id=none -Map="$out/native-link.map" \
  -T "$out/window-linker.ld" -o "$elf" "$out/start.o" "$out/main.o" \
  "$out/register-probe.o" "$out/libgtos_png_pixels.a" > "$out/native-link.log" 2>&1
if [ -n "$("$nm" --undefined-only "$elf")" ]; then
  echo "Undefined symbols in the probe" >&2
  "$nm" --undefined-only "$elf" >&2
  exit 1
fi
"$nm" --undefined-only "$elf" > "$out/native-undefined.log"
objdump -d "$elf" > "$out/native-disassembly.log"
if grep -Eq '%(xmm|ymm|mm|st)[0-9]*\b' "$out/native-disassembly.log"; then
  echo "Prohibited SIMD/FP register in the probe" >&2
  exit 1
fi
if grep -Eq '\t(fadd|fsub|fmul|fdiv|fld|fst|fild|fist|fsin|fcos|fsqrt)' "$out/native-disassembly.log"; then
  echo "Prohibited FP instruction in the probe" >&2
  exit 1
fi
python3 "$repo/tests/native_window_probe_audit.py" "$elf" "$out/native-link.map" \
  "$out/start.o" "$out/main.o" "$out/register-probe.o" \
  "$out"/decode.o "$out"/memory.o "$out"/ashldi3.o "$out"/lshrdi3.o \
  "$out"/udivdi3.o "$out"/udivmoddi4.o "$out"/pixel.o "$out"/skia.o \
  > "$out/native-scalar-audit.log" 2>&1
readelf -h -lW -r "$elf" > "$out/native-readelf.log" 2>&1
stripped="$out/native-window-probe.stripped.elf"
"$objcopy" --strip-all "$elf" "$stripped" > "$out/native-strip.log" 2>&1
python3 tools/browser_artifact.py "$elf" > "$out/inventory.json" 2> "$out/inventory.log"
python3 - "$out" <<'PY'
import json, pathlib, sys
out = pathlib.Path(sys.argv[1])
inventory = json.loads((out / 'inventory.json').read_text())
assert inventory['elf_bits'] == 32 and inventory['machine'] == 'i386', 'Not a real i386 ELF'
assert inventory['elf_type'] == 'EXEC', 'Not a static executable'
assert not inventory['interpreters'], 'Unexpected interpreter'
assert all(s['permissions'] != 'RWX' for s in inventory['segments']), 'Writable+executable segment'
assert set(inventory['requirements']) <= {'zero-filled memory beyond file-backed bytes'}, inventory['requirements']
load_pages = 0
for segment in inventory['segments']:
    if segment['type'] == 'LOAD':
        start = segment['address']; size = segment['memory_bytes']
        load_pages += ((start + size + 4095) // 4096) - (start // 4096)
assert load_pages + 2 <= 256, 'Existing per-process page budget exceeded'
print('load pages:', load_pages)
PY
# The actual repository ELF32 validator, so admission is proven by production code.
"$clang" -std=c11 -O1 -g -c apps/wuffs_gif_probe/validation_host.c -o "$out/host-io.o" \
  > "$out/host-io-build.log" 2>&1
"$clangdir/bin/clang++" -std=c++11 -O1 -g -Iinclude apps/wuffs_gif_probe/validation_host.cpp \
  src/process/elf32.cpp "$out/host-io.o" -o "$out/actual-elf32-validator" \
  > "$out/host-validator-build.log" 2>&1
"$out/actual-elf32-validator" "$stripped" > "$out/actual-elf32-validation.log" 2>&1
grep -q 'REAL GTOS ELF32 VALIDATOR PASS' "$out/actual-elf32-validation.log"

# Real compile inputs, and the probe's own stack-usage evidence.
manifest="$app/native_main.cc $png/start.s $resource_app/register_probe.s \
 $png/linker.ld $png/png_decode.h $png/png_pixel.h $resource_app/resource_oracles.h \
 $repo/include/process/abi.h $repo/include/process/resource_abi.h \
 $repo/include/process/window_abi.h $repo/include/process/window_client.h \
 $repo/include/process/native_window_adapter.h \
 $repo/include/process/vm_abi.h \
 $repo/tools/export-native-graphics-sdk.py \
 $repo/tools/audit-kernel-instructions.py \
 $repo/tools/browser_artifact.py $repo/tests/native_window_probe_audit.py \
 $repo/tests/native_window_probe_build.sh"
sha256sum $manifest > "$out/source-hashes.txt"
# The hash list and the snapshot must agree file for file, so a compile input
# cannot be omitted from the recorded evidence.
[ "$(wc -l < "$out/source-hashes.txt")" -eq "$(printf '%s\n' $manifest | wc -l)" ] \
  || { echo "Source hash list is incomplete" >&2; exit 1; }
for f in $manifest; do
  cp "$f" "$out/source-snapshot/$(basename "$f")"
done
[ "$(ls -1 "$out/source-snapshot" | wc -l)" -eq "$(printf '%s\n' $manifest | wc -l)" ] \
  || { echo "Source snapshot is incomplete" >&2; exit 1; }
# The probe's own stack-usage evidence. Every function that can be on the
# deepest path must appear, so a silently missing measurement is a failure and
# not an empty file.
grep -hE 'png_guest_main|window_requests|publish|rejected_creates|input_copy_stage|reset_copy_stage|close_copy_stage|vm_setup|vm_range|classify|query_info|sayNumber' \
  "$out"/*.su > "$out/stack-usage-key.txt"
for required in png_guest_main publish vm_range classify sayNumber; do
  grep -qF "$required" "$out/stack-usage-key.txt" \
    || { echo "Missing stack-usage measurement for $required" >&2; exit 1; }
done
if grep -vE '\s(static|dynamic,bounded)\s*$' "$out/stack-usage-key.txt"; then
  echo "Unbounded stack usage in the probe" >&2
  exit 1
fi
{
  printf 'elf=%s\n' "$elf"
  printf 'stripped=%s\n' "$stripped"
  printf 'elf_bytes=%s\n' "$(stat -c%s "$elf")"
  printf 'stripped_bytes=%s\n' "$(stat -c%s "$stripped")"
} > "$out/sizes.txt"
sha256sum "$elf" "$stripped" > "$out/elf-hashes.txt"
cat "$out/elf-hashes.txt"
