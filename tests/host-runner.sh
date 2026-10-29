#!/bin/sh
# Host program supply for the CI units: publish an image, then start it exactly once.
#
# Two functions, because the two things are different and the CI needs them at different times.
#
#   publish_host_binary <binary>
#       Copies the image into $GTOS_HOST_PUBLISH_ROOT/<exclusive directory>/, compares the copy with its source, makes the copy
#       read-only and prints the published path. A symlink, a directory, a missing file, a missing or unwritable root, a failed
#       copy, a copy whose bytes differ from the source, or a published file that is still writable is refused before anything
#       can start. Nothing here executes the image.
#
#   run_host_binary <binary> [args...]
#       Starts one regular file with the arguments it was given and returns that program's own status. A directory, a symlink,
#       a missing file or a non-regular file is refused with status 2. The arguments are forwarded unchanged: a caller that
#       selects a case must not have that selection swallowed here.
#
# Starting an image is a property of the image, not of PATH. An i386 static image cannot start on an x86-64 host, and a 64-bit
# loader cannot load it, so the caller names the fixed executor for the image it built in GTOS_HOST_LAUNCHER (for example
# qemu-i386). The runner never looks one up in PATH: a CI that silently changes its execution architecture when some emulator
# happens to be installed is not reporting the same thing twice. When a launcher is named, the image it starts must be the
# published, read-only copy: a launcher pointed at a writable file is refused, because the point of publishing is that what runs
# cannot change underneath it.

# publish_host_binary <binary>
publish_host_binary() {
    if [ "$#" -ne 1 ]; then
        echo "publish_host_binary takes exactly one binary" >&2
        return 2
    fi
    source_binary=$1
    if [ -L "$source_binary" ]; then
        echo "publish_host_binary: refusing a symlink: $source_binary" >&2
        return 2
    fi
    if [ ! -f "$source_binary" ]; then
        echo "publish_host_binary: not a file: $source_binary" >&2
        return 2
    fi
    publish_root=${GTOS_HOST_PUBLISH_ROOT:-}
    if [ -z "$publish_root" ]; then
        echo "publish_host_binary: GTOS_HOST_PUBLISH_ROOT is not set" >&2
        return 2
    fi
    if [ ! -d "$publish_root" ]; then
        echo "publish_host_binary: publish root is not a directory: $publish_root" >&2
        return 2
    fi
    if [ ! -w "$publish_root" ]; then
        echo "publish_host_binary: publish root is not writable: $publish_root" >&2
        return 2
    fi
    publish_name=$(basename "$source_binary")
    publish_directory=$(mktemp -d "$publish_root/$(date +%Y%m%dT%H%M%S)-XXXXXX") || {
        echo "publish_host_binary: cannot create a directory under $publish_root" >&2
        return 2
    }
    published=$publish_directory/$publish_name
    if ! cp "$source_binary" "$published"; then
        echo "publish_host_binary: cannot copy into $published" >&2
        return 2
    fi
    # The copy is the artifact, so the copy is what gets checked: a source that changed during the copy would otherwise be
    # published under the identity of the file it used to be.
    source_digest=$(sha256sum "$source_binary" | cut -d' ' -f1)
    published_digest=$(sha256sum "$published" | cut -d' ' -f1)
    if [ "$source_digest" != "$published_digest" ]; then
        echo "publish_host_binary: the copy does not match its source: $source_binary" >&2
        return 2
    fi
    if ! chmod 0555 "$published"; then
        echo "publish_host_binary: cannot make $published read-only" >&2
        return 2
    fi
    # Root can write a file whose mode forbids it, so the mode is what is checked here and the run stage is what makes the
    # check effective: the view root is not in its writable set, which the kernel enforces for every user.
    published_mode=$(stat -c %a "$published" 2>/dev/null || echo '')
    if [ -z "$published_mode" ] || [ $((0$published_mode & 0222)) -ne 0 ]; then
        echo "publish_host_binary: the published file is not read-only: $published ($published_mode)" >&2
        return 2
    fi
    printf '%s\n' "$published"
    return 0
}

# run_host_binary <binary> [args...]
run_host_binary() {
    if [ "$#" -lt 1 ]; then
        echo "run_host_binary takes a binary and its arguments" >&2
        return 2
    fi
    binary=$1
    shift
    if [ -L "$binary" ]; then
        echo "run_host_binary: refusing a symlink: $binary" >&2
        return 2
    fi
    if [ ! -f "$binary" ]; then
        echo "run_host_binary: not a regular file: $binary" >&2
        return 2
    fi
    if [ -n "${GTOS_HOST_LAUNCHER:-}" ]; then
        if [ -z "${GTOS_HOST_PUBLISH_ROOT:-}" ]; then
            echo "run_host_binary: a launcher is named but GTOS_HOST_PUBLISH_ROOT is not, so the image it would load is writable" >&2
            return 2
        fi
        image_mode=$(stat -c %a "$binary" 2>/dev/null || echo '')
        if [ -z "$image_mode" ] || [ $((0$image_mode & 0222)) -ne 0 ]; then
            echo "run_host_binary: a launcher may only load a read-only published image: $binary" >&2
            return 2
        fi
        # The launcher carries its own arguments, so this expansion is deliberately unquoted: splitting it is the point.
        # shellcheck disable=SC2086
        $GTOS_HOST_LAUNCHER "$binary" "$@"
        return $?
    fi
    "$binary" "$@"
}

# ---------------------------------------------------------------- the two-step supply
# A step that builds a program and then starts it cannot do both under one execution allow-list: the program did not exist when
# the list was made. The scripts that do both therefore accept a stage argument, and these two functions carry the artifact
# between the two runs.
#
#   stage_binary <key> <binary>   publish <binary> and append "<key>=<published path>" to $GTOS_PUBLISH_MANIFEST
#   staged_binary <key>           print the path recorded for <key>, failing if the manifest does not name it
#
# The manifest is the record of which artifact runs, written where the first run can write and the second can only read.
stage_binary() {
    if [ "$#" -ne 2 ]; then
        echo "stage_binary takes a key and a binary" >&2
        return 2
    fi
    manifest=${GTOS_PUBLISH_MANIFEST:-}
    if [ -z "$manifest" ]; then
        echo "stage_binary: GTOS_PUBLISH_MANIFEST is not set" >&2
        return 2
    fi
    published=$(publish_host_binary "$2") || return 2
    printf '%s=%s\n' "$1" "$published" >> "$manifest" || return 2
    printf 'STAGED %s %s\n' "$1" "$published"
    return 0
}

staged_binary() {
    if [ "$#" -ne 1 ]; then
        echo "staged_binary takes a key" >&2
        return 2
    fi
    manifest=${GTOS_PUBLISH_MANIFEST:-}
    if [ -z "$manifest" ]; then
        echo "staged_binary: GTOS_PUBLISH_MANIFEST is not set" >&2
        return 2
    fi
    if [ ! -f "$manifest" ]; then
        echo "staged_binary: no manifest at $manifest" >&2
        return 2
    fi
    entry=$(grep "^$1=" "$manifest" | tail -n 1)
    if [ -z "$entry" ]; then
        echo "staged_binary: $1 is not recorded in $manifest" >&2
        return 2
    fi
    printf '%s\n' "${entry#*=}"
}
