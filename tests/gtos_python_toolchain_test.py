#!/usr/bin/env python3
"""Exercise the private view's actual pinned interpreter through GN and Ninja."""
import argparse
import json
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--gn', required=True)
parser.add_argument('--ninja', required=True)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)


def run(name, command, success=True):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == success, result.stdout
    return result.stdout


line = re.search(r'^script_executable\s*=\s*"(//[^"]+)"', (args.view / '.gn').read_text(), re.M)
assert line, 'the view must select the checked-out interpreter'
relative = line.group(1)[2:]
python = args.view / relative
assert run('version', [str(python), '--version']).strip() == 'Python 3.11.9'
module = args.view / 'build/modules/unified/modulemap_config.py'
probe = ('import importlib.util; s=importlib.util.spec_from_file_location("modulemap_config",' +
         repr(str(module)) + '); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); '
         'print("HEADER_IMPORT_OK",m.Header.__name__)')
assert 'HEADER_IMPORT_OK' in run('real-module', [str(python), '-B', '-c', probe])
assert "'type' object is not subscriptable" in run(
    'old-interpreter', ['/usr/bin/python3.8', '-B', '-c', probe], False)
print('PASS pinned Python imports the real module; Python 3.8 reproduces the failure')

root = args.out / 'fixture'
root.mkdir()
(root / Path(relative).parent).mkdir(parents=True)
(root / relative).symlink_to(python)
(root / '.gn').write_text('buildconfig = "//config.gn"\n' + line.group(0) + '\n')
(root / 'config.gn').write_text('set_default_toolchain("//:tools")\n')
(root / 'BUILD.gn').write_text('''toolchain("tools") {
  tool("stamp") { command = "touch {{output}}" }
}
action("sample") {
  script = "sample.py"
  outputs = [ "$root_build_dir/value.txt" ]
  args = [ rebase_path(outputs[0], root_build_dir) ]
}
group("default") { deps = [ ":sample" ] }
''')
sample = root / 'sample.py'
sample.write_text('import json,pathlib,sys\nvalues: list[str] = ["modern"]\n'
                  'pathlib.Path(sys.argv[1]).write_text(json.dumps({"value":values[0],'
                  '"executable":sys.executable,"version":list(sys.version_info[:3])}))\n')
run('gen', [args.gn, 'gen', str(root / 'out'), '--root=' + str(root)])
command = [args.ninja, '-C', str(root / 'out'), 'value.txt']
run('build', command)
observed = json.loads((root / 'out/value.txt').read_text())
assert observed['value'] == 'modern' and observed['version'] == [3, 11, 9], observed
assert Path(observed['executable']).resolve() == python.resolve(), observed
print('PASS GN and Ninja run the configured interpreter and produce the expected output')
assert 'no work to do' in run('unchanged', command)
print('PASS an unchanged action does not rebuild')
sample.write_text('raise RuntimeError("deliberate action failure")\n')
assert 'deliberate action failure' in run('failed-action', command, False)
print('PASS a Python action failure propagates through Ninja')
