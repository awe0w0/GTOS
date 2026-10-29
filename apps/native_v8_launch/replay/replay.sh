#!/bin/sh
# Replay the real V8 guest run: build the diagnosis kernel from the current sources, boot it with the pinned image as its only
# module, and read the lifecycle records the guest itself reports.
#
# The tree and the tools come from the environment; nothing here knows a host path:
#   GTOS_REPO          the GTOS source root to compile from (required)
#   GTOS_ELF           the pinned V8 image to drive (required)
#   GTOS_OUT           where a new, exclusive run directory is created (required)
#   GTOS_NM            llvm-nm used to read the image's state symbols (required)
#   GTOS_QEMU          qemu-system-i386 (required)
#   GTOS_LOADER        the dynamic loader that matches QEMU's libraries (required)
#   GTOS_QEMU_LIB      colon-separated library directories of that QEMU (required)
#   GTOS_QEMU_DATA     the QEMU data directory (firmware, keymaps) (required)
#   GTOS_QEMU_MODULE_DIR  the QEMU module directory (required)
#   GTOS_CONFIGS       space-separated <memory>:<cpus>[:<tag>] list (default: "32M:4 64M:1")
#   GTOS_EXPECT_ELF_SHA256 / GTOS_EXPECT_ELF_BYTES
#                      the pinned image's identity; when given, a different image stops the run before anything is built
#   GTOS_RUN_DIR       an explicit run directory (default: <GTOS_OUT>/run-<UTC timestamp>); it must not exist yet
#
# Every run gets its own directory and nothing is overwritten: the build record, the debug-console capture, the QEMU stderr, each
# guest's own stdout and the result JSON are separate files. This observer writes to the debug console only and the command line
# disables the serial port, so there is no serial capture for this diagnosis.
#
# The build also runs this tree's own instruction audit on the kernel before anything boots. A configuration counts as passed only
# when the shared judge accepts its log and the recorded exit: the guest's own pass line, every record with all of its fields, the
# reap naming both sides it compares, and QEMU exiting through the debug-exit port with 33.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
configs=${GTOS_CONFIGS:-32M:4 64M:1}

for name in GTOS_REPO GTOS_ELF GTOS_OUT GTOS_NM GTOS_QEMU GTOS_LOADER GTOS_QEMU_LIB GTOS_QEMU_DATA GTOS_QEMU_MODULE_DIR; do
    eval "value=\${$name:-}"
    [ -n "$value" ] || { echo "REPLAY: $name is not set" >&2; exit 2; }
done
for name in GTOS_REPO GTOS_ELF GTOS_QEMU GTOS_LOADER GTOS_QEMU_DATA; do
    eval "value=\${$name}"
    [ -e "$value" ] || { echo "REPLAY: $name points at a missing path: $value" >&2; exit 2; }
done
mkdir -p "$GTOS_OUT"
run=${GTOS_RUN_DIR:-"$GTOS_OUT/run-$(date -u +%Y%m%dT%H%M%SZ)"}
if [ -e "$run" ]; then
    echo "REPLAY: the run directory already exists: $run; a run never reuses evidence" >&2
    exit 2
fi
mkdir -p "$run"
build=$run/build
mkdir -p "$build"
printf 'REPLAY run directory: %s\n' "$run"

REPO=$GTOS_REPO OUT=$build ELF=$GTOS_ELF NM=$GTOS_NM TMPDIR=${TMPDIR:-/tmp} \
    EXPECT_ELF_SHA256=${GTOS_EXPECT_ELF_SHA256:-} EXPECT_ELF_BYTES=${GTOS_EXPECT_ELF_BYTES:-} \
    python3 "$here/kernel.py" > "$run/kernel-build.stdout.log" 2> "$run/kernel-build.stderr.log" || {
    status=$?
    echo "REPLAY: the kernel build failed (status $status); its output follows" >&2
    tail -n 40 "$run/kernel-build.stderr.log" >&2
    exit "$status"
}
printf 'REPLAY kernel built: %s\n' "$(sed -n 's/.*"sha256": "\([0-9a-f]*\)".*/\1/p' "$build/status.json" | tail -n 1)"

failed=0
for configuration in $configs; do
    memory=$(printf '%s' "$configuration" | cut -d: -f1)
    cpus=$(printf '%s' "$configuration" | cut -d: -f2)
    tag=$(printf '%s' "$configuration" | cut -d: -f3 -s)
    [ -n "$tag" ] || tag="$memory-smp$cpus"
    set +e
    OUT=$build RESULTS=$run ELF=$GTOS_ELF QEMU=$GTOS_QEMU LOADER=$GTOS_LOADER QEMU_LIB=$GTOS_QEMU_LIB QEMU_DATA=$GTOS_QEMU_DATA \
        QEMU_MODULE_DIR=$GTOS_QEMU_MODULE_DIR MEMORY=$memory SMP=$cpus TAG=$tag TIMEOUT=${GTOS_TIMEOUT:-600} \
        EXPECT_ELF_SHA256=${GTOS_EXPECT_ELF_SHA256:-} EXPECT_ELF_BYTES=${GTOS_EXPECT_ELF_BYTES:-} \
        python3 "$here/guest.py" > "$run/guest-$tag.stdout.log" 2> "$run/guest-$tag.stderr.log"
    status=$?
    set -e
    case "$status" in
        0) printf 'REPLAY guest passed: %s\n' "$tag" ;;
        *)
            printf 'REPLAY guest failed: %s (status %s)\n' "$tag" "$status" >&2
            tail -n 30 "$run/guest-$tag.stderr.log" >&2
            failed=1
            ;;
    esac
done
[ "$failed" -eq 0 ] || exit 1
printf 'REPLAY ok\n'
