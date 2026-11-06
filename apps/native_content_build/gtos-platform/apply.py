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
