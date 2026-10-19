#!/bin/bash
# Prove the ISO entry point rejects illegal parameter plans BEFORE it writes
# anything, and that it never appends a fifth boot module.
#
#   native_window_iso_precheck.sh FRESH_OUT KERNEL_BINARY PROBE_ELF
#
# No grub is run and no ISO is produced here; the real image is built later by
# the one required guest build.
set -uo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: native_window_iso_precheck.sh FRESH_OUT KERNEL_BINARY PROBE_ELF" >&2
  exit 2
fi
repo=/mnt/f/GTOS-Chromium/sources/GTOS
out="$1"
kernel="$2"
probe="$3"
entry="$repo/tests/native_window_iso_build.sh"
if [ -e "$out" ]; then
  echo "Refusing to reuse an existing directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
fail=0
cases=0

# expect_refusal LABEL EXPECTED_EXIT ARGS...
expect_refusal() {
  label="$1"; want="$2"; shift 2
  cases=$((cases + 1))
  /usr/bin/bash "$entry" "$@" > "$out/case-$cases.log" 2>&1
  got=$?
  if [ "$got" -ne "$want" ]; then
    echo "FAIL $label: exit $got, expected $want" >&2
    sed -n '1,3p' "$out/case-$cases.log" >&2
    fail=1
    return
  fi
  printf 'rejected %-28s exit %d  %s\n' "$label" "$got" \
    "$(tail -n 1 "$out/case-$cases.log")"
}

plan="$out/plan"
# 1. A legal replacement plan is accepted and writes nothing.
cases=$((cases + 1))
/usr/bin/bash "$entry" --check-only "$plan" "$kernel" "browser-probe.elf=$probe" \
  > "$out/case-$cases.log" 2>&1
got=$?
if [ "$got" -ne 0 ]; then
  echo "FAIL legal plan rejected: exit $got" >&2
  fail=1
else
  grep -q 'check-only: parameters accepted, nothing written' "$out/case-$cases.log" \
    || { echo "FAIL legal plan did not report acceptance" >&2; fail=1; }
  grep -q 'plan: replace browser-probe.elf' "$out/case-$cases.log" \
    || { echo "FAIL legal plan did not name the replaced slot" >&2; fail=1; }
  grep -q 'catch.gtapp /boot/native-fault.elf /boot/native-peer.elf /boot/browser-probe.elf' \
    "$out/case-$cases.log" \
    || { echo "FAIL legal plan changed the four-module order" >&2; fail=1; }
  printf 'accepted %-28s exit %d\n' 'legal replacement plan' "$got"
fi
[ -e "$plan" ] && { echo "FAIL check-only wrote the output directory" >&2; fail=1; }

# 2. A fifth module (any name outside the four slots) is refused.
expect_refusal 'fifth module' 2 --check-only "$plan" "$kernel" "extra-probe.elf=$probe"
# 3. Directory traversal in the slot name is refused.
expect_refusal 'traversal ../' 2 --check-only "$plan" "$kernel" "../escape.elf=$probe"
# 4. A nested path in the slot name is refused.
expect_refusal 'nested path' 2 --check-only "$plan" "$kernel" "sub/dir.elf=$probe"
# 5. A backslash in the slot name is refused.
expect_refusal 'backslash name' 2 --check-only "$plan" "$kernel" 'win\path.elf='"$probe"
# 6. The same slot twice is refused.
expect_refusal 'duplicate slot' 2 --check-only "$plan" "$kernel" \
  "browser-probe.elf=$probe" "browser-probe.elf=$probe"
# 7. A bare path with no slot name is refused.
expect_refusal 'bare path' 2 --check-only "$plan" "$kernel" "$probe"
# 8. A missing module source is refused.
expect_refusal 'missing source' 2 --check-only "$plan" "$kernel" \
  "native-peer.elf=$out/does-not-exist.elf"
# 9. A missing kernel is refused.
expect_refusal 'missing kernel' 2 --check-only "$plan" "$out/no-kernel.bin"

# 10. An existing output directory is refused even without an ISO in it.
cases=$((cases + 1))
/usr/bin/bash "$entry" --check-only "$out" "$kernel" > "$out/case-$cases.log" 2>&1
got=$?
if [ "$got" -ne 2 ]; then
  echo "FAIL existing output directory accepted: exit $got" >&2
  fail=1
else
  printf 'rejected %-28s exit %d\n' 'existing output dir' "$got"
fi

# Nothing above may have materialised boot content.
if [ -e "$out/iso" ] || [ -e "$out/GTOS.iso" ]; then
  echo "FAIL a rejected plan wrote boot content" >&2
  fail=1
fi

echo "cases=$cases"
[ "$fail" -eq 0 ] && echo 'ISO precheck PASS' || echo 'ISO precheck FAIL'
exit "$fail"
