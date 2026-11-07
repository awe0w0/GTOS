#!/bin/sh
# The suite's output contract: a run writes into a new generation of its own and leaves the previous one exactly as it was.
#
# This unit exists because the suite used to do the opposite. It reused fixed directories and, when a unit refused to reuse its
# artifact directory, it removed that directory first. Both habits destroy the only record of what the last run published, and the
# second one turns a refusal into a pass by deleting the thing being refused. The check is on real files: a sentinel generation is
# created, a second one is created through the same helper the suite uses, a unit is run into the second one, and the first one's
# bytes are compared before and after.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh
failures=0
check() { # <description> <status>
    if [ "$2" -eq 0 ]; then
        echo "ok $1"
    else
        echo "FAIL $1" >&2
        failures=$((failures + 1))
    fi
}

root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-suite-generation.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM

# The previous run's generation, with a manifest, a record and a published image in it.
first=$root/generation-previous
mkdir -p "$first/publish/image-1"
printf 'old-manifest line\n' > "$first/manifest.txt"
printf '%s\n' "$first/publish/image-1/one 5 0000000000000000000000000000000000000000000000000000000000000000" > "$first/record.txt"
printf 'sentinel bytes\n' > "$first/publish/image-1/one"
chmod 0444 "$first/publish/image-1/one"
before=$(cd "$first" && find . -type f -exec sha256sum {} + | sort)
before_tree=$(cd "$first" && find . | sort)

second=$(suite_generation "$root") || exit 1
check 'a generation is created under the root' "$([ -d "$second" ] && echo 0 || echo 1)"
check 'the new generation is not the previous one' "$([ "$second" != "$first" ] && echo 0 || echo 1)"

# A unit runs into the new generation, and it must confine everything it writes there.
mkdir -p "$second/host-supply/publish"
GTOS_HOST_PUBLISH_ROOT=$second/host-supply/publish \
GTOS_PUBLISH_MANIFEST=$second/host-supply/manifest.txt \
GTOS_PUBLISH_RECORD=$second/host-supply/record.txt \
    ./tests/host_runner_test.sh prepare > "$second/prepare.log" 2>&1
check 'the unit ran into the new generation' "$([ -s "$second/host-supply/manifest.txt" ] && echo 0 || echo 1)"

after=$(cd "$first" && find . -type f -exec sha256sum {} + | sort)
after_tree=$(cd "$first" && find . | sort)
check 'the previous generation bytes are unchanged' "$([ "$before" = "$after" ] && echo 0 || echo 1)"
check 'the previous generation has no new or missing paths' "$([ "$before_tree" = "$after_tree" ] && echo 0 || echo 1)"

# Nothing the new run wrote may be inside the previous generation, and the published path it recorded has to be the new one.
escaped=$(awk -v first="$first" '$1 ~ ("^" first "/") { print $1 }' "$second/host-supply/record.txt" | head -n 1)
check 'the new run published nothing into the previous generation' "$([ -z "$escaped" ] && echo 0 || echo 1)"

# Two runs of the helper never hand out the same directory.
third=$(suite_generation "$root") || exit 1
check 'every generation has its own directory' "$([ "$third" != "$second" ] && [ "$third" != "$first" ] && echo 0 || echo 1)"

# A root that cannot be created is refused rather than silently writing somewhere else.
if suite_generation /dev/null/nope > /dev/null 2>&1; then
    echo 'FAIL a generation under an impossible root is refused' >&2
    failures=$((failures + 1))
else
    echo 'ok a generation under an impossible root is refused'
fi

if [ "$failures" -ne 0 ]; then
    echo "suite generation: $failures failure(s)" >&2
    exit 1
fi
echo 'PASS suite generation: a run writes into a new generation and the previous one keeps its exact bytes and paths'
