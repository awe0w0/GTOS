#!/bin/bash
# Bounded native window bank test. Real checked-copy, desktop composition and GUI
# closing require actual guest acceptance and are not claimed here.
#
# Every listed check is REQUIRED: any failure or missing item makes the run
# non-zero and marks status.json false. Original command output is never
# overwritten and no failure is swallowed with `|| true`.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo "usage: native_window_host_test.sh FRESH_ARTIFACT_DIR" >&2
  exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$1"
if [ -e "$out" ]; then
  echo "Refusing to reuse artifact directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
out="$(realpath "$out")"
compiler="${CXX:-g++}"
cc="${CC:-gcc}"
"$compiler" --version > "$out/compiler.txt"
printf 'CXX=%q CC=%q bash %q %q\n' "$compiler" "$cc" \
  "$repo/tests/native_window_host_test.sh" "${out}-reproduced" > "$out/reproduce.sh"
sha256sum "$repo/include/process/window_abi.h" "$repo/include/process/native_windows.h" \
  "$repo/include/gui/native_window.h" "$repo/include/gui/native_image.h" \
  "$repo/include/memory/criticalsection.h" "$repo/src/process/native_windows.cpp" \
  "$repo/tests/native_window_host.cpp" "$repo/tests/native_window_wire_c11.c" \
  "$repo/tests/native_window_host_test.sh" "$repo/tests/native_window_host_status.py" \
  > "$out/source-hashes.txt"
# Keep the exact bytes this run used, not only their hashes, so a later edit
# cannot orphan this evidence.
mkdir -p "$out/source-snapshot"
for rel in include/process/window_abi.h include/process/native_windows.h \
           include/gui/native_window.h include/gui/native_image.h \
           include/memory/criticalsection.h src/process/native_windows.cpp \
           tests/native_window_host.cpp tests/native_window_wire_c11.c \
           tests/native_window_host_test.sh tests/native_window_host_status.py; do
  cp "$repo/$rel" "$out/source-snapshot/$(echo "$rel" | tr / _)"
done

status=0
# Required 1: the wire header must compile and assert as C11.
if "$cc" -std=c11 -O2 -Wall -Wextra -Werror -pedantic -I"$repo/include" \
     "$repo/tests/native_window_wire_c11.c" -o "$out/native-window-wire-c11" \
     > "$out/build-c11.log" 2>&1; then
  "$out/native-window-wire-c11" > "$out/run-c11.log" 2>&1 || status=1
else
  status=1
fi

# Required 2: x64 O0/O2 with sanitizers.
for level in 0 2; do
  if "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic \
       -DGTOS_WINDOW_HOST_TEST -fsanitize=address,undefined \
       -fno-omit-frame-pointer -fno-pie -no-pie -I"$repo/include" \
       "$repo/src/process/native_windows.cpp" "$repo/tests/native_window_host.cpp" \
       -o "$out/native-window-host-x64-O$level" > "$out/build-x64-O$level.log" 2>&1; then
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
      "$out/native-window-host-x64-O$level" > "$out/run-x64-O$level.log" 2>&1 || status=1
  else
    status=1
  fi
done

# Required 3: actual i386 ELF32 execution of the same cases, both levels.
for level in 0 2; do
  if "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic -m32 \
       -DGTOS_WINDOW_HOST_TEST -DGTOS_WINDOW_I386_HOST -ffreestanding -fno-builtin \
       -fno-rtti -fno-exceptions -fno-use-cxa-atexit -fno-stack-protector -fno-pie -no-pie \
       -mno-sse -mno-mmx -msoft-float -nostdlib -Wl,-e,_start -I"$repo/include" \
       "$repo/src/process/native_windows.cpp" "$repo/tests/native_window_host.cpp" \
       -o "$out/native-window-host-i386-O$level" > "$out/build-i386-O$level.log" 2>&1; then
    readelf -h "$out/native-window-host-i386-O$level" > "$out/readelf-i386-O$level.log" 2>&1 || status=1
    "$out/native-window-host-i386-O$level" > "$out/run-i386-O$level.log" 2>&1 || status=1
  else
    status=1
  fi
done

# Required 4: production i386 object with the real guard, no heap imports.
if "$compiler" -std=c++11 -O2 -m32 -ffreestanding -fno-builtin -fno-rtti -fno-exceptions \
     -fno-use-cxa-atexit -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables \
     -mno-sse -mno-mmx -msoft-float -I"$repo/include" -c "$repo/src/process/native_windows.cpp" \
     -o "$out/native-windows-production-i386.o" > "$out/build-production-i386.log" 2>&1; then
  nm -u -C "$out/native-windows-production-i386.o" > "$out/production-undefined-symbols.txt"
  objdump -d -C "$out/native-windows-production-i386.o" > "$out/production-disassembly.txt"
else
  status=1
fi

"$compiler" -std=c++11 -O2 -Wall -Wextra -Werror -pedantic -DGTOS_WINDOW_HOST_TEST \
  -I"$repo/include" -fsyntax-only "$repo/tests/native_window_host.cpp" \
  > "$out/syntax-only.log" 2>&1 || status=1

python3 "$repo/tests/native_window_host_status.py" "$repo" "$out" "$compiler" "$status"
