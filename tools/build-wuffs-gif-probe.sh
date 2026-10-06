#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "${PYTHON:-python3}" "$repo/tools/build-wuffs-gif-probe.py" "$@"
