#!/bin/bash
# Narrow native-window desktop runner. Unlike desktop_test.sh this keeps every
# artefact: it never uses mktemp and never removes its output directory.
#   native_window_desktop_test.sh FRESH_OUT [GROUP]
# With GROUP the binary exercises only that native window group, which keeps a
# targeted failure cheap; without it the full suite runs.
set -euo pipefail
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: native_window_desktop_test.sh FRESH_ARTIFACT_DIR [GROUP]" >&2
  exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$1"
group="${2:-}"
if [ -e "$out" ]; then
  echo "Refusing to reuse artifact directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
out="$(realpath "$out")"
# desktop_tests.cpp uses .incbin with repository-relative paths.
cd "$repo"
compiler="${CXX:-g++}"
flags=$(cat "$repo/tools/kernel-cxxflags")
"$compiler" --version > "$out/compiler.txt"
printf '%s\n' "$flags" > "$out/kernel-cxxflags.txt"
groupflag=""
if [ -n "$group" ]; then
  groupflag="-DGTOS_DESKTOP_NATIVE_GROUP=\"$group\""
  printf '%s\n' "$group" > "$out/group.txt"
fi
sources="$repo/tests/desktop_tests.cpp $repo/src/drivers/framebuffer.cpp \
 $repo/src/gui/modern_geometry.cpp $repo/src/gui/modern_painter.cpp \
 $repo/src/gui/modern_desktop.cpp $repo/src/gui/modern_render.cpp \
 $repo/src/common/boot_log.cpp $repo/src/apps/package.cpp $repo/src/apps/vm.cpp \
 $repo/src/storage/appstore.cpp $repo/src/storage/settings.cpp \
 $repo/src/process/native_windows.cpp $repo/src/i18n/catalog.cpp \
 $repo/src/i18n/font.cpp $repo/src/i18n/pinyin.cpp $repo/src/i18n/utf8.cpp"
printf '%s\n' "$sources" > "$out/source-list.txt"
manifest="$sources $repo/include/gui/native_window.h $repo/include/process/window_abi.h \
 $repo/include/process/native_windows.h $repo/include/process/native_window_memory.h \
 $repo/include/gui/modern_desktop.h $repo/include/gui/modern_geometry.h \
 $repo/include/gui/native_image.h $repo/include/gui/modern_painter.h \
 $repo/include/drivers/framebuffer.h $repo/tools/kernel-cxxflags \
 $repo/tests/native_window_desktop_test.sh $repo/tests/native_window_desktop_status.py"
sha256sum $manifest > "$out/source-hashes.txt"
mkdir -p "$out/source-snapshot"
for f in $manifest; do
  cp "$f" "$out/source-snapshot/$(basename "$f")"
done
# Record the exact argv so the run can be reproduced without guessing.
{
  printf 'compiler: %s\n' "$compiler"
  printf 'flags: %s\n' "$flags"
  printf 'sources: %s\n' "$sources"
  printf 'group: %s\n' "${group:-<full suite>}"
} > "$out/argv.txt"

status=0
if "$compiler" $flags -std=c++11 -O2 -I"$repo/include" \
     -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST -mstackrealign -ffreestanding \
     -fno-exceptions -fno-rtti -fno-builtin -fno-use-cxa-atexit -fno-stack-protector \
     -fno-pie -no-pie -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start -m32 \
     $groupflag $sources -o "$out/native-window-desktop-i386" \
     > "$out/build-i386.log" 2>&1; then
  readelf -h "$out/native-window-desktop-i386" > "$out/readelf-i386.log" 2>&1 || status=1
  if "$out/native-window-desktop-i386" > "$out/run-i386.log" 2>&1; then
    printf '0\n' > "$out/native-exit.txt"
  else
    native_status=$?
    status=$native_status
    printf '%s\n' "$native_status" > "$out/native-exit.txt"
  fi
else
  status=1
  printf 'build\n' > "$out/native-exit.txt"
fi
# Stack-usage evidence for the synchronisation path: compile just the desktop
# translation unit to an object inside this OUT so the .su lands here.
mkdir -p "$out/stack"
if "$compiler" $flags -std=c++11 -O2 -I"$repo/include" -DGTOS_DESKTOP_HOST_TEST \
     -DGTOS_MEMORY_TEST -mstackrealign -ffreestanding -fno-exceptions -fno-rtti \
     -fno-builtin -fno-use-cxa-atexit -fno-stack-protector -fno-pie -m32 \
     -fstack-usage -c "$repo/src/gui/modern_desktop.cpp" \
     -o "$out/stack/modern_desktop.o" > "$out/stack/build.log" 2>&1; then
  for su in "$out/stack"/*.su; do
    [ -e "$su" ] || continue
    grep -E 'SyncNativeWindows|SyncNativeFocus|RetireNativeSlot|ForgetNativeWindow|DrawNativeSlot|ApplyRouteResult' \
      "$su" >> "$out/stack-usage-key.txt" 2>/dev/null || true
  done
else
  status=1
fi

python3 "$repo/tests/native_window_desktop_status.py" "$repo" "$out" "$status"
