#!/usr/bin/env python3
"""Build the controlled GTOS overlay for the real Chromium gfx type header.

    native_gfx_types_overlay.py REAL_SOURCE FRESH_OUT

REAL_SOURCE is the locked Chromium view the reviewed M run used:

    .../native-content-mojom-core-consumer-20261019-a/run-07/source

Nothing under REAL_SOURCE is written. The two headers this step depends on are
read, their digests are checked against the pins below, and only
`ui/gfx/native_ui_types.h` is copied into a fresh OUT and extended. Everything
else - `build/build_config.h` in particular - keeps resolving from the real tree,
which is why the small TU later proves its own include origins with -H.

Why a patch instead of a rewritten header: the real file is 244 lines of a
Chromium header that other targets must keep seeing unchanged. Three insertions
are added at three exact anchors; the script refuses unless each anchor appears
exactly once, and it then proves that removing the three inserted blocks gives
back the original bytes. A mirror header written from scratch would prove nothing
about the real one.

The GTOS identity itself is NOT invented here: `build/build_config.h` in the
locked view already derives OS_GTOS from `__GTOS__` and already defines
BUILDFLAG_INTERNAL_IS_GTOS(). This overlay only adds the two gfx type branches
that header was still missing, and it does not define Linux, Apple, Windows,
Android or Ozone to get there.
"""
import difflib
import hashlib
import json
import pathlib
import sys

# The locked real headers this step is allowed to depend on. A different file at
# either path stops the run rather than silently proving something else.
PINS = {
    'ui/gfx/native_ui_types.h':
        '24a0b53bf57294c1703d09d5c56a8d4487098edcee496425b21387372747375d',
    'build/build_config.h':
        'bd185448071dbd2907bdf2896ff754573ee841e8c4cdc939595c00c53e9a5216',
}

TARGET = 'ui/gfx/native_ui_types.h'

