#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/gtos-i18n.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
python3 tools/i18n_generate.py --check
compile() {
    "${CXX:-g++}" "$1" -std=c++11 -O2 -Iinclude -mstackrealign -ffreestanding \
      -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector -fno-pie -no-pie \
      -nostdlib -static -Wall -Wextra -Werror -Wl,-e,_start \
      tests/i18n_tests.cpp src/i18n/*.cpp -o "$2"
}
compile -m32 "$build/test32"
runtime="$(pwd)/../gtos-runtime/root"
if [ -x "$runtime/usr/bin/qemu-i386" ]; then
    LD_LIBRARY_PATH="$runtime/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$runtime/usr/bin/qemu-i386" "$build/test32"
elif command -v qemu-i386 >/dev/null 2>&1; then
    qemu-i386 "$build/test32"
else
    "$build/test32"
fi
compile -m64 "$build/test64"
"$build/test64"
if [ "${GTOS_I18N_SANITIZERS:-0}" = 1 ]; then
    "${CXX:-g++}" -m64 -std=c++11 -O1 -g -Iinclude -DGTOS_I18N_SANITIZE \
      -fno-exceptions -fno-rtti -fno-builtin -fno-pie -no-pie -Wall -Wextra -Werror \
      -fsanitize=address,undefined -fno-omit-frame-pointer tests/i18n_tests.cpp \
      src/i18n/*.cpp -o "$build/sanitized"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$build/sanitized"
fi
if [ -n "${GTOS_I18N_FONT:-}" ]; then
    python3 tools/i18n_generate.py --check --font "$GTOS_I18N_FONT"
fi
