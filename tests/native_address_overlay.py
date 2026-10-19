#!/usr/bin/env python3
"""Build the controlled GTOS overlay for the real Chromium sockaddr storage header.

    native_address_overlay.py REAL_SOURCE FRESH_OUT

REAL_SOURCE is the locked Chromium view the reviewed M run used:

    .../native-content-mojom-core-consumer-20261019-a/run-10/source

Nothing under REAL_SOURCE is written. Two headers are read, their digests are
checked against the pins below, and only `net/base/sockaddr_storage.h` is copied
into a fresh OUT and changed. Everything else - `build/build_config.h` in
particular - keeps resolving from the real tree.

The change is one semantic edit and nothing else:

    #elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
    #elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_GTOS)

Why it is needed rather than cosmetic: that header only reaches <sys/socket.h>
under IS_WIN or IS_POSIX/IS_FUCHSIA, and on GTOS both are 0, so the header does
not define `sockaddr_storage` or `socklen_t` for itself. A test translation unit
that included <sys/socket.h> first would compile anyway - and would be proving the
include ORDER, not that the header stands on its own. This overlay makes the
header self-sufficient; the consumer check then includes it alone.

The GTOS identity is not invented here: `build/build_config.h` in the locked view
already derives OS_GTOS from `__GTOS__` and already defines
BUILDFLAG_INTERNAL_IS_GTOS(). No Linux, Apple, Windows, Android, POSIX, Fuchsia or
Ozone macro is defined to get here, and the WIN arm is untouched.
"""
import difflib
import hashlib
import json
import pathlib
import sys

PINS = {
    'net/base/sockaddr_storage.h':
        'f11db8dd752961a5f65d21e6477d9e3b1f29f526a5097bbc09528a87ab7ff192',
    'build/build_config.h':
        'bd185448071dbd2907bdf2896ff754573ee841e8c4cdc939595c00c53e9a5216',
}

TARGET = 'net/base/sockaddr_storage.h'

# old -> new. Each old string must appear exactly once in the real header.
REPLACEMENTS = [
    (
        '#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)\n'
        '#include <sys/socket.h>\n',
        '#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA) || '
        'BUILDFLAG(IS_GTOS)\n'
        '#include <sys/socket.h>\n',
    ),
]


def require(condition, message):
    if not condition:
        raise SystemExit('refused: ' + message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source = pathlib.Path(argv[1])
    out = pathlib.Path(argv[2])
    require(source.is_dir(), 'REAL_SOURCE is not a directory: ' + str(source))
    require(not out.exists(), 'FRESH_OUT already exists: ' + str(out))

    binding = {}
    for relative, expected in sorted(PINS.items()):
        path = source / relative
        require(path.is_file(), 'the locked header is missing: ' + relative)
        data = path.read_bytes()
        actual = sha256_bytes(data)
        require(actual == expected,
                '%s is %s, pinned %s' % (relative, actual, expected))
        binding[relative] = {'bytes': len(data), 'sha256': actual,
                             'resolved': str(path.resolve())}

    original = (source / TARGET).read_bytes()
    text = original.decode('utf-8')

    patched = text
    applied = []
    for index, (old, new) in enumerate(REPLACEMENTS, start=1):
        count = patched.count(old)
        require(count == 1,
                'anchor %d appears %d times in %s' % (index, count, TARGET))
        patched = patched.replace(old, new)
        applied.append({'anchor': index, 'old_bytes': len(old),
                        'new_bytes': len(new)})

    # Removing the replacement must reproduce the original bytes exactly: the
    # overlay may differ from the real header by this edit and by nothing else.
    reverted = patched
    for old, new in REPLACEMENTS:
        reverted = reverted.replace(new, old)
    require(reverted.encode('utf-8') == original,
            'the patched header is not the original plus the one edit')

    # Structural checks the revert cannot see.
    require(patched.count('BUILDFLAG(IS_GTOS)') == 1,
            'the GTOS arm must appear exactly once')
    require('#if BUILDFLAG(IS_WIN)' in patched,
            'the Windows arm must survive')
    require(patched.count('#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA) || '
                          'BUILDFLAG(IS_GTOS)') == 1,
            'the POSIX/FUCHSIA arm must keep its body and gain only GTOS')
    require('#include <sys/types.h>' in patched,
            'the sys/types.h include must survive')
    require('struct sockaddr_storage {' in patched or
            'sockaddr_storage addr_storage;' in patched,
            'the SockaddrStorage body must be untouched')
    require(patched.count('#error') == text.count('#error'),
            'the overlay must not add or remove an #error')

    overlay = out / 'overlay'
    target = overlay / TARGET
    target.parent.mkdir(parents=True)
    target.write_bytes(patched.encode('utf-8'))

    diff = ''.join(difflib.unified_diff(
        text.splitlines(keepends=True), patched.splitlines(keepends=True),
        fromfile='a/' + TARGET, tofile='b/' + TARGET))
    (out / 'overlay.diff').write_text(diff)

    manifest = {
        'schema_version': 1,
        'real_source': str(source.resolve()),
        'target': TARGET,
        'original_bytes': len(original),
        'original_sha256': sha256_bytes(original),
        'patched_bytes': len(patched.encode('utf-8')),
        'patched_sha256': sha256_bytes(patched.encode('utf-8')),
        'replacements': applied,
        'semantic_change': 'the <sys/socket.h> include arm also covers '
                           'BUILDFLAG(IS_GTOS); no other branch, body or '
                           'condition changed',
        'raw_header_unchanged': True,
        'pins': binding,
        'note': 'build/build_config.h is not copied: it already derives OS_GTOS '
                'from __GTOS__ and already defines BUILDFLAG_INTERNAL_IS_GTOS(), '
                'so it keeps resolving from the real tree and the consumer TU '
                'proves that with -H.',
    }
    (out / 'overlay.json').write_text(json.dumps(manifest, indent=2,
                                                 sort_keys=True) + '\n')
    print('overlay: %s' % target)
    print('original %d bytes %s' % (len(original), manifest['original_sha256']))
    print('patched  %d bytes %s' % (manifest['patched_bytes'],
                                    manifest['patched_sha256']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
