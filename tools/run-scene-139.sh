#!/bin/bash
# GTOS-DSH-139 H/G: run one scene in the emulator and keep both channels.
#
# Usage: run-scene.sh <scene-name> <append-line> <vcpus> <image> <outdir> <label>
#
# The append line is passed in whole, derived from the scene declaration by the caller. It is NOT composed here from a
# stop value: composing it here is how an earlier run lost the scene's flags and started a different scenario than the one
# it reported. A scene is its flags and its stop together, and the guest reads both.
#
# The emulator and its libraries come from the materialised runtime tree. The tree is addressed by an explicit loader and
# an explicit four-directory library path because the host has no system qemu-system-i386 and the bundled binary is built
# against a different loader path than this distribution uses. QEMU_MODULE_DIR has to name the module directory or the
# accelerators cannot be found, and the failure then looks like a missing machine rather than a missing module.
set -u

SCENE="$1"; APPEND="$2"; VCPUS="$3"; IMAGE="$4"; OUT="$5"; LABEL="$6"
RT="${RT_ROOT:?RT_ROOT must name the runtime tree}"

mkdir -p "$OUT"
E9="$OUT/$LABEL.e9.log"
SERIAL="$OUT/$LABEL.serial.log"
: > "$E9"
: > "$SERIAL"

LOADER="$RT/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
EMU="$RT/usr/bin/qemu-system-i386"
LIB="$RT/usr/lib/x86_64-linux-gnu:$RT/lib/x86_64-linux-gnu:$RT/usr/lib/x86_64-linux-gnu/qemu:$RT/lib/x86_64-linux-gnu/qemu"
FIRMWARE="${FIRMWARE_DIR:-$RT/../firmware}"
export QEMU_MODULE_DIR="$RT/usr/lib/x86_64-linux-gnu/qemu"

timeout 90 "$LOADER" --library-path "$LIB" "$EMU" \
  -machine pc -accel tcg,thread=single -cpu qemu32 \
  -smp "$VCPUS" -m 128M -nodefaults -no-user-config \
  -display none -monitor none -nic none -vga none \
  -serial "file:$SERIAL" \
  -chardev "file,id=e9,path=$E9" \
  -device isa-debugcon,iobase=0xe9,chardev=e9 \
  -no-reboot -no-shutdown \
  -L "$FIRMWARE" \
  -kernel "$IMAGE" \
  -append "$APPEND" >"$OUT/$LABEL.transport.log" 2>&1
RC=$?

echo "scene=$SCENE append=[$APPEND] vcpus=$VCPUS rc=$RC e9=$(wc -c < "$E9") serial=$(wc -c < "$SERIAL")"
