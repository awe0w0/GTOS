#!/bin/sh
# The desktop unit: the catalog inventory, then the same test body built at both widths.
#
# The building and the starting are two steps when the caller says so. Under an execution allow-list the program a step has just
# linked cannot be started by that step — it was not on the list when the list was made — so `build` compiles, publishes each image
# and records where it went, and `run` starts only those published images. With no argument the script builds and runs in one go,
# which is what an ordinary host does and what the repository's own `make test` does.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
kernel_flags=$(cat tools/kernel-cxxflags)
stage=${1:-all}
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
compile() {
    status_define=
    if [ -n "${GTOS_DESKTOP_EXIT_CODE:-}" ]; then status_define="-DGTOS_DESKTOP_EXIT_CODE=$GTOS_DESKTOP_EXIT_CODE"; fi
    "${CXX:-g++}" $kernel_flags "$1" $status_define -std=c++11 -O2 -Iinclude -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST \
      -mstackrealign -ffreestanding -fno-use-cxa-atexit -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector \
      -fno-pie -no-pie -nostdlib -static -Wall -Wextra \
      -Werror -Wl,-e,_start tests/desktop_tests.cpp src/drivers/framebuffer.cpp \
      src/gui/modern_geometry.cpp src/gui/modern_painter.cpp src/gui/modern_desktop.cpp \
      src/gui/modern_render.cpp src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp \
      src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp \
      src/process/native_windows.cpp src/process/native_surface.cpp -o "$2"
}
# The strict sanitizer build is the same test body with address and undefined-behaviour instrumentation. It is a function so that the
# three ways of asking for it -- a local run, the build step, and the run step -- compile exactly the same thing; while it was spelled
# out only in the local branch, the two-step supply had no way to publish or start it at all.
compile_sanitized() {
    "${CXX:-g++}" $kernel_flags -m64 -std=c++11 -O1 -g -Iinclude \
      -DGTOS_DESKTOP_HOST_TEST -DGTOS_MEMORY_TEST -DGTOS_DESKTOP_SANITIZE -fno-exceptions -fno-rtti \
      -fno-builtin -fno-pie -no-pie -Wall -Wextra -Werror \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      tests/desktop_tests.cpp src/drivers/framebuffer.cpp src/gui/modern_geometry.cpp \
      src/gui/modern_painter.cpp src/gui/modern_desktop.cpp src/gui/modern_render.cpp \
      src/common/boot_log.cpp src/apps/package.cpp src/apps/vm.cpp src/storage/appstore.cpp src/storage/settings.cpp src/i18n/*.cpp \
      src/process/native_windows.cpp src/process/native_surface.cpp -o "$1"
}

if [ "$stage" = build ] || [ "$stage" = all ]; then
    python3 tests/desktop_catalog_test.py
    compile -m32 "$build/test32"
    compile -m64 "$build/test64"
    if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
        compile_sanitized "$build/sanitized"
    fi
fi
if [ "$stage" = build ]; then
    stage_binary desktop32 "$build/test32"
    stage_binary desktop64 "$build/test64"
    if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
        stage_binary desktop-sanitized "$build/sanitized"
    fi
    exit 0
fi
# The supply has its own status. Every other status a caller sees is the status of the program that was started, verbatim: a suite that
# collapses them cannot tell "the image was refused" from "the program said 2".
GTOS_EXIT_SUPPLY=70
if [ "$stage" = run ]; then
    # The images are resolved and verified before anything starts: the manifest names them and the digest recorded with each name is the
    # identity of the bytes that run. A refusal here is the supply's, and it is reported as such rather than as the program's answer.
    desktop32=$(staged_binary desktop32) || exit "$GTOS_EXIT_SUPPLY"
    desktop64=$(staged_binary desktop64) || exit "$GTOS_EXIT_SUPPLY"

    # The i386 executor, if this host has one: a named regular file when GTOS_DESKTOP_I386_EXECUTOR is given (that is what a strict run
    # uses, because the name is part of the grant), otherwise whatever the runtime tree or PATH provides.
    # The record is checked before anything starts. A manifest can name an image whose record entry is missing or whose bytes no longer
    # match what was published; without this check that refusal would surface as the program's own 2 and be read as a real status.
    verify_publish_record "$desktop32" || exit "$GTOS_EXIT_SUPPLY"
    verify_publish_record "$desktop64" || exit "$GTOS_EXIT_SUPPLY"

    executor=
    if [ "${GTOS_DESKTOP_STRICT:-0}" = 1 ]; then
        # A strict run says what it starts: the executor is the file named here, resolved, and nothing found by a search stands in for it.
        if [ -z "${GTOS_DESKTOP_I386_EXECUTOR:-}" ]; then
            echo "desktop_test: a strict run needs GTOS_DESKTOP_I386_EXECUTOR to name the i386 executor" >&2
            exit "$GTOS_EXIT_SUPPLY"
        fi
        if [ -L "$GTOS_DESKTOP_I386_EXECUTOR" ] || [ ! -f "$GTOS_DESKTOP_I386_EXECUTOR" ] || [ ! -x "$GTOS_DESKTOP_I386_EXECUTOR" ]; then
            echo "desktop_test: GTOS_DESKTOP_I386_EXECUTOR is not an executable regular file: $GTOS_DESKTOP_I386_EXECUTOR" >&2
            exit "$GTOS_EXIT_SUPPLY"
        fi
        executor=$GTOS_DESKTOP_I386_EXECUTOR
    elif [ -n "${GTOS_DESKTOP_I386_EXECUTOR:-}" ]; then
        if [ -L "$GTOS_DESKTOP_I386_EXECUTOR" ] || [ ! -f "$GTOS_DESKTOP_I386_EXECUTOR" ] || [ ! -x "$GTOS_DESKTOP_I386_EXECUTOR" ]; then
            echo "desktop_test: GTOS_DESKTOP_I386_EXECUTOR is not an executable regular file: $GTOS_DESKTOP_I386_EXECUTOR" >&2
            exit "$GTOS_EXIT_SUPPLY"
        fi
        executor=$GTOS_DESKTOP_I386_EXECUTOR
    else
        runtime="$(pwd)/../gtos-runtime/root"
        if [ -x "$runtime/usr/bin/qemu-i386" ]; then
            executor=$runtime/usr/bin/qemu-i386
        elif command -v qemu-i386 >/dev/null 2>&1; then
            executor=$(command -v qemu-i386)
        fi
    fi

    # The 32-bit width. Its status is the program's status: 2 is 2, not a refusal, and 126 from this kernel means the host could not
    # execute the image at all, which is reported as that rather than folded into a pass.
    width32=0
    echo "desktop_test: starting the 32-bit image"
    if [ -n "$executor" ]; then
        GTOS_HOST_LAUNCHER=$executor
        if run_published_binary "$desktop32"; then width32=0; else width32=$?; fi
        GTOS_HOST_LAUNCHER=
        if [ "$width32" -eq 126 ]; then
            echo "desktop_test: the executor refused the 32-bit image (126)" >&2
        fi
        [ "$width32" -eq 0 ] || exit "$width32"
    else
        if run_published_binary "$desktop32"; then width32=0; else width32=$?; fi
        if [ "$width32" -eq 126 ] && [ "$(uname -m)" = x86_64 ]; then
            # The image is not executable on this host. That is a fact about the host, and it is recorded as one; the 64-bit width still
            # has to run, and the qualification of the 32-bit width is not claimed from a run that did not happen.
            echo "desktop_test: this host cannot execute the 32-bit image (126); the 32-bit width did not run" >&2
            width32=126
        elif [ "$width32" -ne 0 ]; then
            exit "$width32"
        fi
    fi

    # The 64-bit width runs whether or not the 32-bit one did: one width answering for the other is exactly the substitution this unit
    # must not make.
    echo "desktop_test: starting the 64-bit image"
    if run_published_binary "$desktop64"; then status=0; else status=$?; fi
    [ "$status" -eq 0 ] || exit "$status"
    if [ "$width32" -eq 126 ] && [ "${GTOS_DESKTOP_STRICT:-0}" = 1 ]; then
        exit "$GTOS_EXIT_SUPPLY"
    fi
if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
        # Resolved in its own statement: an assignment's status is the substitution's, but inside a command's arguments it is not.
        sanitized=$(staged_binary desktop-sanitized) || exit "$GTOS_EXIT_SUPPLY"
        ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run_host_binary "$sanitized"
    fi
    exit 0
fi
if [ "$stage" != all ]; then
    echo "usage: $0 [build|run]" >&2
    exit 2
fi

runtime="$(pwd)/../gtos-runtime/root"
if [ -x "$runtime/usr/bin/qemu-i386" ]; then
    LD_LIBRARY_PATH="$runtime/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$runtime/usr/bin/qemu-i386" "$build/test32"
elif command -v qemu-i386 >/dev/null 2>&1; then
    qemu-i386 "$build/test32"
else
    status=0
    "$build/test32" || status=$?
    if [ "$status" -eq 126 ] && [ "$(uname -m)" = x86_64 ]; then
        compile -m64 "$build/test64"
        run_host_binary "$build/test64"
        exit 0
    else
        [ "$status" -eq 0 ] || exit "$status"
    fi
fi

run_host_binary "$build/test64"
if [ "${GTOS_DESKTOP_SANITIZERS:-0}" = 1 ]; then
    # The local entry compiles and starts in one go, which is what an ordinary host does; the two-step supply above compiles it in the
    # build step and starts the published copy in the run step. Both use the same function.
    compile_sanitized "$build/sanitized"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run_host_binary "$build/sanitized"
fi
