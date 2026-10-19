#!/bin/bash
# Build a GTOS ISO into a fresh artifact directory without touching the
# repository obj/iso tree or the repository images.
#
#   native_window_iso_build.sh [--check-only] FRESH_OUT KERNEL_BINARY [SLOT=PATH ...]
#
# The kernel's boot contract is exactly four modules in a fixed order. This
# entry point therefore only ever REPLACES one of those four slots; it can never
# append a fifth. Every parameter is validated before the first copy or write,
# and --check-only stops after the plan is printed so a rejection can be proven
# without touching the filesystem or running grub.
set -euo pipefail

usage() {
  echo "usage: native_window_iso_build.sh [--check-only] FRESH_OUT KERNEL_BINARY [SLOT=PATH ...]" >&2
  echo "slots: catch.gtapp native-fault.elf native-peer.elf browser-probe.elf" >&2
  exit 2
}

check_only=0
if [ "${1:-}" = "--check-only" ]; then
  check_only=1
  shift
fi
if [ "$#" -lt 2 ]; then usage; fi

repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
kernel="$2"
shift 2
specs=("$@")

# The complete, ordered boot contract. Nothing outside this list may be
# installed, and nothing may be added to it.
slots="catch.gtapp native-fault.elf native-peer.elf browser-probe.elf"

# ---- validation stage: no filesystem mutation happens above this line --------
if [ -z "$out" ]; then
  echo "Refusing an empty output directory" >&2
  exit 2
fi
if [ -e "$out" ]; then
  # A fresh directory is required: an existing one may already hold evidence.
  echo "Refusing to reuse an existing output directory: $out" >&2
  exit 2
fi
case "$out" in
  */../*|*/..|../*|..) echo "Refusing a path containing ..: $out" >&2; exit 2 ;;
esac
if [ ! -f "$kernel" ]; then
  echo "Kernel binary not found: $kernel" >&2
  exit 2
fi

declare -A installed=()
order=()
for spec in "${specs[@]+"${specs[@]}"}"; do
  case "$spec" in
    *=*) base="${spec%%=*}"; src="${spec#*=}" ;;
    *)   echo "Module must be given as SLOT=PATH, got: $spec" >&2; exit 2 ;;
  esac
  # The slot name must be one of the four known basenames, literally.
  case "$base" in
    */*|*\\*|*..*|"")
      echo "Refusing an unsafe slot name: $base" >&2; exit 2 ;;
  esac
  case " $slots " in
    *" $base "*) ;;
    *) echo "Refusing an unknown slot name: $base (not one of: $slots)" >&2; exit 2 ;;
  esac
  if [ -n "${installed[$base]:-}" ]; then
    echo "Refusing a duplicate slot: $base" >&2; exit 2
  fi
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "Module source not found for $base: $src" >&2
    exit 2
  fi
  installed[$base]="$src"
  order+=("$base")
done

modules="/boot/catch.gtapp /boot/native-fault.elf /boot/native-peer.elf /boot/browser-probe.elf"
echo "plan: kernel=$kernel"
for base in "${order[@]+"${order[@]}"}"; do
  printf 'plan: replace %s with %s\n' "$base" "${installed[$base]}"
done
echo "plan: modules=$modules"
if [ "$check_only" -eq 1 ]; then
  echo "check-only: parameters accepted, nothing written"
  exit 0
fi

# ---- materialisation stage --------------------------------------------------
mkdir -p "$out/iso/boot/grub" "$out/iso/boot/licenses"
cp "$kernel" "$out/iso/boot/GTOS.bin"
cp "$repo/apps/catch.gtapp" "$out/iso/boot/catch.gtapp"
cp "$repo/obj/native/fault.elf" "$out/iso/boot/native-fault.elf"
cp "$repo/obj/native/peer.elf" "$out/iso/boot/native-peer.elf"
cp "$repo/obj/browser-probe/browser-probe.elf" "$out/iso/boot/browser-probe.elf"
for base in "${order[@]+"${order[@]}"}"; do
  cp "${installed[$base]}" "$out/iso/boot/$base"
  printf 'slot %s %s\n' "$base" "$(sha256sum "${installed[$base]}" | cut -d' ' -f1)"
done > "$out/replacements.txt"
cp "$repo/docs/desktop-font-license.txt" "$out/iso/boot/licenses/"
cp "$repo/apps/fonts/OFL.txt" "$out/iso/boot/licenses/Noto-OFL.txt"
cp "$repo/apps/fonts/README.md" "$out/iso/boot/licenses/noto-font-provenance.md"
cp "$repo/apps/fonts/han_manifest.json" "$out/iso/boot/licenses/"
{
  printf 'set timeout=0\nset default=0\nmenuentry "GTOS Desktop" {\n multiboot /boot/GTOS.bin\n'
  for m in $modules; do printf ' module %s\n' "$m"; done
  printf ' boot\n}\n'
} > "$out/iso/boot/grub/grub.cfg"
printf '%s\n' "$modules" > "$out/modules.txt"
sha256sum "$out/iso/boot/GTOS.bin" "$out/iso/boot"/*.gtapp "$out/iso/boot"/*.elf \
  > "$out/boot-hashes.txt"
for spec in "${specs[@]+"${specs[@]}"}"; do
  printf 'request %s\n' "$spec"
done > "$out/requests.txt"
/mnt/f/GTOS-Chromium/toolchains/gtos-runtime/bin/grub-mkrescue \
  --output="$out/GTOS.iso" "$out/iso" > "$out/grub.log" 2>&1
sha256sum "$out/GTOS.iso" > "$out/iso-sha256.txt"
cat "$out/iso-sha256.txt"
