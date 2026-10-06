#!/bin/sh
# Builds a fixture; guest execution is a separate acceptance step.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$repo/obj/browser-probe"}
mkdir -p "$out"
${CC:-gcc} -m32 -std=c11 -Os -Wall -Wextra -Werror -ffreestanding \
    -nostdlib -fno-builtin -fno-pie -fno-stack-protector \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c "$repo/apps/browser_probe/main.c" -o "$out/main.o"
${AS:-as} --32 "$repo/apps/browser_probe/start.s" -o "$out/start.o"
${LD:-ld} -melf_i386 --build-id=none -z max-page-size=4096 \
    -T "$repo/apps/browser_probe/linker.ld" -o "$out/browser-probe.elf" \
    "$out/start.o" "$out/main.o"
${READELF:-readelf} -h -l -r -s "$out/browser-probe.elf" > "$out/readelf.txt"
${OBJDUMP:-objdump} -d "$out/browser-probe.elf" > "$out/disassembly.txt"
${PYTHON:-python3} "$repo/tools/browser_artifact.py" "$out/browser-probe.elf" \
    --output "$out/inventory.json"
sha256sum "$out/browser-probe.elf" > "$out/sha256.txt"
printf 'Built provisional ABI fixture: %s\nGuest execution has NOT been performed by this script.\n' "$out/browser-probe.elf"
