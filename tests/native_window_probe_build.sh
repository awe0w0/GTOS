#!/bin/bash
# Build the real GTOS i386 native window consumer from the qualified PNG archive.
#
# The pixel source is the same unchanged Wuffs/Skia object graph that the
# published PNG qualification proved; only the consumer translation unit and the
# presentation layer differ. Every digest of the reused objects is re-checked
# against the qualified manifest before the link, so nothing here is "trusted"
# because it happens to be on disk.
#
#   build-native-window-probe.sh FRESH_OUT QUALIFIED_PNG_DIR CLANG_DIR
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: build-native-window-probe.sh FRESH_OUT QUALIFIED_PNG_DIR CLANG_DIR" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
reference="$2"
clangdir="$3"
if [ -e "$out" ]; then
  echo "Choose a fresh output directory: $out" >&2
  exit 2
fi
mkdir -p "$out/source-snapshot"
out="$(realpath "$out")"
reference="$(realpath "$reference")"
clangdir="$(realpath "$clangdir")"
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
  "-I$repo/include" "-I$app" "-I$resource_app" "-I$png"
  "-I$repo/apps/wuffs_gif_probe/freestanding")
"$clang" -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics "${common[@]}" \
  -c "$app/native_main.cc" -o "$out/main.o" > "$out/native-main.log" 2>&1
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
 $repo/include/process/window_abi.h $repo/include/process/vm_abi.h \
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
