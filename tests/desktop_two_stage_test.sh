#!/bin/sh
# Exercise the real desktop build and run entry points across the publication boundary.
set -eu
cd "$(dirname "$0")/.."
. ./tests/host-runner.sh

usage() {
    echo "usage: $0 [build <fresh-dir> | run <fresh-dir>]" >&2
    exit 2
}

build_cases() {
    mkdir "$1"
    build_root=$(realpath "$1")
    for name in normal status-2 status-7 status-9; do
        case_root=$build_root/$name
        mkdir -p "$case_root/publish"
        code=${name#status-}
        if [ "$name" = normal ]; then code=; fi
        GTOS_DESKTOP_EXIT_CODE=$code GTOS_DESKTOP_SANITIZERS=0 \
            GTOS_HOST_PUBLISH_ROOT=$case_root/publish GTOS_PUBLISH_MANIFEST=$case_root/manifest.txt \
            GTOS_PUBLISH_RECORD=$case_root/record.txt sh tests/desktop_test.sh build > "$case_root/build.log"
        if grep -q 'starting the' "$case_root/build.log"; then
            echo "desktop_two_stage_test: $name started an image during build" >&2
            return 1
        fi
        [ "$(grep -c '^desktop\(32\|64\)=' "$case_root/manifest.txt")" -eq 2 ]
        for key in desktop32 desktop64; do
            image=$(GTOS_PUBLISH_MANIFEST=$case_root/manifest.txt staged_binary "$key")
            case $image in "$case_root/publish"/*) ;; *) return 1 ;; esac
            GTOS_PUBLISH_RECORD=$case_root/record.txt verify_publish_record "$image"
        done
    done

    # Change a separate copy so the record and executor controls still see the original images.
    normal=$build_root/normal
    original=$(GTOS_PUBLISH_MANIFEST=$normal/manifest.txt staged_binary desktop32)
    changed=$normal/publish/changed.elf
    cp "$original" "$changed"
    chmod u+w "$changed"
    printf '\001' >> "$changed"
    chmod 0555 "$changed"
    while IFS= read -r line; do
        case $line in desktop32=*) line=desktop32=$changed ;; esac
        printf '%s\n' "$line"
    done < "$normal/manifest.txt" > "$normal/changed-manifest.txt"
    : > "$normal/empty-record.txt"

    mkdir "$build_root/executor" "$build_root/no-publication"
    launcher=$build_root/executor/qemu-i386
    cat > "$launcher" <<'SH'
#!/bin/sh
printf '%s\n' "$1" >> "$GTOS_DESKTOP_EXECUTOR_LOG"
if [ -n "${GTOS_DESKTOP_TEST_I386:-}" ]; then exec "$GTOS_DESKTOP_TEST_I386" "$@"; fi
exec "$@"
SH
    chmod 0555 "$launcher"
    echo 'desktop_two_stage_test: built four pairs, started none'
}

run_case() {
    label=$1 case_root=$2 expected_status=$3 expected_starts=$4 expected_text=$5
    shift 5
    log=$run_root/$label.log
    status=0
    env GTOS_HOST_PUBLISH_ROOT="$case_root/publish" GTOS_PUBLISH_MANIFEST="$case_root/manifest.txt" \
        GTOS_PUBLISH_RECORD="$case_root/record.txt" GTOS_HOST_LAUNCHER= GTOS_DESKTOP_SANITIZERS=0 \
        GTOS_DESKTOP_I386_EXECUTOR="$executor" GTOS_DESKTOP_STRICT=0 \
        "$@" sh tests/desktop_test.sh run > "$log" 2>&1 || status=$?
    starts=$(grep -c 'starting the .*-bit image' "$log" || :)
    if [ "$status" -ne "$expected_status" ] || [ "$starts" -ne "$expected_starts" ] ||
        ! grep -Fq -- "$expected_text" "$log"; then
        cat "$log" >&2
        echo "FAIL $label: rc=$status starts=$starts, expected rc=$expected_status starts=$expected_starts and '$expected_text'" >&2
        return 1
    fi
    echo "ok $label: rc=$status starts=$starts"
}

run_cases() {
    [ -n "$1" ] && [ -d "$1/normal" ] || usage
    build_root=$(realpath "$1")
    run_root=$(realpath -m "$2")
    case $run_root in "$build_root"|"$build_root"/*) usage ;; esac
    mkdir "$run_root"
    normal=$build_root/normal
    executor=${GTOS_DESKTOP_TEST_I386:-${GTOS_DESKTOP_I386_EXECUTOR:-}}
    if [ -z "$executor" ]; then executor=$(command -v qemu-i386 || :); fi
    launcher=$build_root/executor/qemu-i386
    executor_log=$run_root/executor.log
    : > "$executor_log"

    run_case missing-manifest "$build_root/no-publication" 70 0 'no manifest'
    run_case missing-record "$normal" 70 0 'no publish record' GTOS_PUBLISH_RECORD="$normal/absent-record.txt"
    run_case empty-record "$normal" 70 0 'is not recorded as published' GTOS_PUBLISH_RECORD="$normal/empty-record.txt"
    run_case changed-image "$normal" 70 0 'is not the image that was published' GTOS_PUBLISH_MANIFEST="$normal/changed-manifest.txt"
    run_case strict-no-executor "$normal" 70 0 'a strict run needs' GTOS_DESKTOP_STRICT=1 GTOS_DESKTOP_I386_EXECUTOR=
    run_case strict-path-only "$normal" 70 0 'a strict run needs' GTOS_DESKTOP_STRICT=1 GTOS_DESKTOP_I386_EXECUTOR= \
        PATH="$build_root/executor:$PATH" GTOS_DESKTOP_EXECUTOR_LOG="$executor_log"
    run_case strict-directory "$normal" 70 0 'not an executable regular file' \
        GTOS_DESKTOP_STRICT=1 GTOS_DESKTOP_I386_EXECUTOR="$build_root/executor"
    [ ! -s "$executor_log" ]

    run_case normal "$normal" 0 2 'Desktop/framebuffer safety and interaction tests passed'
    # A host that cannot execute i386 natively can still run the ordinary 64-bit fallback. With a named emulator the guard test
    # requires both programs to finish and verifies the image path passed through the instrumented launcher.
    if grep -q 'the 32-bit width did not run' "$run_root/normal.log"; then
        [ -z "$executor" ]
        echo 'desktop_two_stage_test: native i386 unavailable on this host'
        status_starts=2
    else
        run_case named-executor "$normal" 0 2 'Desktop/framebuffer safety and interaction tests passed' \
            GTOS_DESKTOP_STRICT=1 GTOS_DESKTOP_I386_EXECUTOR="$launcher" GTOS_DESKTOP_TEST_I386="$executor" \
            GTOS_DESKTOP_EXECUTOR_LOG="$executor_log"
        image=$(GTOS_PUBLISH_MANIFEST=$normal/manifest.txt staged_binary desktop32)
        [ "$(cat "$executor_log")" = "$image" ]
        status_starts=1
    fi
    for code in 2 7 9; do
        run_case "status-$code" "$build_root/status-$code" "$code" "$status_starts" 'starting the'
    done
    echo 'desktop_two_stage_test: all checks pass'
}

case "$#:${1:-all}" in
    0:all)
        root=$(mktemp -d "${TMPDIR:-/tmp}/gtos-desktop-two-stage.XXXXXX")
        trap 'rm -rf "$root"' EXIT HUP INT TERM
        build_cases "$root/build"
        run_cases "$root/build" "$root/run"
        ;;
    2:build) build_cases "$2" ;;
    2:run) run_cases "${DESKTOP_TEST_BUILD_DIR:-}" "$2" ;;
    *) usage ;;
esac
