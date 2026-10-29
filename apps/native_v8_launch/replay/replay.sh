#!/bin/sh
# Replay the real V8 guest run: build the diagnosis kernel from the current sources, boot it with the pinned image as its only
# module, and read the lifecycle records the guest itself reports.
#
# The tree and the tools come from the environment; nothing here knows a host path:
#   GTOS_REPO          the GTOS source root to compile from (required)
#   GTOS_ELF           the pinned V8 image to drive (required)
#   GTOS_OUT           the build/output directory (required)
#   GTOS_NM            llvm-nm used to read the image's state symbols (required)
#   GTOS_QEMU          qemu-system-i386 (required)
#   GTOS_LOADER        the dynamic loader that matches QEMU's libraries (required)
#   GTOS_QEMU_LIB      colon-separated library directories of that QEMU (required)
#   GTOS_QEMU_DATA     the QEMU data directory (firmware, keymaps) (required)
#   GTOS_QEMU_MODULE_DIR  the QEMU module directory (required)
#   GTOS_CONFIGS       space-separated <memory>:<cpus>[:<tag>] list (default: "32M:4 64M:1")
# The build also runs this tree's own instruction audit on the kernel before anything boots. A configuration counts as passed only
# when the guest reports its own pass line and the exact reap, and QEMU exits through the debug-exit port with 33; a timeout, a
# plain zero-exit or a log without the records is a failure.
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

REPO=$GTOS_REPO OUT=$GTOS_OUT ELF=$GTOS_ELF NM=$GTOS_NM TMPDIR=${TMPDIR:-/tmp} python3 "$here/kernel.py" > "$GTOS_OUT/kernel-build.log" 2>&1 || {
    echo 'REPLAY: the kernel build failed; its log follows' >&2
    tail -n 40 "$GTOS_OUT/kernel-build.log" >&2
    exit 1
}
kernel_sha=$(sed -n 's/.*"sha256": "\([0-9a-f]*\)".*/\1/p' "$GTOS_OUT/status.json" | tail -n 1)
printf 'REPLAY kernel built: %s\n' "$kernel_sha"

failed=0
for configuration in $configs; do
    memory=$(printf '%s' "$configuration" | cut -d: -f1)
    cpus=$(printf '%s' "$configuration" | cut -d: -f2)
    tag=$(printf '%s' "$configuration" | cut -d: -f3 -s)
    [ -n "$tag" ] || tag="$memory-smp$cpus"
    if OUT=$GTOS_OUT ELF=$GTOS_ELF QEMU=$GTOS_QEMU LOADER=$GTOS_LOADER QEMU_LIB=$GTOS_QEMU_LIB QEMU_DATA=$GTOS_QEMU_DATA \
        QEMU_MODULE_DIR=$GTOS_QEMU_MODULE_DIR MEMORY=$memory SMP=$cpus TAG=$tag TIMEOUT=${GTOS_TIMEOUT:-600} \
        python3 "$here/guest.py" > "$GTOS_OUT/guest-$tag.log" 2>&1; then
        printf 'REPLAY guest passed: %s\n' "$tag"
    else
        printf 'REPLAY guest failed: %s\n' "$tag" >&2
        tail -n 30 "$GTOS_OUT/guest-$tag.log" >&2
        failed=1
    fi
done
[ "$failed" -eq 0 ] || exit 1
printf 'REPLAY ok\n'
