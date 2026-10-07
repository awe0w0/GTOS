#!/bin/bash
# Standalone checks for the new resource service only; no frozen workloads.
set -euo pipefail
if [ "$#" -ne 2 ]; then
  echo "usage: native_resource_host_test.sh INDEPENDENT.png FRESH_ARTIFACT_DIR" >&2
  exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
expected="$(realpath "$1")"
out="$2"
test -f "$expected"
if [ -e "$out" ]; then
  echo "Refusing to reuse artifact directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
out="$(realpath "$out")"
compiler="${CXX:-g++}"
"$compiler" --version > "$out/compiler.txt"
printf 'CXX=%q bash %q %q %q\n' "$compiler" "$repo/tests/native_resource_host_test.sh" "$expected" "${out}-reproduced" > "$out/reproduce.sh"
sha256sum "$repo/include/process/resource_abi.h" "$repo/include/process/resources.h" \
  "$repo/src/process/resources.cpp" "$repo/src/process/resources_png.inc" \
  "$repo/src/process/native_runtime.cpp" "$repo/tests/native_resource_host.cpp" \
  "$repo/tests/native_resource_host_test.sh" "$expected" > "$out/source-hashes.txt"
for level in 0 2; do
  "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    -I"$repo/include" "$repo/src/process/resources.cpp" "$repo/tests/native_resource_host.cpp" \
    -o "$out/native-resource-host-O$level" > "$out/build-O$level.log" 2>&1
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$out/native-resource-host-O$level" "$expected" > "$out/run-O$level.log" 2>&1
  cat "$out/run-O$level.log"
done
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
