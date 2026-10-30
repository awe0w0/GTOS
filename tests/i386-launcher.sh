#!/bin/sh
# Which executor starts an i386 image — a decision, in one place, so it can be tested without compiling anything.
#
# An i386 static ELF cannot start on an x86-64 host, so ordinarily something has to load it. Three situations must not be
# confused:
#
#   * an ordinary host that can start the image directly (a 32-bit host, or a native i386 kernel) — no executor needed;
#   * a host where the caller names the executor explicitly (a restricted stage, a CI with a pinned emulator);
#   * a restricted stage that needs an executor and was given none, which is a refusal, not a silent fallback.
#
# The choice never depends on PATH for a restricted stage: an emulator that happens to be installed must not change the
# execution architecture of a reported run. The fallback to a native start exists only so that the ordinary, no-argument
# entry keeps working on a public checkout that has no neighbouring private runtime.

# i386_launcher_choose <mode: all|run>  ->  prints "launcher <path>", or "native" for a direct start
# Returns 2 when a restricted run needs an executor and none is available, or when the named executor is not executable.
i386_launcher_choose() {
    mode=${1:-all}
    if [ -n "${ELF32_I386_LAUNCHER:-}" ]; then
        if [ ! -x "${ELF32_I386_LAUNCHER}" ]; then
            echo "ELF32_I386_LAUNCHER is not an executable file: ${ELF32_I386_LAUNCHER}" >&2
            return 2
        fi
        printf 'launcher %s\n' "${ELF32_I386_LAUNCHER}"
        return 0
    fi
    if [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
        printf 'launcher %s\n' ../gtos-runtime/root/usr/bin/qemu-i386
        return 0
    fi
    if [ "$mode" = run ]; then
        echo "this run stage needs an executor for i386 images: set ELF32_I386_LAUNCHER to a real qemu-i386" >&2
        return 2
    fi
    printf 'native\n'
    return 0
}

# i386_launcher_is_native <choice line>  ->  true when the choice says a direct start
i386_launcher_is_native() {
    [ "$1" = native ]
}

# i386_launcher_path <choice line>  ->  the executor path of a "launcher <path>" choice
i386_launcher_path() {
    printf '%s\n' "${1#launcher }"
}
