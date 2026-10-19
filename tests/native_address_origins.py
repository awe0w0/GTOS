#!/usr/bin/env python3
"""Check where the GTOS address layer's headers actually came from.

    native_address_origins.py INCLUDE_LOG EXPORT_DIR SDK_DIR REAL_DIR REPO_DIR \
        [with-build-config|without-build-config]

INCLUDE_LOG is a clang `-H` transcript. This script reads the opened paths out of
it and refuses if the story is not the intended one:

  * `net/address_types.h`, `sys/socket.h` and `netinet/in.h` are the three members
    of THIS round's export, and each is opened exactly once;
  * the ten canonical llvm-libc type headers and the stdint macro header they
    depend on are opened from the bound original SDK, exactly once each - which is
    the point of reusing them instead of restating the typedefs;
  * with `with-build-config` (the default), `build/build_config.h` is opened from
    the real Chromium view, not from a copy. The C unit passes
    `without-build-config`: it is a pure C layout unit and never includes the
    Chromium build config, so demanding it there would be a false expectation;
  * nothing is opened from the GTOS repository's own include tree, and nothing
    from a host system include directory.

It reports paths, not counts: a header reached twice means two copies of the same
type are in play, which is the failure this revision exists to avoid.
"""
import pathlib
import re
import sys

CANONICAL = (
    'llvm-libc-types/socklen_t.h',
    'llvm-libc-types/sa_family_t.h',
    'llvm-libc-types/in_addr_t.h',
    'llvm-libc-types/in_port_t.h',
    'llvm-libc-types/struct_sockaddr.h',
    'llvm-libc-types/struct_in_addr.h',
    'llvm-libc-types/struct_in6_addr.h',
    'llvm-libc-types/struct_sockaddr_in.h',
    'llvm-libc-types/struct_sockaddr_in6.h',
    'llvm-libc-types/struct_sockaddr_storage.h',
    'llvm-libc-macros/stdint-macros.h',
)

OVERLAY = (
    'net/address_types.h',
    'sys/socket.h',
    'netinet/in.h',
)

HOST_PREFIXES = ('/usr/include', '/usr/local/include', '/usr/lib',
                 '/opt/', '/Library/')


def parse(log_text):
    seen = {}
    for line in log_text.splitlines():
        match = re.match(r'^(?:\.+ )?(/\S+)$', line)
        if not match:
            continue
        path = match.group(1)
        seen[path] = seen.get(path, 0) + 1
    return seen


def main(argv):
    if len(argv) not in (6, 7, 8):
        print(__doc__, file=sys.stderr)
        return 2
    log_path = pathlib.Path(argv[1])
    export_dir = argv[2]
    sdk_dir = argv[3]
    real_dir = argv[4]
    repo_dir = argv[5]
    mode = argv[6] if len(argv) >= 7 else 'with-build-config'
    if mode not in ('with-build-config', 'without-build-config'):
        print('unknown mode: ' + mode, file=sys.stderr)
        return 2
    want_build_config = mode == 'with-build-config'
    # Which overlay names this unit is expected to open. Default is all three; a
    # unit that only includes <sys/socket.h> must not be faulted for not opening
    # <netinet/in.h>.
    expected_overlay = OVERLAY if len(argv) == 6 or len(argv) == 7 else tuple(
        argv[7].split(','))
    unknown = [name for name in expected_overlay if name not in OVERLAY]
    if unknown:
        print('unknown overlay member: %s' % unknown, file=sys.stderr)
        return 2

    seen = parse(log_path.read_text(errors='replace'))
    if not seen:
        print('ORIGIN FAIL: the -H transcript lists no headers at all')
        return 1

    problems = []

    def expect_once(suffix, prefix, label):
        found = [path for path in seen if path.endswith('/' + suffix)]
        if len(found) != 1:
            problems.append('%s opened %d times: %s' % (suffix, len(found), found))
        elif not found[0].startswith(prefix):
            problems.append('%s came from %s, not the %s' % (suffix, found[0], label))

    for member in expected_overlay:
        expect_once(member, export_dir, 'export overlay')
    for member in CANONICAL:
        expect_once(member, sdk_dir, 'bound original SDK')
    if want_build_config:
        expect_once('build/build_config.h', real_dir, 'real Chromium view')
    else:
        stray = [path for path in seen if path.endswith('/build/build_config.h')]
        if stray:
            problems.append('build/build_config.h appeared in a unit that must '
                            'not include it: %s' % stray)

    leaked = [path for path in seen if path.startswith(repo_dir + '/include')]
    if leaked:
        problems.append('headers came from the GTOS repository include tree: %s'
                        % sorted(leaked))
    host = [path for path in seen
            if path.startswith(HOST_PREFIXES)]
    if host:
        problems.append('host system headers were opened: %s' % sorted(host))

    for path in sorted(seen):
        print(path)
    if problems:
        for problem in problems:
            print('ORIGIN FAIL: ' + problem)
        return 1
    print('ORIGINS PASS: %d headers, %d canonical, %d overlay'
          % (len(seen), len(CANONICAL), len(expected_overlay)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
