#!/usr/bin/env python3
"""Compare the values the ABI image printed with a reference computed from the documented semantics.

The reference is computed here, in Python, from the same description the two sources implement: a wrapped 32-bit add, the two
struct folds, a 64-bit return split across the high and low halves, the table entries, and the pointer-out store. It is not derived
from the image or from its disassembly, so agreement means the started program's arithmetic and calling convention matched an
independent statement of what they should be.

The ten lines the image prints are, in order: sum, folded, copied, wide high, wide low, table sum, table wide high, table wide
low, stored, fold.
"""
import argparse
import sys

LOW = 0x11111111
HIGH = 0x2222
TAG = 0x33
MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF


def reference():
    """The expected values, computed from the semantics each side documents."""
    total = (LOW + HIGH) & MASK32
    folded = LOW ^ ((HIGH << 8) & MASK32) ^ TAG
    copied = LOW ^ ((HIGH << 8) & MASK32) ^ TAG
    wide = ((total << 32) | folded) & MASK64
    table_sum = (LOW + HIGH) & MASK32
    table_wide = ((table_sum << 32) | folded) & MASK64
    stored = 0x5AA51234
    fold = (total ^ folded ^ copied ^ (wide >> 32) ^ (wide & MASK32) ^ table_sum
            ^ (table_wide >> 32) ^ (table_wide & MASK32)) & MASK32
    return {
        'sum': total,
        'folded': folded,
        'copied': copied,
        'wide_high': (wide >> 32) & MASK32,
        'wide_low': wide & MASK32,
        'table_sum': table_sum,
        'table_wide_high': (table_wide >> 32) & MASK32,
        'table_wide_low': table_wide & MASK32,
        'stored': stored,
        'fold': fold,
    }


ORDER = ['sum', 'folded', 'copied', 'wide_high', 'wide_low', 'table_sum', 'table_wide_high', 'table_wide_low', 'stored', 'fold']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, help='output the started image printed')
    args = parser.parse_args()
    lines = [line.strip() for line in open(args.log) if line.strip()]
    expected = reference()
    failures = 0
    if len(lines) != len(ORDER):
        print('FAIL the image printed %d lines, expected %d: %s' % (len(lines), len(ORDER), lines), file=sys.stderr)
        return 1
    for name, line in zip(ORDER, lines):
        try:
            actual = int(line, 16)
        except ValueError:
            print('FAIL %s is not a hexadecimal value: %r' % (name, line), file=sys.stderr)
            failures += 1
            continue
        if actual != expected[name]:
            print('FAIL %s is 0x%08x, expected 0x%08x' % (name, actual, expected[name]), file=sys.stderr)
            failures += 1
        else:
            print('ok %s 0x%08x' % (name, actual))
    # The two claims that are properties of the pair rather than of one call: the table's entry points are the same functions, and
    # the pointer-out store arrived.
    pairs = [('sum', 'table_sum'), ('wide_high', 'table_wide_high'), ('wide_low', 'table_wide_low')]
    for left, right in pairs:
        if expected[left] != expected[right]:
            print('FAIL the reference disagrees with itself about %s and %s' % (left, right), file=sys.stderr)
            failures += 1
    if lines[ORDER.index('stored')].strip() != '5aa51234':
        print('FAIL the pointer-out store did not arrive', file=sys.stderr)
        failures += 1
    else:
        print('ok the pointer-out store arrived as 0x5aa51234')
    if failures:
        print('rust ABI run: %d failure(s)' % failures, file=sys.stderr)
        return 1
    print('PASS rust ABI run: every value the started image printed matches the independent reference')
    return 0


if __name__ == '__main__':
    sys.exit(main())
