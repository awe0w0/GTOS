#!/usr/bin/env bash
# Exact production int80 caller/boundary/alias/fault/reap evidence.
set -euo pipefail
exec "${GTOS_TEST_PYTHON:-python3}" "$(dirname "${BASH_SOURCE[0]}")/../tools/verify-native-process-info.py" "$@"
