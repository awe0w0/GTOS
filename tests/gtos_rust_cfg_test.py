#!/usr/bin/env python3
"""Run the real patched cfg action with the pinned compiler and target JSON."""
import argparse
import hashlib
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--spec', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
python = args.view / 'third_party/cpython3/host/bin/python3'
script = args.view / 'build/rust/gni_impl/rustc_print_cfg.py'
rust = args.view / 'third_party/rust-toolchain/bin'
before = hashlib.sha256(args.spec.read_bytes()).hexdigest()


def run(name, target, spec=None, success=True):
    output = args.out / (name + '.cfg')
    command = [str(python), '-B', str(script), '--rust-prefix', str(rust),
               '--target', target, '--output-path', str(output)]
    if spec is not None:
        command += ['--target-spec', str(spec)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == success, result.stdout
    return set(output.read_text().splitlines()) if success else result.stdout


assert 'could not find specification' in run('old-triple', 'i686-unknown-gtos', success=False)
print('PASS the unmapped built-in triple is refused by the actual compiler')
cfg = run('gtos', 'i686-unknown-gtos', args.spec)
for required in ['target_arch="x86"', 'target_pointer_width="32"', 'target_os="gtos"',
                 'panic="abort"', 'target_feature="sse2"', 'target_has_atomic="64"']:
    assert required in cfg, (required, cfg)
assert 'unix' not in cfg and 'target_os="linux"' not in cfg and 'target_feature="avx"' not in cfg
print('PASS the custom GTOS IA32/SSE2 target is read without a host OS identity')
assert 'target_os="linux"' in run('host', 'x86_64-unknown-linux-gnu')
print('PASS the ordinary host target retains its original invocation')
bad = args.out / 'broken.json'
bad.write_text('{ malformed target specification')
assert 'error loading target specification' in run('broken', 'i686-unknown-gtos', bad, False)
print('PASS a malformed custom target fails the production action')
missing = args.out / 'missing.json'
diagnostic = run('missing', 'i686-unknown-gtos', missing, False)
assert 'target file "' + str(missing) + '" does not exist' in diagnostic, diagnostic
assert hashlib.sha256(args.spec.read_bytes()).hexdigest() == before
print('PASS a missing custom target fails and the source specification is unchanged')
