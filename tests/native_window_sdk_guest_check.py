#!/usr/bin/env python3
"""Read one finished guest run and check what the probe actually reported.

    native_window_sdk_guest_check.py GUEST_OUT

GUEST_OUT is the runner's own output directory (the one holding `results.json`
and `debug.log`). Nothing here starts QEMU, edits the run or repairs anything: it
reads the retained bytes and refuses to pass on a log that does not contain the
four lines this round is about.

The point is narrow. The main runner already decides whether the window module
works. This check decides only whether *this* build reached the kernel the way it
claims to:

  * `GTOS NATIVE WINDOW SDK CALLS V1 <wrapped> <raw>` must exist, with a positive
    count on both sides. A zero on either side means one of the two paths never
    ran, which would make the claim half-true rather than false.
  * `<raw>` must equal the number of raw call sites the probe source contains, so
    the counter cannot drift away from the code it is describing. That number is
    derived from the source in front of us, not remembered: the five `raw*`
    helpers are defined once each and every other occurrence is a call site.
  * The full-target copy refusal must still report `48 8 40`, because the wrapper
    must not have changed what the kernel sees.
  * The PASS line must appear exactly once and no runtime failure may appear.
  * The adapter stage's own completion line must appear exactly once, so the
    user-space object path is known to have finished rather than merely compiled.
  * The adapter's lifecycle stage must report a positive case count. It is the
    stage that covers the BAD_STATE, failed-INFO and externally-revoked-handle
    sequences, so a run that skipped it cannot pass on the older checks.
"""
import json
import pathlib
import re
import sys

# The probe source has exactly this many raw call sites. They are the deliberately
# malformed requests - bad request size, bad version, bad destination pointer -
# that must reach the kernel instead of being screened in user space, plus the
# bare CLOSEs and the one bare PRESENT the adapter lifecycle stage uses to revoke a
# window behind the object's back. The list is pinned here AND re-derived from the
# source below, so neither a silent edit nor a stale constant can pass.
#
# 34 = 40 occurrences of the six helper names minus their six definitions. The
# earlier pin of 33 was wrong by one: it was written when the lifecycle stage was
# planned rather than counted, and the guest run itself reported 34, so the pin -
# not the source - was the stale number.
EXPECTED_RAW_SITES = 34
RAW_HELPERS = ('rawcreate', 'rawsubmit', 'rawpresent', 'rawinfo', 'rawiread',
               'rawclose')
PROBE_SOURCE = pathlib.Path(__file__).resolve().parent / '..' / 'apps' / \
    'native_window_probe' / 'native_main.cc'


def raw_sites():
    """Count raw call sites in the probe source: uses minus the definitions."""
    text = PROBE_SOURCE.read_text()
    total = 0
    for name in RAW_HELPERS:
        occurrences = len(re.findall(r'\b%s\s*\(' % name, text))
        # One occurrence per helper is its own definition.
        if occurrences < 1:
            return None, 'the probe source no longer mentions ' + name
        total += occurrences - 1
    return total, None


def fail(message):
    print('GUEST SDK CHECK FAIL: ' + message)
    return 1


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = pathlib.Path(argv[1])
    results_path = out / 'results.json'
    debug_path = out / 'debug.log'
    if not results_path.is_file():
        return fail('results.json is missing from ' + str(out))
    if not debug_path.is_file():
        return fail('debug.log is missing from ' + str(out))

    results = json.loads(results_path.read_text())
    text = debug_path.read_text()

    problems = []
    derived, why = raw_sites()
    if derived is None:
        problems.append(why)
    elif derived != EXPECTED_RAW_SITES:
        problems.append('the probe source now has %d raw call sites, the checker '
                        'pins %d' % (derived, EXPECTED_RAW_SITES))
    if results.get('guest_pass') is not True:
        problems.append('the runner did not report guest_pass')
    missing = results.get('missing_checks') or []
    if missing:
        problems.append('the runner reported missing checks: %s' % ', '.join(missing))

    matches = re.findall(r'GTOS NATIVE WINDOW SDK CALLS V1 (\d+) (\d+)', text)
    if len(matches) != 1:
        problems.append('expected exactly one SDK CALLS line, found %d'
                        % len(matches))
        wrapped = raw = None
    else:
        wrapped, raw = (int(value) for value in matches[0])
        if wrapped <= 0:
            problems.append('no normal call went through the SDK wrapper')
        if raw <= 0:
            problems.append('no malformed request went through the raw path')
        if raw != EXPECTED_RAW_SITES:
            problems.append('raw call count is %d, the probe source has %d raw '
                            'call sites' % (raw, EXPECTED_RAW_SITES))

    # The wrapper must not have altered the request the kernel receives: the
    # full-target copy refusal still has to report the same three numbers.
    if 'GTOS NATIVE WINDOW CROSS READONLY V1 48 8 40' not in text:
        problems.append('the 48/8/40 full-target copy refusal is not in the log')

    if text.count('GTOS NATIVE WINDOW PASS') != 1:
        problems.append('the PASS line does not appear exactly once')
    # The adapter stage prints this after its final CHECK. It is a separate line
    # from the window PASS, because the two paths are separate claims.
    if text.count('GTOS NATIVE WINDOW ADAPTER PASS V1') != 1:
        problems.append('the adapter stage did not report its completion line')
    if 'NATIVE RUNTIME FAIL' in text or 'NATIVE WINDOW FAIL' in text:
        problems.append('a runtime failure is present in the log')

    for name in ('INPUT_COPY_DONE', 'RESET_COPY_DONE', 'CLOSE_COPY_DONE',
                 'ADAPTER_LIFECYCLE_DONE'):
        found = re.search(r'GTOS NATIVE WINDOW STAGE %s V1 (\d+)' % name, text)
        if not found or int(found.group(1)) <= 0:
            problems.append('stage %s did not report a positive case count' % name)

    if problems:
        for problem in problems:
            print('GUEST SDK CHECK FAIL: ' + problem)
        return 1
    print('wrapped calls %d, raw calls %d (expected %d raw sites)'
          % (wrapped, raw, EXPECTED_RAW_SITES))
    print('GUEST SDK CHECK PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
