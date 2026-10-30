#!/usr/bin/env python3
"""Keep the desktop test's two source lists from drifting apart again.

tests/desktop_test.sh compiles the same test body twice: once the way the repository's ordinary build does it, and once with
sanitizers for the workflow's strict step. The second invocation carries its own hand-written list of sources. When the test body
started calling the native window service, only the first list was extended, and the sanitized image stopped linking.

The two lists are not the same by design — the sanitizer build has different flags and drops nothing else — so the check is
containment: every project source the ordinary build compiles must also appear in the sanitizer build. Flags, output names and the
test body itself are free to differ.
"""
import argparse
import pathlib
import re
import sys

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'tests/desktop_test.sh'
SOURCE = re.compile(r'(?:^|\s)((?:src|tests)/[A-Za-z0-9_./*+-]+\.(?:cpp|cc|c|h))')


def lists(text):
    """Return (ordinary, sanitized) source lists, each as an ordered list of paths."""
    ordinary = []
    sanitized = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if '"${CXX:-g++}" $kernel_flags "$1"' in line:
            for following in lines[index:index + 8]:
                ordinary.extend(SOURCE.findall(following))
        elif '-fsanitize=address,undefined' in line:
            for following in lines[index:index + 6]:
                sanitized.extend(SOURCE.findall(following))
    return ordinary, sanitized


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script', default=str(SCRIPT), help='the desktop test script to read the two lists from')
    args = parser.parse_args()
    script = pathlib.Path(args.script)
    ordinary, sanitized = lists(script.read_text())
    failures = 0
    if not ordinary or not sanitized:
        print('FAIL could not read both source lists from %s (ordinary=%d, sanitized=%d)'
              % (script, len(ordinary), len(sanitized)), file=sys.stderr)
        return 1
    missing = [path for path in ordinary if path not in sanitized]
    for path in sorted(set(missing)):
        print('FAIL the sanitizer build does not compile %s, which the ordinary build does' % path, file=sys.stderr)
        failures += 1
    if not missing:
        print('ok every one of the %d sources the ordinary build compiles is also in the sanitizer build' % len(ordinary))
    extra = [path for path in sanitized if path not in ordinary]
    for path in sorted(set(extra)):
        print('ok the sanitizer build also compiles %s' % path)
    if failures:
        return 1
    print('PASS desktop sanitizer sources: the two lists cannot drift apart without this failing')
    return 0


if __name__ == '__main__':
    sys.exit(main())
