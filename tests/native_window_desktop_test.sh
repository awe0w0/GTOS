#!/bin/bash
# Narrow native-window desktop runner. Unlike desktop_test.sh this keeps every
# artefact: it never uses mktemp and never removes its output directory.
#   native_window_desktop_test.sh FRESH_OUT [GROUP]
#
# Two optional steps. `build FRESH_OUT [GROUP]` compiles, records the evidence and publishes the image through the execution
# supply, starting nothing; `run RUN_DIR` starts only that published copy, with the build step's directory passed read-only as
# WINDOW_DESKTOP_BUILD_DIR, and writes its run log, its native exit code and the status report into the run directory. The split
# exists because a program created inside a step cannot be started inside that step under an execution allow-list.
set -euo pipefail
stage=all
case "${1:-}" in
  build|run) stage=$1; shift ;;
esac
usage() {
  echo "usage: native_window_desktop_test.sh FRESH_ARTIFACT_DIR [GROUP]" >&2
  echo "       native_window_desktop_test.sh build FRESH_ARTIFACT_DIR [GROUP]" >&2
  echo "       native_window_desktop_test.sh run RUN_DIR   (with WINDOW_DESKTOP_BUILD_DIR set)" >&2
  exit 2
}
if [ "$stage" = run ]; then
  [ "$#" -eq 1 ] || usage
  run_dir="$1"
else
  if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
  fi
  out="$1"
  group="${2:-}"
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The two-step supply lives here: stage_binary publishes an image read-only and records its identity, staged_binary finds it again.
. "$repo/tests/host-runner.sh"
compiler="${CXX:-g++}"
flags=$(cat "$repo/tools/kernel-cxxflags")

# Everything that records what this run used. Both steps describe their inputs identically, so the evidence is comparable.
prepare() {
  local out=$1
  local group=$2
  if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null || true)" ]; then
    echo "Refusing to reuse artifact directory: $out" >&2
    exit 2
  fi
  mkdir -p "$out"
  out="$(realpath "$out")"
  # desktop_tests.cpp uses .incbin with repository-relative paths.
  cd "$repo"
  "$compiler" --version > "$out/compiler.txt"
  printf '%s\n' "$flags" > "$out/kernel-cxxflags.txt"
  local groupflag=""
  if [ -n "$group" ]; then
    groupflag="-DGTOS_DESKTOP_NATIVE_GROUP=\"$group\""
    printf '%s\n' "$group" > "$out/group.txt"
  fi
  local sources="$repo/tests/desktop_tests.cpp $repo/src/drivers/framebuffer.cpp \
 $repo/src/gui/modern_geometry.cpp $repo/src/gui/modern_painter.cpp \
 $repo/src/gui/modern_desktop.cpp $repo/src/gui/modern_render.cpp $repo/src/gui/command_shell.cpp \
 $repo/src/common/boot_log.cpp $repo/src/apps/package.cpp $repo/src/apps/vm.cpp \
 $repo/src/storage/appstore.cpp $repo/src/storage/settings.cpp $repo/src/storage/gpt.cpp $repo/src/storage/ntfs.cpp \
 $repo/src/process/native_windows.cpp $repo/src/i18n/catalog.cpp \
 $repo/src/i18n/font.cpp $repo/src/i18n/pinyin.cpp $repo/src/i18n/utf8.cpp"
  printf '%s\n' "$sources" > "$out/source-list.txt"
  local manifest="$sources $repo/include/gui/native_window.h $repo/include/process/window_abi.h \
 $repo/include/process/native_windows.h $repo/include/process/native_window_memory.h \
 $repo/include/gui/modern_desktop.h $repo/include/gui/modern_geometry.h $repo/include/gui/command_shell.h \
 $repo/include/storage/readable_disk.h $repo/include/storage/gpt.h $repo/include/storage/ntfs.h \
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
  echo "$out"
}

compile_all() {
  local out=$1
  local group=$2
  local status=0
  local groupflag=""
  if [ -n "$group" ]; then
    groupflag="-DGTOS_DESKTOP_NATIVE_GROUP=\"$group\""
  fi
  if "$compiler" $flags -std=c++11 -O2 -I"$repo/include" \
       -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST -mstackrealign -ffreestanding \
       -fno-exceptions -fno-rtti -fno-builtin -fno-use-cxa-atexit -fno-stack-protector \
       -fno-pie -no-pie -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start -m32 \
       $groupflag $(cat "$out/source-list.txt") -o "$out/native-window-desktop-i386" \
       > "$out/build-i386.log" 2>&1; then
    readelf -h "$out/native-window-desktop-i386" > "$out/readelf-i386.log" 2>&1 || status=1
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
  return "$status"
}

# Starting is its own step. The image is static i386, so on a host with a 32-bit capable kernel it runs directly.
start_image() {
  local out=$1
  shift
  local status=0
  if "$@" > "$out/run-i386.log" 2>&1; then
    printf '0\n' > "$out/native-exit.txt"
  else
    local native_status=$?
    status=$native_status
    printf '%s\n' "$native_status" > "$out/native-exit.txt"
  fi
  return "$status"
}

case "$stage" in
  all)
    out="$(prepare "$out" "$group")"
    status=0
    if compile_all "$out" "$group"; then
      start_image "$out" "$out/native-window-desktop-i386" || status=$?
    else
      status=$?
    fi
    python3 "$repo/tests/native_window_desktop_status.py" "$repo" "$out" "$status"
    ;;
  build)
    out="$(prepare "$out" "$group")"
    compile_all "$out" "$group"
    stage_binary window-desktop-i386 "$out/native-window-desktop-i386"
    echo "built $out"
    ;;
  run)
    build_dir=${WINDOW_DESKTOP_BUILD_DIR:-}
    if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
      echo "run needs WINDOW_DESKTOP_BUILD_DIR to hold the static evidence the build step produced" >&2
      exit 2
    fi
    image=$(staged_binary window-desktop-i386) || exit 2
    verify_publish_record "$image" || exit 2
    mkdir -p "$run_dir"
    run_dir="$(realpath "$run_dir")"
    # The build step's evidence is read-only here; this step keeps a copy next to its own log so the status report and the image it
    # describes stay in one directory the run owns.
    cp -a "$build_dir"/. "$run_dir"/
    rm -f "$run_dir"/run-i386.log "$run_dir"/native-exit.txt
    cp "$image" "$run_dir/native-window-desktop-i386"
    status=0
    start_image "$run_dir" run_published_binary "$image" || status=$?
    python3 "$repo/tests/native_window_desktop_status.py" "$repo" "$run_dir" "$status"
    ;;
esac
