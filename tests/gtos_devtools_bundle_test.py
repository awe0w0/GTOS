#!/usr/bin/env python3
"""Run the real DevTools bundler against generated imports in a symlink view."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--origin', required=True, type=Path)
parser.add_argument('--node', required=True)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
devtools = args.view / 'third_party/devtools-frontend/src'
origin = args.origin / 'third_party/devtools-frontend/src'
script = devtools / 'scripts/build/esbuild.js'
environment = dict(os.environ, NODE_OPTIONS='--preserve-symlinks --preserve-symlinks-main')


def invoke(name, program, source, expected=True):
    output = args.out / (name + '.mjs')
    command = [args.node, str(program), str(source), str(output), '--bundleAll',
               '--rootDir', str(args.out / 'view'), '--rootGenDir', str(args.out / 'gen'),
               '--depfile', str(output) + '.d']
    result = subprocess.run(command, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == expected, result.stdout
    return output, result.stdout


def value(output):
    return subprocess.check_output([
        args.node, '--input-type=module', '-e',
        'import {pathToFileURL} from "node:url"; '
        'console.log((await import(pathToFileURL(process.argv[1]))).answer);', str(output)],
        env=environment, text=True).strip()


for name in ['view', 'origin', 'gen']:
    (args.out / name / 'front_end/sample').mkdir(parents=True)
relative = Path('front_end/sample/main.ts')
source = args.out / 'origin' / relative
source.write_text('import {number} from "./generated.js"; export const answer = number + 1;\n')
linked = args.out / 'view' / relative
linked.symlink_to(source)
generated = args.out / 'gen/front_end/sample/generated.js'
generated.write_text('export const number = 41;\n')
before = hashlib.sha256(source.read_bytes()).hexdigest()

# Execute the unmodified upstream script as a negative calibration. Its real
# plugin and path helper are copied unchanged, with the same pinned esbuild.
legacy = args.out / 'legacy'
for rel in ['scripts/build/esbuild.js', 'scripts/build/devtools_plugin.js', 'scripts/devtools_paths.js']:
    destination = legacy / rel
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(origin / rel, destination)
assert hashlib.sha256((legacy / 'scripts/build/esbuild.js').read_bytes()).hexdigest() == \
    '52d417868256adbe0f95558d591b641f0d07e34e162d01451ae6dbaf84542a8f'
(legacy / 'package.json').write_text(json.dumps({'type': 'module'}))
(legacy / 'node_modules').symlink_to(devtools / 'node_modules', target_is_directory=True)
(legacy / 'third_party/esbuild').mkdir(parents=True)
(legacy / 'third_party/esbuild/esbuild').symlink_to(devtools / 'third_party/esbuild/esbuild')
_, log = invoke('legacy-calibration', legacy / 'scripts/build/esbuild.js', linked, False)
assert 'Cannot read file' in log and 'generated.js' in log, log
print('PASS original bundler reproduces the generated-file lookup failure')

output, _ = invoke('linked', script, linked)
assert value(output) == '42'
deps = Path(str(output) + '.d').read_text()
assert 'view/front_end/sample/main.ts' in deps and 'gen/front_end/sample/generated.js' in deps, deps
assert 'origin/front_end/sample' not in deps, deps
print('PASS patched bundler resolves generated imports and records logical view dependencies')

# A real file beside the canonical source must not override the logical view.
(source.parent / 'generated.js').write_text('export const number = 900;\n')
output, _ = invoke('canonical-decoy', script, linked)
assert value(output) == '42'
print('PASS canonical source neighbour cannot replace the generated module')

regular = args.out / 'view/front_end/sample/regular.ts'
regular.write_text(source.read_text())
output, _ = invoke('regular', script, regular)
assert value(output) == '42'
print('PASS ordinary non-symlink source retains its behaviour')

generated.unlink()
_, log = invoke('missing-generated', script, linked, False)
assert 'generated.js' in log
print('PASS missing generated input remains a real build failure')
generated.write_text('export const number = ;\n')
_, log = invoke('invalid-generated', script, linked, False)
assert 'Unexpected' in log
assert hashlib.sha256(source.read_bytes()).hexdigest() == before
print('PASS malformed generated input is refused and source bytes remain unchanged')
