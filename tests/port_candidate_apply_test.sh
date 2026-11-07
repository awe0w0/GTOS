#!/bin/sh
# Compose the port candidate from the committed patches, into a directory of this test's own, and check what composition produced.
#
# This is the composer that can write into the wrong tree, so the test checks the two things that matter about it: the four edited
# files come out with exactly the digests the manifest records, and a destination inside a shared source root is refused before
# anything is written. It skips, with a reason, when the pinned checkouts are absent.
set -eu
cd "$(dirname "$0")/.."
python=${PYTHON:-python3}
candidate=apps/native_content_build/port-candidate-r18
if [ ! -f "$candidate/candidate.json" ] || [ ! -d "$candidate/patches" ]; then
    echo 'skip port candidate apply: the candidate is not in this checkout'
    exit 0
fi
if [ ! -d /mnt/f/GTOS-Chromium/sources/chromium-full/src ]; then
    echo 'skip port candidate apply: the pinned checkouts are not present'
    exit 0
fi
root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-port-candidate.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM
failures=0

# 1. A destination inside the shared tree is refused, and nothing appears there.
set +e
out=$("$python" -B "$candidate/apply.py" --view /mnt/f/GTOS-Chromium/sources/gtos-candidate-should-not-exist 2>&1)
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo 'FAIL composing into a shared source root was allowed' >&2
    failures=$((failures + 1))
elif printf '%s' "$out" | grep -q 'refused'; then
    echo "ok a destination under sources/ is refused (status $status)"
else
    echo "FAIL the refusal did not say why: $out" >&2
    failures=$((failures + 1))
fi
if [ -e /mnt/f/GTOS-Chromium/sources/gtos-candidate-should-not-exist ]; then
    echo 'FAIL the refused destination was created anyway' >&2
    failures=$((failures + 1))
fi

# 2. A real composition reproduces the recorded bytes.
"$python" -B "$candidate/apply.py" --view "$root/view" > "$root/apply.log" 2>&1 || {
    sed -n '1,40p' "$root/apply.log" >&2
    echo 'FAIL composition failed' >&2
    exit 1
}
if "$python" -B - "$root" "$candidate/candidate.json" <<'PY'
import hashlib
import json
import sys

root, manifest_path = sys.argv[1], sys.argv[2]
manifest = json.load(open(manifest_path))
edited = [entry for entry in manifest['files'] if entry['edited']]
problems = 0
for entry in edited:
    path = root + '/view/' + entry['path']
    recorded = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    if recorded != entry['overlay_sha256']:
        print('FAIL %s composed to %s, not the recorded %s' % (entry['path'], recorded, entry['overlay_sha256']), file=sys.stderr)
        problems += 1
    else:
        print('ok %s composed to the recorded bytes' % entry['path'])
summary = json.load(open(root + '/applied-view.json'))
if summary.get('shared_writes'):
    print('FAIL composition reported shared writes: %s' % summary['shared_writes'], file=sys.stderr)
    problems += 1
else:
    print('ok composition wrote nothing outside the view')
sys.exit(1 if problems else 0)
PY
then :; else failures=$((failures + 1)); fi

if [ "$failures" -ne 0 ]; then
    echo "port candidate apply: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS port candidate apply: composition refuses shared roots and reproduces the recorded bytes'
