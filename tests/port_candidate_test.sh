#!/bin/sh
# The port candidate's own check: every patch in apps/native_content_build/port-candidate-r18 reproduces the bytes the manifest
# records, when applied to the file it was generated from.
#
# This is a check on the committed unit, not on Chromium: it says the reviewable patches and the recorded identities still describe
# the same files, so a reviewer who applies a patch gets exactly the candidate that was measured. It skips, with a reason, when the
# pinned checkouts the patches were generated from are not present.
set -eu
cd "$(dirname "$0")/.."
python=${PYTHON:-python3}
exec "$python" -B tests/port_candidate_check.py
