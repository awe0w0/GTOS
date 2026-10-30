#!/bin/bash
# New bounded surface service only; real checked-copy/display require QEMU.
#
# Three modes. `all` builds into a fresh directory, starts what it built and keeps nothing, which is what a normal host wants. `build
# <dir>` compiles into a directory the caller keeps and publishes the four images through the supply, and `run <run-dir>` starts the
# published copies and re-checks the static evidence the build produced. The split exists because a program created inside a step
# cannot be started inside that step under an execution allow-list; the build stage hands the run stage the static artifacts it needs
# through SURFACE_BUILD_DIR, and the run stage writes new logs of its own.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$repo/tests/host-runner.sh"

stage=${1:-all}
compiler="${CXX:-g++}"

usage() {
    echo "usage: $0 [all|build <artifact-dir>|run <run-dir>]" >&2
    exit 2
}

compile_all() { # <out>
    local out=$1
    "$compiler" --version > "$out/compiler.txt"
    printf 'CXX=%q bash %q %q\n' "$compiler" "$repo/tests/native_surface_host_test.sh" "${out}-reproduced" > "$out/reproduce.sh"
    sha256sum "$repo/include/process/surface_abi.h" "$repo/include/process/native_surface.h" \
        "$repo/include/gui/native_image.h" "$repo/include/memory/criticalsection.h" \
        "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
        "$repo/tests/native_surface_host_test.sh" > "$out/source-hashes.txt"
    for level in 0 2; do
        "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic \
            -DGTOS_SURFACE_HOST_TEST -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
            -I"$repo/include" "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
            -o "$out/native-surface-host-x64-O$level" > "$out/build-x64-O$level.log" 2>&1
        "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic -m32 \
            -DGTOS_SURFACE_HOST_TEST -DGTOS_SURFACE_I386_HOST -ffreestanding -fno-builtin \
            -fno-rtti -fno-exceptions -fno-use-cxa-atexit -fno-stack-protector -fno-pie -no-pie \
            -mno-sse -mno-mmx -msoft-float -nostdlib -Wl,-e,_start \
            -I"$repo/include" "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
            "$repo/tests/i386_pure_virtual_runtime.cpp" \
            -o "$out/native-surface-host-i386-O$level" > "$out/build-i386-O$level.log" 2>&1
        readelf -h "$out/native-surface-host-i386-O$level" > "$out/readelf-i386-O$level.log"
    done
    # Compile actual production i386 guards, without the host-only substitutions.
    "$compiler" -std=c++11 -O2 -m32 -ffreestanding -fno-builtin -fno-rtti -fno-exceptions \
        -fno-use-cxa-atexit -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables \
        -mno-sse -mno-mmx -msoft-float -I"$repo/include" -c "$repo/src/process/native_surface.cpp" \
        -o "$out/native-surface-production-i386.o" > "$out/build-production-i386.log" 2>&1
    nm -u -C "$out/native-surface-production-i386.o" > "$out/production-undefined-symbols.txt"
    objdump -d -C "$out/native-surface-production-i386.o" > "$out/production-disassembly.txt"
    sha256sum "$out/native-surface-host-"*"-O"[02] "$out/native-surface-production-i386.o" > "$out/binary-hashes.txt"
}

start_image() { # <image> <log>
    local image=$1 log=$2
    case $(basename "$image") in
        *i386*) "$image" > "$log" 2>&1 ;;
        *) ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
               "$image" > "$log" 2>&1 ;;
    esac
}

