#!/bin/sh
# Replay the recorded Content bring-up: the port's refusal stays the default answer, and one named invocation gets the real graph.
#
# Everything about the tree comes from the environment, so the same script runs against any candidate view:
#   GTOS_VIEW     the candidate view root (the directory that holds BUILD.gn and .gn)
#   GTOS_GN       the gn binary to use (default: gn from PATH)
#   GTOS_OUT      where to put the generated directories (default: <view>/out/gtos-frontier-replay)
#   GTOS_PORT     optional; when set, the script refuses to touch a view that resolves into this shared tree
#   GTOS_EXPECT   text the Content generation's first error must contain, when it fails (default: rust)
# The script exits 0 only when the default generation still meets the port's own refusal; the Content result is reported either way.
set -eu
gn=${GTOS_GN:-gn}
view=${GTOS_VIEW:?set GTOS_VIEW to the candidate view root}
out=${GTOS_OUT:-$view/out/gtos-frontier-replay}
expect=${GTOS_EXPECT:-rust}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
platform='target_os="gtos" target_cpu="x86"'

resolved=$(realpath "$view")
if [ -n "${GTOS_PORT:-}" ]; then
    port=$(realpath "$GTOS_PORT")
    case "$resolved" in
        "$port"|"$port"/*) echo "REPLAY: refusing to write into the shared tree $port" >&2; exit 2 ;;
    esac
fi
for file in BUILD.gn .gn; do
    [ -f "$view/$file" ] || { echo "REPLAY: $view/$file is missing" >&2; exit 2; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp "$view/BUILD.gn" "$work/BUILD.gn"
cp "$view/.gn" "$work/dot-gn"
mkdir -p "$work/tmp"
TMPDIR="$work/tmp" python3 "$here/bringup-patch.py" "$work/BUILD.gn" "$work/dot-gn"

# The replacement is a new file, never a write through a name that might be linked into a source tree.
rm -f "$view/BUILD.gn" "$view/.gn"
cp "$work/BUILD.gn" "$view/BUILD.gn"
cp "$work/dot-gn" "$view/.gn"
chmod 0644 "$view/BUILD.gn" "$view/.gn"
if [ "$(realpath "$view/BUILD.gn")" != "$resolved/BUILD.gn" ] || [ "$(realpath "$view/.gn")" != "$resolved/.gn" ]; then
    echo 'REPLAY: the installed root files do not stay in the view' >&2
    exit 2
fi

mkdir -p "$out"
set +e
"$gn" gen --root="$view" "$out/refusal" --args="$platform" > "$out/refusal.log" 2>&1
refusal_status=$?
"$gn" gen --root="$view" "$out/content" --root-target=//content/shell:content_shell_app \
    --args="$platform gtos_frontier_bringup=true" > "$out/content.log" 2>&1
content_status=$?
set -e

refusal_note=$(sed -n 's/.*\(GTOS-FRONTIER.*\)/\1/p' "$out/refusal.log" | head -n 1)
content_note=$(sed -n 's/.*\(GTOS-FRONTIER.*\)/\1/p' "$out/content.log" | head -n 1)
if [ "$refusal_status" -eq 0 ] || ! grep -q 'GTOS browser runtime is not implemented' "$out/refusal.log"; then
    echo "REPLAY: the default generation no longer meets the port refusal (status $refusal_status)" >&2
    cat "$out/refusal.log" >&2
    exit 1
fi
printf 'REPLAY refusal preserved: status %s, %s\n' "$refusal_status" "$refusal_note"

first_error=$(sed -n 's/^\(ERROR at .*\)$/\1/p' "$out/content.log" | head -n 1)
if [ "$content_status" -eq 0 ]; then
    printf 'REPLAY content graph generated: %s\n' "$out/content"
else
    printf 'REPLAY content stopped: status %s, %s\n' "$content_status" "$first_error"
    case "$first_error" in
        *"$expect"*) ;;
        *) echo "REPLAY: the first error does not mention $expect; the port changed" >&2; exit 1 ;;
    esac
fi
printf 'REPLAY ok\n'
