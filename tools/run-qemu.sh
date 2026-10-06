#!/bin/sh
# Uses only the dedicated image named here. No host block device is attached.
set -eu
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RUNTIME=$(dirname "$REPO")/gtos-runtime
if [ -x "$RUNTIME/root/usr/bin/qemu-system-i386" ]; then
    export LD_LIBRARY_PATH="$RUNTIME/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QEMU_MODULE_DIR="$RUNTIME/root/usr/lib/x86_64-linux-gnu/qemu"
    export PATH="$RUNTIME/bin:$RUNTIME/root/usr/bin:$PATH"
    QEMU="$RUNTIME/root/usr/bin/qemu-system-i386"
    BIOS="-L $RUNTIME/root/usr/share/qemu"
else
    QEMU=${QEMU:-qemu-system-i386}
    BIOS=
fi
make -C "$REPO" GTOS.iso
mkdir -p "$REPO/data"
DISK=${GTOS_DISK:-$REPO/data/apps.img}
if [ ! -e "$DISK" ]; then
    python3 "$REPO/tools/disk.py" create "$DISK" --size-mib 8
fi
exec "$QEMU" $BIOS -machine pc -accel tcg -m "${GTOS_MEMORY:-64M}" \
    -smp "${GTOS_CPUS:-4}" -cdrom "$REPO/GTOS.iso" -boot d \
    -drive "file=$DISK,format=raw,if=ide,index=0" -nic none \
    -display "${QEMU_DISPLAY:-gtk}" -qmp stdio \
    -debugcon "file:$REPO/obj/debug.log" -global isa-debugcon.iobase=0xe9 "$@"
