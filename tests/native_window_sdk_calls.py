#!/usr/bin/env python3
"""Bind each window opcode to the exact request byte count it must carry.

    native_window_sdk_calls.py DISASSEMBLY_LOG
    native_window_sdk_calls.py --self-test

The 009 checker only asked whether each trap carried a byte count drawn from the
set {20, 8, 12, 16}. That set is right, but membership is not the property we
care about: INFO must carry 12 and INPUT_READ must carry 16, and a build that
swapped them would still have passed. This helper pairs the opcode with its own
count, so a swap is a failure rather than a coincidence.

The pairing is stated once, in PAIRS, and both the real disassembly and the
in-memory counterexamples go through the same `analyse()` function. The
counterexamples are the two swaps that used to be invisible:

  * INFO (0x4754) carrying 20 instead of 12;
  * INPUT_READ (0x4755) carrying 8 instead of 16.

Both stay inside the old allowed set, so they are exactly the cases a set
membership test cannot catch.

This checks the emitted machine code of one translation unit. It is not a
control-flow analysis, and it says nothing about whether a request pointer is a
valid user address - that is the kernel's job and is proven in the guest.
"""
import re
import sys

# opcode -> (name, exact request byte count as the immediate appears in objdump)
PAIRS = {
    '0x4750': ('CREATE', '0x14'),
    '0x4751': ('SUBMIT', '0x14'),
    '0x4752': ('PRESENT', '0x8'),
    '0x4753': ('CLOSE', '0x8'),
    '0x4754': ('INFO', '0xc'),
    '0x4755': ('INPUT_READ', '0x10'),
}

TRAP = 'int $0x80'
# How far back an argument may be found. The wrapper is six instructions at most;
# 16 is generous and, crucially, is not used in place of a data relationship -
# the relationship is the opcode/count pair above.
BACK_LIMIT = 16


def mnemonics_of(text):
    """Every instruction's mnemonic column, in order.

    Only the mnemonic column is read. The raw byte column also contains the
    encoding `cd 80` of the same instruction, so matching whole lines would
    count every trap twice - which is exactly the mistake the first 009 run
    made.
    """
    found = []
    for line in text.splitlines():
        match = re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s)+\s*(\S.*)$', line)
        if match:
            found.append(' '.join(match.group(1).split()))
    return found


def writes(instruction, register):
    """True when the instruction's destination operand is `register`."""
    if instruction.startswith('pop '):
        return instruction.split()[-1] == register
    operands = instruction.split(None, 1)
    if len(operands) != 2:
        return False
    return operands[1].split(',')[-1].strip() == register


def immediate(instruction):
    match = re.match(r'^mov\s+\$(0x[0-9a-f]+),', instruction)
    if match:
        return match.group(1)
    match = re.match(r'^push\s+\$(0x[0-9a-f]+)$', instruction)
    return match.group(1) if match else None


def analyse(text):
    """Return (problems, pairs) for one disassembly. No printing, no exit."""
    instructions = mnemonics_of(text)
    problems = []
    traps = [index for index, item in enumerate(instructions)
             if re.fullmatch(re.escape(TRAP), item)]
    if len(traps) != len(PAIRS):
        problems.append('expected %d %s traps, found %d'
                        % (len(PAIRS), TRAP, len(traps)))
        return problems, []

    found = []
    for ordinal, trap in enumerate(traps):
        opcode = count = None
        for back in range(trap - 1, max(trap - BACK_LIMIT, -1), -1):
            instruction = instructions[back]
            if opcode is None and writes(instruction, '%eax'):
                opcode = immediate(instruction)
            if count is None and writes(instruction, '%ecx'):
                count = immediate(instruction)
                if count is None and instruction.startswith('pop'):
                    # A small constant is cheaper as push/pop than as a mov.
                    for deeper in range(back - 1, max(back - 4, -1), -1):
                        if instructions[deeper].startswith('push $'):
                            count = immediate(instructions[deeper])
                            break
            if opcode is not None and count is not None:
                break
        if opcode is None:
            problems.append('trap %d has no opcode in %%eax' % ordinal)
            continue
        if opcode not in PAIRS:
            problems.append('trap %d carries unknown opcode %s' % (ordinal, opcode))
            continue
        name, expected = PAIRS[opcode]
        if count != expected:
            problems.append('%s (%s) carries request bytes %r, must be %s'
                            % (name, opcode, count, expected))
        # The request pointer must travel in ebx: the ABI names ebx, so a
        # pointer arriving on the stack or in a host register would be a
        # different convention even if the trap and count looked right.
        if not any(writes(instructions[back], '%ebx')
                   for back in range(trap - 1, max(trap - BACK_LIMIT, -1), -1)):
            problems.append('%s (%s) has no request pointer in %%ebx'
                            % (name, opcode))
        found.append((opcode, name, count))

    seen = sorted(item[0] for item in found)
    if seen != sorted(PAIRS):
        problems.append('emitted opcodes are %s, expected exactly %s'
                        % (seen, sorted(PAIRS)))
    return problems, found


def disassembly(pairs):
    """Build a small objdump-shaped text carrying the given (opcode, count)."""
    lines = ['00000000 <probe>:']
    offset = 0
    for opcode, count in pairs:
        body = [
            ('b8', 'mov    $%s,%%eax' % opcode),
            ('bb', 'mov    $0x1,%ebx'),
            ('b9', 'mov    $%s,%%ecx' % count),
            ('cd', 'int    $0x80'),
        ]
        for index, (prefix, mnemonic) in enumerate(body):
            lines.append('  %x:\t%s 00 00 00 00    \t%s' % (offset, prefix, mnemonic))
            offset += 5
    return '\n'.join(lines) + '\n'


def self_test():
    """The two swaps that used to pass, plus the correct build."""
    problems = []

    correct = [(opcode, count) for opcode, (_, count) in sorted(PAIRS.items())]
    found, pairs = analyse(disassembly(correct))
    if found:
        problems.append('the correct pairing was rejected: %s' % found)

    swapped = []
    for opcode, count in correct:
        if opcode == '0x4754':
            count = '0x14'          # INFO carrying SUBMIT's count
        elif opcode == '0x4755':
            count = '0x8'           # INPUT_READ carrying CLOSE's count
        swapped.append((opcode, count))
    found, pairs = analyse(disassembly(swapped))
    if not found:
        problems.append('the two swapped counts were accepted')
    else:
        text = ' '.join(found)
        if 'INFO (0x4754) carries request bytes' not in text:
            problems.append('the INFO swap was not named: ' + text)
        if 'INPUT_READ (0x4755) carries request bytes' not in text:
            problems.append('the INPUT_READ swap was not named: ' + text)

    # A missing trap must not be silently tolerated either.
    found, pairs = analyse(disassembly(correct[:-1]))
    if not found:
        problems.append('a missing trap was accepted')

    for problem in problems:
        print('CALL PAIRING SELF TEST FAIL: ' + problem)
    print('CALL PAIRING SELF TEST PASS' if not problems else 'CALL PAIRING SELF TEST FAIL')
    return 0 if not problems else 1


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    if argv[1] == '--self-test':
        return self_test()
    text = open(argv[1]).read()
    problems, pairs = analyse(text)
    if problems:
        for problem in problems:
            print('CALL SITE FAIL: ' + problem)
        return 1
    print('CALL SITES PASS: ' + ', '.join(
        '%s %s %s' % (name, opcode, count) for opcode, name, count in pairs))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
