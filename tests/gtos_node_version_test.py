#!/usr/bin/env python3
"""Run the actual Node version action through regular and symlinked entries."""
import argparse
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view',required=True,type=Path)
parser.add_argument('--origin',required=True,type=Path)
parser.add_argument('--out',required=True,type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
node_dir = args.view/'third_party/node'
python = args.view/'third_party/cpython3/host/bin/python3'
binary = node_dir/'linux/node-linux-x64/bin/node'
sources = [node_dir/'check_version.py',node_dir/'node.py',node_dir/'check_version.js',
           node_dir/'update_node_binaries',args.origin/'third_party/node/check_version.py']
before = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
canonical = args.out/'origin'
private = args.out/'private'
canonical.mkdir()
private.mkdir()
for directory in [canonical,private]:
    shutil.copyfile(node_dir/'node.py',directory/'node.py')
shutil.copyfile(sources[-1],canonical/'upstream.py')
shutil.copyfile(node_dir/'check_version.py',canonical/'patched.py')
(private/'upstream.py').symlink_to(canonical/'upstream.py')
(private/'patched.py').symlink_to(canonical/'patched.py')
shutil.copyfile(node_dir/'check_version.py',private/'regular.py')
shutil.copyfile(node_dir/'check_version.js',private/'check_version.js')
tool = private/'linux/node-linux-x64/bin/node'
tool.parent.mkdir(parents=True)
tool.symlink_to(binary)
version = node_dir/'update_node_binaries'


def run(name,entry,expected=version,success=True):
    output = args.out/(name+'.result')
    result = subprocess.run([str(python),'-B',str(private/entry),
        '--expected_version_file',str(expected),'--out_file',str(output)],
        text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (args.out/(name+'.log')).write_text(result.stdout)
    assert (result.returncode==0)==success,result.stdout
    if success:
        assert output.read_text()=='OK'
    else:
        assert not output.exists()
    return result.stdout


failure = run('upstream','upstream.py',success=False)
assert str(canonical/'linux/node-linux-x64/bin/node') in failure,failure
print('PASS actual upstream entry follows the canonical helper and fails without a tool there')
run('private-symlink','patched.py')
print('PASS patched symlink entry imports the private helper and executes the actual pinned Node')
run('private-regular','regular.py')
print('PASS the ordinary entry retains the real version check')
wrong = args.out/'wrong-version'
text,count = re.subn(r'(?m)^NODE_VERSION="[^"]+"','NODE_VERSION="v999.0.0"',version.read_text())
assert count==1
wrong.write_text(text)
assert 'Failed NodeJS version check' in run('wrong-version','patched.py',wrong,False)
print('PASS a genuinely wrong version is rejected without a successful output file')
tool.unlink()
assert str(tool) in run('missing-tool','patched.py',success=False)
print('PASS a missing private tool is rejected without a host fallback')
tool.symlink_to(binary)
run('recovered','patched.py')
assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
print('PASS restoring the exact tool recovers and source bytes remain unchanged')