# anchor -> text appended immediately after the anchor
INSERTIONS = [
    # After the AtkObject block: forward declarations for the GTOS types. The
    # adapter is only ever used as a pointer here, so a declaration is enough and
    # this header keeps no dependency on the native SDK.
    (
        '#if BUILDFLAG(IS_LINUX)\n'
        'extern "C" {\n'
        'struct _AtkObject;\n'
        'using AtkObject = struct _AtkObject;\n'
        '}\n'
        '#endif\n',
        '\n'
        '// GTOS: the window is a user-space object the caller owns, and an input\n'
        '// record is a fixed 16-byte value type. Only declarations are needed\n'
        '// here; the definitions come from <process/native_window_adapter.h> and\n'
        '// <process/window_abi.h> in the native SDK, which this header does not\n'
        '// include.\n'
        '#if BUILDFLAG(IS_GTOS)\n'
        'namespace gtos {\n'
        'namespace process {\n'
        'class NativeWindowAdapter;\n'
        '}  // namespace process\n'
        '}  // namespace gtos\n'
        'struct GtosInputEvent;\n'
        '#endif\n',
    ),
    # First chain (NativeView/NativeWindow/NativeEvent). GTOS is not Aura, not
    # iOS, not macOS and not Android, so it needs its own branch ahead of the
    # #error. This chain has no OZONE arm and none is added.
    (
        '#elif BUILDFLAG(IS_ANDROID)\n'
        'using NativeView = ui::ViewAndroid*;\n'
        'using NativeWindow = ui::WindowAndroid*;\n'
        'using NativeEvent = base::android::ScopedJavaGlobalRef<jobject>;\n',
        '#elif BUILDFLAG(IS_GTOS)\n'
        '// The view handle and the window handle are the same object on GTOS:\n'
        '// there is no view tree, so the window\'s root content view shares its\n'
        '// identity instead of introducing a second one.\n'
        'using NativeView = gtos::process::NativeWindowAdapter*;\n'
        'using NativeWindow = gtos::process::NativeWindowAdapter*;\n'
        'using NativeEvent = const GtosInputEvent*;\n',
    ),
    # Second chain (AcceleratedWidget). This one does have an OZONE arm; GTOS is
    # a separate branch, not Ozone. The anchor stops at the end of the Android
    # arm so the OZONE arm keeps its own condition and its own body.
    (
        '#elif BUILDFLAG(IS_ANDROID)\n'
        'using AcceleratedWidget = ANativeWindow*;\n'
        'constexpr AcceleratedWidget kNullAcceleratedWidget = nullptr;\n',
        '#elif BUILDFLAG(IS_GTOS)\n'
        '// The kernel\'s persistent 32-bit window handle. This is a kernel\n'
        '// identifier, not the adapter object: the adapter is a user-space\n'
        '// address and the two must not be reinterpret_cast into each other.\n'
        '// 0 means "no surface".\n'
        'using AcceleratedWidget = uint32_t;\n'
        'inline constexpr AcceleratedWidget kNullAcceleratedWidget = 0;\n',
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

    # Pin first: nothing is copied until the real headers are the pinned ones.
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

    # Every anchor must appear exactly once, so no insertion can land in the
    # wrong chain or be applied twice.
    patched = text
    applied = []
    for index, (anchor, insertion) in enumerate(INSERTIONS, start=1):
        count = patched.count(anchor)
        require(count == 1,
                'anchor %d appears %d times in %s' % (index, count, TARGET))
        patched = patched.replace(anchor, anchor + insertion)
        applied.append({'anchor': index, 'inserted_bytes': len(insertion)})

    # Prove the only difference is the three inserted blocks: removing them from
    # the patched text must reproduce the original bytes exactly.
    reverted = patched
    for anchor, insertion in INSERTIONS:
        reverted = reverted.replace(anchor + insertion, anchor)
    require(reverted.encode('utf-8') == original,
            'the patched header is not the original plus three insertions')

    require('#error Unknown build environment.' in patched,
            'the first chain still has its #error')
    require('#error unknown platform' in patched,
            'the second chain still has its #error')
    require('BUILDFLAG(IS_GTOS)' in patched, 'no GTOS branch was added')

    # The revert check above cannot see a branch that was inserted in the wrong
    # place, because reverting removes it again either way. So the two chains are
    # also checked structurally: the GTOS arm must own its own body, and the Ozone
    # arm must keep both its condition and its body, exactly once.
    ozone_arm = ('#elif BUILDFLAG(IS_OZONE)\n'
                 'using AcceleratedWidget = uint32_t;\n'
                 'inline constexpr AcceleratedWidget kNullAcceleratedWidget = 0;\n')
    require(patched.count('#elif BUILDFLAG(IS_OZONE)') == 1,
            'the Ozone arm must appear exactly once')
    require(ozone_arm in patched,
            'the Ozone arm lost its body: the GTOS branch was inserted in the '
            'wrong place')
    gtos_arm = ('#elif BUILDFLAG(IS_GTOS)\n'
                '// The kernel\'s persistent 32-bit window handle.')
    require(patched.count(gtos_arm) == 1, 'the GTOS widget arm is missing or repeated')
    require(patched.index(gtos_arm) < patched.index('#elif BUILDFLAG(IS_OZONE)'),
            'the GTOS widget arm must come before the Ozone arm')
    # Chain one has no Ozone arm and must not have gained one.
    require(patched.count('#error Unknown build environment.') == 1,
            'the first chain #error must appear exactly once')

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
        'insertions': applied,
        'added_branches': 2,
        'added_forward_declarations': 1,
        'unchanged_elsewhere': True,
        'pins': binding,
        'note': 'build/build_config.h is not copied: it already derives OS_GTOS '
                'from __GTOS__ and already defines BUILDFLAG_INTERNAL_IS_GTOS(), '
                'so it keeps resolving from the real tree and the small TU proves '
                'that with -H.',
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
