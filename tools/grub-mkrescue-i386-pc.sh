#!/bin/sh
# GRUB media builder for environments where grub-mkrescue cannot find its own modules.
#
# This is an adapter, not a replacement: it execs the real grub-mkrescue and passes every argument through unchanged. The one and
# only difference from calling grub-mkrescue directly is the added --directory=<i386-pc module directory>. That matters because a
# relocated toolchain keeps its modules outside the compiled-in prefix: without the flag grub-mkrescue silently produces an image
# with no normal.mod or multiboot.mod, which builds successfully and then fails to boot. The caller must name the module
# directory; this script does not guess, search, or fall back, so a wrong setup fails instead of producing a module-less image.
#
# Usage: GRUB_MKRESCUE=tools/grub-mkrescue-i386-pc.sh GTOS_GRUB_MODULES=/path/to/grub/i386-pc make GTOS.iso
# The real tool can be selected with GTOS_GRUB_MKRESCUE (default: grub-mkrescue), and the caller is expected to record its hash.
set -eu

real=${GTOS_GRUB_MKRESCUE:-grub-mkrescue}
modules=${GTOS_GRUB_MODULES:-}

if [ -z "$modules" ]; then
    echo "grub-mkrescue-i386-pc: set GTOS_GRUB_MODULES to the grub i386-pc module directory" >&2
    exit 2
fi
if [ ! -f "$modules/normal.mod" ] || [ ! -f "$modules/multiboot.mod" ]; then
    echo "grub-mkrescue-i386-pc: $modules does not contain normal.mod and multiboot.mod" >&2
    exit 2
fi

exec "$real" --directory="$modules" "$@"
