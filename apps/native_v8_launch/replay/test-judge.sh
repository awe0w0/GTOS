#!/bin/sh
# Real test for the guest judge and for the replay's start conditions.
#
# The fixture is a real debug-console capture (the 32M/smp4 run), and every case is that capture with exactly one thing changed, so
# each rejection names a condition the production judge really checks. The judge is not re-implemented here: every case calls
# guest.py, the same entry the boot path uses. The start conditions (existing evidence, tool mode, image identity) are exercised
# without starting QEMU, because those checks happen before anything is executed.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fixture=$here/fixtures/v8-guest-32m-smp4.log
[ -f "$fixture" ] || { echo 'judge test: the fixture is missing' >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
pass=0
fail=0

expect() {
    description=$1; wanted=$2; shift 2
    set +e
    "$@" > "$work/out.log" 2> "$work/err.log"
    status=$?
    set -e
    if [ "$status" -eq "$wanted" ]; then
        printf 'PASS: %s\n' "$description"; pass=$((pass + 1))
    else
        printf 'FAIL: %s (status %s, wanted %s)\n' "$description" "$status" "$wanted" >&2
        head -n 12 "$work/err.log" >&2
        fail=$((fail + 1))
    fi
}

judge() {
    log=$1; shift
    python3 "$here/guest.py" judge "$log" "$@"
}

# 1. The unmodified capture passes, and only with the pass exit.
expect 'the real capture passes' 0 judge "$fixture" 33
expect 'a zero exit is not a pass' 1 judge "$fixture" 0
expect 'a timeout is not a pass' 1 judge "$fixture" timeout
expect 'the fail exit 35 is not a pass' 1 judge "$fixture" 35

# 2. The reap must name both sides before it may compare them.
grep -v '^V8 REAP ' "$fixture" > "$work/no-reap.log"
printf 'V8 REAP checks=00000C1F\n' >> "$work/no-reap.log"
expect 'a reap without free and baseline is refused' 1 judge "$work/no-reap.log" 33
if grep -q 'reap names both sides' "$work/out.log"; then
    printf 'PASS: the missing pair is named as the reason\n'; pass=$((pass + 1))
else
    printf 'FAIL: the missing pair was not named\n' >&2; fail=$((fail + 1))
fi

sed 's/free=0000113D/free=0000113E/' "$fixture" > "$work/drift.log"
expect 'a reap that does not return to baseline is refused' 1 judge "$work/drift.log" 33
if grep -q 'reap returns to baseline' "$work/out.log"; then
    printf 'PASS: the drift is named as the reason\n'; pass=$((pass + 1))
else
    printf 'FAIL: the drift was not named\n' >&2; fail=$((fail + 1))
fi

# 3. Records must be complete, singular and numeric.
sed 's/ calls=0000180F//' "$fixture" > "$work/no-calls.log"
expect 'an execution record without calls is refused' 1 judge "$work/no-calls.log" 33
sed 's/calls=0000180F/calls=zzzz0000/' "$fixture" > "$work/not-a-number.log"
expect 'a non-numeric field is refused' 1 judge "$work/not-a-number.log" 33
sed 's/^V8 WORKLOAD mask=0000007F gc=00000002$/&\nV8 WORKLOAD mask=0000007F gc=00000002/' "$fixture" > "$work/duplicate.log"
expect 'a repeated record is refused' 1 judge "$work/duplicate.log" 33

# 4. The workload itself has to be the one that ran.
sed 's/mask=0000007F/mask=0000007E/' "$fixture" > "$work/mask.log"
expect 'a different workload mask is refused' 1 judge "$work/mask.log" 33
sed 's/gc=00000002/gc=00000000/' "$fixture" > "$work/no-gc.log"
expect 'a workload without a collection is refused' 1 judge "$work/no-gc.log" 33

# 5. The pass has to be the guest's own single statement, with nothing failed.
grep -v 'RUN PASS' "$fixture" > "$work/no-pass.log"
expect 'a capture without the pass line is refused' 1 judge "$work/no-pass.log" 33
cat "$fixture" > "$work/two-pass.log"
printf 'GTOS REAL V8 LARGE HEAP RUN PASS\n' >> "$work/two-pass.log"
expect 'two pass lines are refused' 1 judge "$work/two-pass.log" 33
cat "$fixture" > "$work/failed.log"
printf 'FAILED something the kernel did not accept\n' >> "$work/failed.log"
expect 'a failed check line is refused' 1 judge "$work/failed.log" 33

# 6. Start conditions, exercised before anything is executed.
mkdir -p "$work/out" "$work/results"
printf 'kernel\n' > "$work/out/v8-kernel.bin"
printf '#!/bin/sh\nexit 0\n' > "$work/tool"
chmod 0755 "$work/tool"
printf '#!/bin/sh\nexit 0\n' > "$work/loader"
chmod 0755 "$work/loader"
boot() {
    OUT=$work/out RESULTS=$work/results ELF=$fixture QEMU=$1 LOADER=$2 QEMU_LIB=$work QEMU_DATA=$work \
        QEMU_MODULE_DIR=$work MEMORY=32M SMP=4 TAG=32M-smp4 python3 "$here/guest.py"
}
printf 'old evidence\n' > "$work/results/e9-32M-smp4.log"
before=$(sha256sum "$work/results/e9-32M-smp4.log" | cut -d' ' -f1)
expect 'existing evidence stops the run' 2 boot "$work/tool" "$work/loader"
after=$(sha256sum "$work/results/e9-32M-smp4.log" | cut -d' ' -f1)
if [ "$before" = "$after" ] && grep -q 'refusing to overwrite existing evidence' "$work/err.log"; then
    printf 'PASS: the existing evidence is untouched and the reason is named\n'; pass=$((pass + 1))
else
    printf 'FAIL: existing evidence was modified or the reason was missing\n' >&2; fail=$((fail + 1))
fi
rm -f "$work/results/e9-32M-smp4.log"
chmod 0644 "$work/tool"
expect 'a tool without an execute bit stops the run' 2 boot "$work/tool" "$work/loader"
chmod 0755 "$work/tool"
expect 'an image that is not the pinned one stops the run' 3 env EXPECT_ELF_SHA256=deadbeef EXPECT_ELF_BYTES=1 \
    OUT=$work/out RESULTS=$work/results ELF=$fixture QEMU=$work/tool LOADER=$work/loader QEMU_LIB=$work QEMU_DATA=$work \
    QEMU_MODULE_DIR=$work MEMORY=32M SMP=4 TAG=32M-smp4 python3 "$here/guest.py"

printf 'judge test: %s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
