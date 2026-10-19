#!/usr/bin/env python3
"""Export a narrow native ABI overlay ahead of the existing SDK search root.

Two modes, and only two:

* default - exactly the two graphics headers that
  `apps/png_surface_probe` includes but the existing SDK does not provide:
  `process/resource_abi.h` and `process/surface_abi.h`.
* `--with-window` - the same two, plus `process/window_abi.h`,
  `process/window_client.h` and `process/native_window_adapter.h`, so a window
  consumer can resolve its wire header, its six-call wrapper and its small
  user-space window object from the overlay.

The six headers the existing SDK already provides (`abi.h`,
`environment_abi.h`, `file_abi.h`, `info_abi.h`, `tls.h`, `vm_abi.h`) continue to
resolve from the original SDK in both modes, unmodified. The kernel-side
`native_windows.h`, the GUI provider and the C++ kernel classes are never
exported: they are not application interfaces.

The source bytes are copied verbatim: this tool does not rewrite, reformat or
regenerate any header, and it never touches the original SDK or the repository
headers. An existing output directory is refused rather than overwritten.
"""
import argparse
import hashlib
import json
import pathlib
import sys

GRAPHICS_MEMBERS = (
    'include/process/resource_abi.h',
    'include/process/surface_abi.h',
)
# Only added by --with-window. The wire header carries the layout, the client
# header carries the six thin calls, and the adapter header carries the small
# user-space window object built on those calls. Nothing else about the window
# path is exported.
WINDOW_MEMBERS = (
    'include/process/window_abi.h',
    'include/process/window_client.h',
    'include/process/native_window_adapter.h',
)
SDK_PROVIDED = ('process/abi.h', 'process/environment_abi.h',
                'process/file_abi.h', 'process/info_abi.h',
                'process/tls.h', 'process/vm_abi.h')


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
    parser = argparse.ArgumentParser(
        description='Export the fixed native graphics ABI header subset.')
    parser.add_argument('output', help='a new directory to create; must not exist')
    parser.add_argument('--with-window', action='store_true',
                        help='also export process/window_abi.h and '
                             'process/window_client.h (opt-in; the default stays '
                             'the two graphics headers)')
    args = parser.parse_args(argv)

    root = repository_root()
    require((root / 'include/process').is_dir(),
            'not inside a GTOS checkout: ' + str(root))
    output = pathlib.Path(args.output)
    members = GRAPHICS_MEMBERS + (WINDOW_MEMBERS if args.with_window else ())

    # Every member is validated before anything is written.
    plan = []
    for relative in members:
        source = root / relative
        require(source.is_file() and not source.is_symlink(),
                'source is not a regular file: ' + relative)
        resolved = source.resolve()
        root_resolved = root.resolve()
        require(resolved == root_resolved
                or root_resolved in resolved.parents,
                'source escapes the checkout: ' + relative)
        name = pathlib.PurePosixPath(relative).name
        target = output / 'sdk/process' / name
        plan.append({'logical': 'process/' + name,
                     'source': str(source), 'target': str(target),
                     'bytes': source.stat().st_size,
                     'sha256': sha256(source)})

    require(not output.exists() and not output.is_symlink(),
            'the output already exists; a fresh directory is required: ' + str(output))
    (output / 'sdk/process').mkdir(parents=True)
    require(not (output / 'sdk/process').is_symlink(),
            'the output process directory must be a real directory')

    written = []
    for item in plan:
        source = pathlib.Path(item['source'])
        target = pathlib.Path(item['target'])
        require(not target.exists() and not target.is_symlink(),
                'the target already exists: ' + item['logical'])
        target.write_bytes(source.read_bytes())
        require(target.is_file() and not target.is_symlink(),
                'the exported member is not a regular file: ' + item['logical'])
        actual = sha256(target)
        require(actual == item['sha256'],
                'the exported member differs from its source: ' + item['logical'])
        written.append({'logical': item['logical'], 'target': str(target),
                        'bytes': target.stat().st_size, 'sha256': actual})

    manifest = {'schema_version': 1,
                'checkout': str(root),
                'mode': 'with-window' if args.with_window else 'graphics-only',
                'members': written,
                'not_exported': list(SDK_PROVIDED),
                'note': 'The six headers above already resolve from the original '
                        'tools/gtos_native_sdk/sdk and are intentionally not '
                        'duplicated here. The kernel-side native_windows.h, the '
                        'GUI provider and the C++ kernel classes are never '
                        'exported.'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'output': str(output),
                      'mode': manifest['mode'],
                      'members': [item['logical'] for item in written],
                      'total_bytes': sum(item['bytes'] for item in written)},
                     indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
