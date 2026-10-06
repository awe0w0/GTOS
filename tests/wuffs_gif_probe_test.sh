#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test "$#" -ge 1 || { printf 'Usage: %s NEW_OUTPUT [builder options]\n' "$0" >&2; exit 2; }
"$repo/tools/build-wuffs-gif-probe.sh" "$@"
printf 'Real host ASan/UBSan and native scalar object/ELF checks passed. Guest acceptance is separate.\n'
