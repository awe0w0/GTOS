#!/bin/bash
# Standalone checks for the new resource service only; no frozen workloads.
#
# Three modes. `all <png> <dir>` builds, starts and reports in one directory, which is what an ordinary host does and what this
# script did before. `build <png> <dir>` compiles into a directory the caller keeps and publishes both images through the supply,
# starting nothing. `run <png> <run-dir>` starts only those published copies and writes its own run logs and status report. The
# split exists because a program created inside a step cannot be started inside that step under an execution allow-list.
set -euo pipefail
stage=all
case "${1:-}" in
  all|build|run) stage=$1; shift ;;
esac
usage() {
  echo "usage: $0 [all|build] INDEPENDENT.png FRESH_ARTIFACT_DIR" >&2
  echo "       $0 run INDEPENDENT.png RUN_DIR   (with RESOURCE_BUILD_DIR set)" >&2
  exit 2
}
if [ "$#" -ne 2 ]; then
  usage
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The two-step supply lives here: stage_binary publishes an image read-only and records its identity, staged_binary finds it again.
. "$repo/tests/host-runner.sh"
expected="$(realpath "$1")"
out="$2"
test -f "$expected"
compiler="${CXX:-g++}"

# Everything that records what this run used, so both steps describe their inputs identically.
prepare() {
  local out=$1
  if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null || true)" ]; then
    echo "Refusing to reuse artifact directory: $out" >&2
    exit 2
  fi
  mkdir -p "$out"
  out="$(realpath "$out")"
  "$compiler" --version > "$out/compiler.txt"
  printf 'CXX=%q bash %q %q %q\n' "$compiler" "$repo/tests/native_resource_host_test.sh" "$expected" "${out}-reproduced" > "$out/reproduce.sh"
  sha256sum "$repo/include/process/resource_abi.h" "$repo/include/process/resources.h" \
    "$repo/src/process/resources.cpp" "$repo/src/process/resources_png.inc" \
    "$repo/src/process/native_runtime.cpp" "$repo/src/process/native_realtime.cpp" "$repo/include/process/native_realtime.h" "$repo/include/process/realtime_abi.h" "$repo/tests/native_resource_host.cpp" \
    "$repo/tests/native_resource_host_test.sh" "$expected" > "$out/source-hashes.txt"
  echo "$out"
}

compile_all() {
  local out=$1
  for level in 0 2; do
    "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic \
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
      -I"$repo/include" "$repo/src/process/resources.cpp" "$repo/tests/native_resource_host.cpp" \
      -o "$out/native-resource-host-O$level" > "$out/build-O$level.log" 2>&1
  done
}

# Starting is its own step: `direct` runs the images in place, `published` runs the copies the supply handed over.
start_images() {
  local out=$1
  local how=$2
  local level path
  for level in 0 2; do
    if [ "$how" = published ]; then
      # Resolved in its own statement: a substitution inside a command's arguments does not propagate its exit status, so a missing
      # published image would become an empty argument instead of a refusal.
      path=$(staged_binary "resource-host-O$level") || exit 2
    else
      path="$out/native-resource-host-O$level"
    fi
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
      "$path" "$expected" > "$out/run-O$level.log" 2>&1
    cat "$out/run-O$level.log"
  done
}

report() {
  local out=$1
  sha256sum "$out/native-resource-host-O0" "$out/native-resource-host-O2" > "$out/binary-hashes.txt"
  python3 - "$repo" "$expected" "$out" "$compiler" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

repo, expected, out, compiler = sys.argv[1:]
out = Path(out)
status = {
    'all_required_checks_pass': True,
    'scope': 'new pure immutable resource service; checked-copy requires real guest acceptance',
    'source': repo,
    'expected_png': expected,
    'expected_png_sha256': hashlib.sha256(Path(expected).read_bytes()).hexdigest(),
    'compiler': compiler,
    'optimizations': ['O0', 'O2'],
    'sanitizers': ['address', 'undefined'],
    'logs': {level: (out / ('run-' + level + '.log')).read_text().strip() for level in ['O0', 'O2']},
}
(out / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
print('Evidence:', out / 'status.json')
PY
}

case "$stage" in
  all)
    out="$(prepare "$out")"
    compile_all "$out"
    start_images "$out" direct
    report "$out"
    ;;
  build)
    out="$(prepare "$out")"
    compile_all "$out"
    stage_binary resource-host-O0 "$out/native-resource-host-O0"
    stage_binary resource-host-O2 "$out/native-resource-host-O2"
    echo "built $out"
    ;;
  run)
    build_dir=${RESOURCE_BUILD_DIR:-}
    if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
      echo "run needs RESOURCE_BUILD_DIR to hold the static evidence the build step produced" >&2
      exit 2
    fi
    mkdir -p "$out"
    out="$(realpath "$out")"
    # The build step's evidence is read-only here; the run step keeps a copy next to its own logs, so the status report and the
    # images it describes stay in one directory that this step owns.
    cp -a "$build_dir"/. "$out"/
    rm -f "$out"/run-*.log
    start_images "$out" published
    report "$out"
    ;;
esac
