#!/bin/bash
# Bounded native window bank test. Real checked-copy, desktop composition and GUI
# closing require actual guest acceptance and are not claimed here.
#
# Every listed check is REQUIRED: any failure or missing item makes the run
# non-zero and marks status.json false. Original command output is never
# overwritten and no failure is swallowed with `|| true`.
#
# Three modes. `all <dir>` builds into a fresh directory, starts what it built and writes its own status, which is what an ordinary
# host wants. `build <dir>` compiles into a directory the caller keeps, records the same evidence and publishes the five images
# through the supply, and `run <run-dir>` starts only those published copies. The split exists because a program created inside a
# step cannot be started inside that step under an execution allow-list; the build step hands the run step its static evidence
# through WINDOW_BUILD_DIR, and the run step writes run logs and the status of its own into the run directory.
set -euo pipefail
stage=${1:-all}
case "$stage" in
    all|build|run) ;;
    *) echo "usage: $0 [all [<dir>]|build <artifact-dir>|run <run-dir>]" >&2; exit 2 ;;
esac
usage() {
  echo "usage: $0 [all [<dir>]|build <artifact-dir>|run <run-dir>]" >&2
  exit 2
}
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The two-step supply lives here: stage_binary publishes an image read-only and records its identity, staged_binary finds it again.
. "$repo/tests/host-runner.sh"
compiler="${CXX:-g++}"
cc="${CC:-gcc}"

# Everything that records what this run used. Kept separate from compiling so that both steps describe their inputs identically.
prepare() {
  local out=$1
  if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null || true)" ]; then
    echo "Refusing to reuse artifact directory: $out" >&2
    exit 2
  fi
  mkdir -p "$out"
  out="$(realpath "$out")"
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
  echo "$out"
}

# The five images this unit has to judge. Named once, so publishing, starting and the read-only checks cannot drift apart.
IMAGES="native-window-wire-c11 native-window-host-x64-O0 native-window-host-x64-O2 native-window-host-i386-O0 native-window-host-i386-O2"

compile_all() {
  local out=$1
  local status=0
  # Required 1: the wire header must compile and assert as C11.
  if "$cc" -std=c11 -O2 -Wall -Wextra -Werror -pedantic -I"$repo/include" \
       "$repo/tests/native_window_wire_c11.c" -o "$out/native-window-wire-c11" \
       > "$out/build-c11.log" 2>&1; then
    :
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
      :
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

  return "$status"
}

# Starting is its own step. `direct` runs an image in place, which is right inside one step on an ordinary host; `published` runs a
# copy the supply handed over, which is the only thing a later step may start.
start_images() {
  local out=$1
  local how=$2
  local status=0
  local image
  for image in $IMAGES; do
    local path log
    case "$image" in
      native-window-wire-c11) log="$out/run-c11.log" ;;
      native-window-host-x64-O0) log="$out/run-x64-O0.log" ;;
      native-window-host-x64-O2) log="$out/run-x64-O2.log" ;;
      native-window-host-i386-O0) log="$out/run-i386-O0.log" ;;
      native-window-host-i386-O2) log="$out/run-i386-O2.log" ;;
    esac
    if [ "$how" = published ]; then
      # One name per image, so a unit that starts published copies cannot pick up the wrong one: each of these is resolved in its own
      # statement, because a substitution inside a command's arguments does not propagate its exit status.
      case "$image" in
        native-window-wire-c11) path=$(staged_binary window-wire-c11) || exit 2 ;;
        native-window-host-x64-O0) path=$(staged_binary window-host-x64-O0) || exit 2 ;;
        native-window-host-x64-O2) path=$(staged_binary window-host-x64-O2) || exit 2 ;;
        native-window-host-i386-O0) path=$(staged_binary window-host-i386-O0) || exit 2 ;;
        native-window-host-i386-O2) path=$(staged_binary window-host-i386-O2) || exit 2 ;;
      esac
    else
      path="$out/$image"
    fi
    case "$image" in
      native-window-host-x64-O*)
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
          "$path" > "$log" 2>&1 || status=1 ;;
      *)
        "$path" > "$log" 2>&1 || status=1 ;;
    esac
  done
  return "$status"
}

case "$stage" in
  all)
    out=${2:-}
    if [ -z "$out" ]; then
      out=$(mktemp -d "${TMPDIR:-/tmp}/gtos-window-host.XXXXXX")
      trap 'rm -rf "$out"' EXIT HUP INT TERM
    fi
    out="$(prepare "$out")"
    status=0
    compile_all "$out" || status=$?
    start_images "$out" direct || status=1
    python3 "$repo/tests/native_window_host_status.py" "$repo" "$out" "$compiler" "$status"
    ;;
  build)
    out=${2:-}
    [ -n "$out" ] || usage
    out="$(prepare "$out")"
    status=0
    compile_all "$out" || status=$?
    [ "$status" -eq 0 ] || exit "$status"
    stage_binary window-wire-c11 "$out/native-window-wire-c11"
    stage_binary window-host-x64-O0 "$out/native-window-host-x64-O0"
    stage_binary window-host-x64-O2 "$out/native-window-host-x64-O2"
    stage_binary window-host-i386-O0 "$out/native-window-host-i386-O0"
    stage_binary window-host-i386-O2 "$out/native-window-host-i386-O2"
    echo "built $out"
    ;;
  run)
    run_dir=${2:-}
    [ -n "$run_dir" ] || usage
    build_dir=${WINDOW_BUILD_DIR:-}
    if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
      echo "run needs WINDOW_BUILD_DIR to hold the static evidence the build step produced" >&2
      exit 2
    fi
    mkdir -p "$run_dir"
    run_dir="$(realpath "$run_dir")"
    # The build step's evidence is read-only here; the run step records its own logs next to a copy of it, so the status report and
    # the images it describes stay in one directory that this step owns.
    cp -a "$build_dir"/. "$run_dir"/
    rm -f "$run_dir"/run-*.log
    status=0
    start_images "$run_dir" published || status=1
    python3 "$repo/tests/native_window_host_status.py" "$repo" "$run_dir" "$compiler" "$status"
    ;;
esac