check_evidence() { # <build-dir> <run-dir>
    python3 - "$repo" "$1" "$2" "$compiler" <<'PY'
import json
import os
from pathlib import Path
import re
import sys
repo, build, run, compiler = sys.argv[1:]
build = Path(build)
run = Path(run)
undefined = (build / 'production-undefined-symbols.txt').read_text()
assert not re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', undefined), undefined
sections = {}
name = None
for line in (build / 'production-disassembly.txt').read_text().splitlines():
    label = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
    if label:
        name = label.group(1)
        sections[name] = []
    elif name is not None:
        sections[name].append(line)
guarded = ['SetAvailable', 'Begin', 'ValidateWrite', 'Write', 'Present', 'Abort', 'ReclaimOwner', 'CopyLatest']
for method in guarded:
    bodies = [body for name, body in sections.items()
              if 'gtos::process::NativeSurfaceBank::' + method + '(' in name]
    assert len(bodies) == 1, (method, len(bodies))
    body = '\n'.join(bodies[0])
    assert re.search(r'\bcli\b', body) and re.search(r'\bsti\b', body), method
logs = {}
for architecture, bits in [('x64', 64), ('i386', 32)]:
    for level in ['O0', 'O2']:
        key = architecture + '-' + level
        logs[key] = (run / ('run-' + key + '.log')).read_text().strip()
        assert 'NATIVE SURFACE HOST PASS' in logs[key] and 'pointer_bits=' + str(bits) in logs[key]
        if architecture == 'i386':
            # The image that ran is the published copy the supply handed over, named by the environment.
            elf = Path(os.environ['SURFACE_IMAGE_I386_' + level]).read_bytes()
            assert elf[:6] == b'\x7fELF\x01\x01' and int.from_bytes(elf[18:20], 'little') == 3
status = dict(all_required_checks_pass=True, source=repo, compiler=compiler,
    scope='Actual pure surface service; native checked-copy, BSP admission and GUI closing require real guest acceptance',
    optimizations=['O0', 'O2'], x64_sanitizers=['address', 'undefined'],
    i386_execution='Actual Linux ELF32, same test/service code, freestanding int80 report/exit, no missing multilib CRT',
    i386_sanitizers=[], production_guard_methods=guarded, production_no_heap_imports=True,
    logs=logs)
(run / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
print('Evidence:', run / 'status.json')
PY
}

case $stage in
all)
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/gtos-surface-host.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    compile_all "$tmp/build"
    mkdir -p "$tmp/run"
    for architecture in x64 i386; do
        for level in 0 2; do
            start_image "$tmp/build/native-surface-host-$architecture-O$level" "$tmp/run/run-$architecture-O$level.log"
            cat "$tmp/run/run-$architecture-O$level.log"
        done
    done
    SURFACE_IMAGE_I386_O0="$tmp/build/native-surface-host-i386-O0" \
    SURFACE_IMAGE_I386_O2="$tmp/build/native-surface-host-i386-O2" \
        check_evidence "$tmp/build" "$tmp/run"
    ;;
build)
    out=${2:-}
    [ -n "$out" ] || usage
    if [ -e "$out" ]; then
        echo "Refusing to reuse artifact directory: $out" >&2
        exit 2
    fi
    mkdir -p "$out"
    out="$(realpath "$out")"
    compile_all "$out"
    for architecture in x64 i386; do
        for level in 0 2; do
            stage_binary "surface-host-$architecture-O$level" "$out/native-surface-host-$architecture-O$level"
        done
    done
    echo "built $out"
    ;;
run)
    run_dir=${2:-}
    [ -n "$run_dir" ] || usage
    mkdir -p "$run_dir"
    build_dir=${SURFACE_BUILD_DIR:-}
    if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
        echo "run needs SURFACE_BUILD_DIR to hold the static evidence the build stage produced" >&2
        exit 2
    fi
    images=
    for architecture in x64 i386; do
        for level in 0 2; do
            staged=$(staged_binary "surface-host-$architecture-O$level") || exit 2
            images="$images $staged"
            start_image "$staged" "$run_dir/run-$architecture-O$level.log"
            cat "$run_dir/run-$architecture-O$level.log"
        done
    done
    # Assigned before use on purpose: a substitution inside a command's arguments does not propagate its exit status, so a missing
    # published image would become an empty environment value instead of a refusal.
    i386_o0=$(staged_binary surface-host-i386-O0) || exit 2
    i386_o2=$(staged_binary surface-host-i386-O2) || exit 2
    SURFACE_BUILD_DIR=$build_dir \
    SURFACE_IMAGE_I386_O0="$i386_o0" \
    SURFACE_IMAGE_I386_O2="$i386_o2" \
        check_evidence "$build_dir" "$run_dir"
    ;;
*)
    usage
    ;;
esac
