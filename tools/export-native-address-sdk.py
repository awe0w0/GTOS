#!/usr/bin/env python3
"""Export the narrow GTOS address overlay ahead of the existing SDK search root.

    export-native-address-sdk.py FRESH_OUT

Exactly three members, and only three:

    include/net/address_types.h -> sdk/net/address_types.h
    include/sys/socket.h        -> sdk/sys/socket.h
    include/netinet/in.h        -> sdk/netinet/in.h

The first carries the GTOS identity gate and the three family constants; the other
two are names that resolve to it and declare no function. They are copied
verbatim: this tool does not rewrite, reformat or regenerate any header.

The ten canonical llvm-libc type headers are NOT exported. They already live in
the original `tools/gtos_native_sdk/sdk` and are included by their real names, so
duplicating them here would create a second, divergent copy of exactly the
typedefs this revision is required to reuse rather than restate. The overlay is a
thin layer on top of that SDK, not a standalone SDK.

Nothing in the repository's `include/` tree is added to a search path by this
tool; the exported directory is what a consumer points at. An existing output
directory is refused rather than overwritten.
"""
import hashlib
import json
import pathlib
import sys

# logical name in the overlay -> path inside the checkout
MEMBERS = (
    ('net/address_types.h', 'include/net/address_types.h'),
    ('sys/socket.h', 'include/sys/socket.h'),
    ('netinet/in.h', 'include/netinet/in.h'),
)

# Provided by the original SDK and deliberately not duplicated here.
SDK_PROVIDED = (
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

# Not in the fixed membership of this revision, and not exported.
NOT_EXPORTED_NAMES = ('AF_UNIX', 'addrinfo', 'SOCK_*', 'MSG_*', 'AI_*',
                      'IPPROTO_*', 'INADDR_*', 'IN6ADDR_*', 'IF_NAMESIZE')


def require(condition, message):
    if not condition:
        raise SystemExit('refused: ' + message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def repository_root():
    return pathlib.Path(__file__).resolve().parent.parent


def main(argv):
    # argv is sys.argv[1:]: exactly one positional, the fresh output directory.
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    root = repository_root()
    require((root / 'include/net').is_dir(),
            'not inside a GTOS checkout: ' + str(root))
    output = pathlib.Path(argv[0])

    # Every member is validated before anything is written.
    plan = []
    for logical, relative in MEMBERS:
        source = root / relative
        require(source.is_file() and not source.is_symlink(),
                'source is not a regular file: ' + relative)
        resolved = source.resolve()
        root_resolved = root.resolve()
        require(resolved == root_resolved or root_resolved in resolved.parents,
                'source escapes the checkout: ' + relative)
        plan.append({'logical': logical,
                     'source': str(source),
                     'target': str(output / 'sdk' / logical),
                     'bytes': source.stat().st_size,
                     'sha256': sha256(source)})

    require(not output.exists() and not output.is_symlink(),
            'the output already exists; a fresh directory is required: ' + str(output))

    written = []
    for item in plan:
        target = pathlib.Path(item['target'])
        target.parent.mkdir(parents=True, exist_ok=True)
        require(not target.exists() and not target.is_symlink(),
                'the target already exists: ' + item['logical'])
        target.write_bytes(pathlib.Path(item['source']).read_bytes())
        require(target.is_file() and not target.is_symlink(),
                'the exported member is not a regular file: ' + item['logical'])
        actual = sha256(target)
        require(actual == item['sha256'],
                'the exported member differs from its source: ' + item['logical'])
        written.append({'logical': item['logical'], 'target': str(target),
                        'bytes': target.stat().st_size, 'sha256': actual})

    manifest = {
        'schema_version': 1,
        'checkout': str(root),
        'kind': 'address-overlay',
        'members': written,
        'sdk_provided_not_duplicated': list(SDK_PROVIDED),
        'not_exported_names': list(NOT_EXPORTED_NAMES),
        'layer': 'thin overlay on the original tools/gtos_native_sdk/sdk; not a '
                 'standalone SDK',
        'declares_functions': False,
        'note': 'The ten canonical type headers resolve from the original SDK and '
                'are intentionally not copied here. This overlay adds the GTOS '
                'identity gate, the three AF constants and the two names '
                '<sys/socket.h> and <netinet/in.h>. No socket function is '
                'declared and no kernel socket ABI is implied.',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'output': str(output),
                      'members': [item['logical'] for item in written],
                      'total_bytes': sum(item['bytes'] for item in written)},
                     indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
