"""Run the real TypeScript generator and observe its error propagation."""
import argparse
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--protoc', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(exist_ok=False)
plugin = args.view / 'tools/protoc_wrapper/protoc-gen-ts_proto.py'
sys.path.insert(0, str(args.view / 'third_party/node'))
import node
import node_modules

proto = args.out / 'sample.proto'
proto.write_text('syntax = "proto3";\nmessage GtosMessage { string text = 1; }\n')
result = subprocess.run(
    [str(args.protoc), '--plugin=protoc-gen-ts_proto=' + str(plugin),
     '--proto_path=' + str(args.out), '--ts_proto_out=' + str(args.out), str(proto)],
    capture_output=True, timeout=30)
assert result.returncode == 0, result.stderr
generated = (args.out / 'sample.ts').read_text()
assert 'export interface GtosMessage' in generated and 'text: string,' in generated

broken_request = b'\x0a\xff'
direct = subprocess.run([node.GetBinaryPath(), node_modules.PathToTsProto()],
                        input=broken_request, capture_output=True, timeout=30)
through_plugin = subprocess.run([str(plugin)], input=broken_request,
                                capture_output=True, timeout=30)
(args.out / 'node-error.log').write_bytes(direct.stderr)
(args.out / 'plugin-error.log').write_bytes(through_plugin.stderr)
assert direct.returncode > 0 and direct.stderr and not direct.stdout
assert through_plugin.returncode == direct.returncode, through_plugin.stderr
assert through_plugin.stderr == direct.stderr and not through_plugin.stdout
print('GTOS_TS_PROTO_PLUGIN_PASS generated=1 propagated_rc=%d' % direct.returncode)
