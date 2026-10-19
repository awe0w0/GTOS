#!/usr/bin/env python3
"""Static geometry and wiring check for the reclaim runner, no guest.

Imports the runner as a module (its main() is guarded) and verifies the values the
screen criterion depends on, so a wrong strip cannot be discovered only by
booting a guest.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import native_window_reclaim_qemu as runner  # noqa: E402

problems = []

if runner.WINDOW != (208, 158, 480, 360):
    problems.append('slot-0 window rect changed: %r' % (runner.WINDOW,))
if runner.CONTENT != (400, 310, 96, 96):
    problems.append('content rect changed: %r' % (runner.CONTENT,))
if runner.STRIP != (208, 480, 372, 36):
    problems.append('strip changed: %r' % (runner.STRIP,))
if runner.SCALE != 6:
    problems.append('scale changed: %r' % (runner.SCALE,))
for required in ('gui_slots_closed', 'window_removed_positive'):
    if required not in runner.REQUIRED_CHECKS:
        problems.append('required check missing: ' + required)
if 'window_gone_after_reap' in runner.REQUIRED_CHECKS:
    problems.append('the old diff-based check is still required')
# The peer tone must differ from the fault tone, or the two blends would be the
# same expectation and one of them could not be distinguished.
if runner.PEER_TONE == runner.FAULT_TONE:
    problems.append('peer and fault tones are equal')
# The strip must be outside the pointer's final position.
sx, sy, sw, sh = runner.STRIP
px, py = runner.OUTSIDE
if sx <= px < sx + sw and sy <= py < sy + sh:
    problems.append('the pointer ends inside the strip')

for problem in problems:
    print('GEOMETRY FAIL: ' + problem)
print('RECLAIM RUNNER GEOMETRY PASS' if not problems else 'RECLAIM RUNNER GEOMETRY FAIL')
print('window=%r content=%r strip=%r scale=%d required=%d'
      % (runner.WINDOW, runner.CONTENT, runner.STRIP, runner.SCALE,
         len(runner.REQUIRED_CHECKS)))
sys.exit(0 if not problems else 1)
