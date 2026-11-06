#!/usr/bin/env python3
"""Apply GTOS platform changes only to a previously composed private view."""
import argparse
import importlib.util
import json
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('candidate', HERE.parent / 'port-candidate-r20/candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)

INPUTS = {
    'build/toolchain/gtos/BUILD.gn': '167bac3e52fb475a55050346979c08b1619f06053942ec984653e4e57a05800b',
    'build/config/clang/BUILD.gn': 'e30467f05002af51f125dfe764294844228ceef5adc3454e261256f803120289',
    'third_party/dawn/src/utils/platform.h': '28c73c24cc45e33b52aedba8deb9be6743be61237feedfdcdffe97fea664f28c',
    'third_party/dawn/src/dawn/common/BUILD.gn': '5ddfe58be80ec142acdcfc5a0b2828a95b6d8562b23aaf798dabd4cc20eeb968',
    'third_party/dawn/src/dawn/common/DynamicLib.h': '9f2cc9ee99bcf18a2f4d5b6541f825f72fb9d270a3d179ac6cabd7b24d7dfae1',
    'third_party/dawn/src/dawn/common/DynamicLib.cpp': '6405a92700b8b4fd6cb97a9deb8a7b8e953a5f1af4336cb5e0a2269977066315',
    'third_party/dawn/src/dawn/common/SystemUtils.cpp': '7574fcea83b3521c1d2ae789cae47952925bfcf77417ab3de5f7cdf874c6f2c7',
    'third_party/dawn/src/dawn/common/SystemHandle.h': '6ec71a7796ebedeedf69d9b51828162a4cb014882068ab473473cc0feab08985',
    'third_party/dawn/src/dawn/common/SystemHandle.cpp': '36a1dfbe024309acdfd0311b46f60b6ff1c8b4254b1777158315a48084fa9774',
    'ui/menus/BUILD.gn': '4085f2e495bb1e385918a2e01b652b30651a550e6af4fe3d9d957ba3f30bb472',
    'ui/base/unowned_user_data/BUILD.gn': '4b68d447db5ca38c6220c30533d6b1db547a6cd43b0fb50cf971f5dbea389895',
    'third_party/nasm/nasm_assemble.gni': 'f19475e0317263e15b6c028732f2638d1e024046931fdb55434ec73f7e73992d',
    'tools/grit/grit_args.gni': 'e3e7b4cf51aea6caa044d59ddacf6b164c56eefe279e637e0712aa9fec318900',
    'tools/grit/grit/node/base.py': '7aa20d2fe8e428d1a29cf47db321a157771a173f2f92ff6c09d44c2968157f59',
}


def apply(view):
    view = view.resolve()
    if candidate.under_shared(view) or not (view / '.gn').is_file():
        raise SystemExit('expected an existing private composed GN view: ' + str(view))
    receipt = view / 'gtos-platform.json'
    if receipt.exists():
        raise SystemExit('refusing to overwrite this platform generation: ' + str(receipt))
    for rel, expected in INPUTS.items():
        if candidate.digest(view / rel) != expected:
            raise SystemExit('platform input changed: ' + rel)
    applied = []
    for rel, expected in INPUTS.items():
        parent = candidate.materialize_parents(view, rel)
        if view not in parent.resolve().parents:
            raise SystemExit('platform parent escaped the private view: ' + str(parent))
        target = parent / Path(rel).name
        if target.is_symlink():
            origin = target.resolve()
            target.unlink()
            shutil.copyfile(origin, target)
        patch = HERE / 'patches' / candidate.patch_name(rel)
        subprocess.run(['/usr/bin/patch', '--batch', '--fuzz=0', '--input', str(patch), str(target)], check=True)
        applied.append({'path': rel, 'origin_sha256': expected,
                        'patch_sha256': candidate.digest(patch), 'sha256': candidate.digest(target)})
    with receipt.open('x') as handle:
        json.dump({'files': applied}, handle, indent=2)
        handle.write('\n')
    print(json.dumps({'view': str(view), 'applied': applied}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('view', type=Path)
    apply(parser.parse_args().view)
