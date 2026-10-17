#!/usr/bin/python3 -IBS
"""Dispatch exactly one reviewed GN helper under the inherited restrictions."""
import contextlib
import ctypes
import hashlib
import io
import importlib.util
import json
import os
import pathlib
import runpy
import sys

W=pathlib.Path('/mnt/f/GTOS-Chromium')
P=W/'artifacts/native-rust-gtos-20261015-a'
OUT=W/'artifacts/native-pma-20261017-a/gn-04'
UPSTREAM=W/'sources/chromium-full/src'
PROOF=P/'gn-timestamp-inputs-01.json'
SCRIPT=UPSTREAM/'build/compute_build_timestamp.py'

def require(ok,why):
    if not ok: raise RuntimeError(why)

def sha(path):
    value=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): value.update(block)
    return value.hexdigest()

require(sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated Python required')
require(len(sys.argv)>=2,'Missing exact GN script')
requested=pathlib.Path(sys.argv[1]).resolve(strict=True)
branding=requested==UPSTREAM/'build/util/version.py'
chrome_version=branding and sys.argv[2:]==[
    '-f','../source/chrome/VERSION','-t',
    'full = "@MAJOR@.@MINOR@.@BUILD@.@PATCH@" major = "@MAJOR@" minor = "@MINOR@" build = "@BUILD@" patch = "@PATCH@" ',
    '--os','gtos']
require(branding or (len(sys.argv)==3 and requested==SCRIPT and sys.argv[2]=='default'),
        'Unreviewed GN script or arguments')
guard=json.loads((OUT/'guard.json').read_text())
require(guard['landlock_applied'] and guard['network_disabled'] and guard['workspace_read_only']
        and guard['source_write_denied'] and not guard['directory_execute_permissions']
        and guard['writable_output']==str(OUT)
        and guard['executable_files'].get(str(pathlib.Path(__file__).resolve()))==sha(__file__),'Missing exact script/guard authorization')
require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required')
if branding:
    if chrome_version:
        PROOF=P/'gn-chrome-version-inputs-01.json'
        expected_proof='bfc551816002c80c24d4769ecfc9ef96411ab451ce76823a4c6fff9b405e2982'
        expected_callsite='//chrome/version.gni'
        receipt_name='chrome-version-calls.jsonl'
    else:
        PROOF=P/'gn-branding-inputs-01.json'
        expected_proof='cd941bb6f1e1d6caaee854f6b3a5b7f7361ab3c0f334fd655ceb7f359cbc8450'
        expected_callsite='//build/util/branding.gni'
        receipt_name='branding-calls.jsonl'
    require(sha(PROOF)==expected_proof,'Version/branding proof changed')
    proof=json.loads(PROOF.read_text())
    require(proof['revision']=='04bca2584cddb39e134fc7c16bb2886e02863d0e'
            and len(proof['files'])==7 and proof['allowed_callsite']==expected_callsite
            and proof['allowed_script']=='build/util/version.py'
            and sys.argv[2:]==proof['allowed_arguments']
            and pathlib.Path.cwd()==OUT/'build','Unreviewed branding call/options/cwd')
    bound={UPSTREAM/relative:item['sha256'] for relative,item in proof['files'].items()}
    require(all(sha(path)==digest for path,digest in bound.items()),'Branding input changed')
    require('CHROME_BUILD_TYPE' not in os.environ,'Unexpected official build environment')
    module_path=UPSTREAM/'build/util/android_chrome_version.py'
    spec=importlib.util.spec_from_file_location('android_chrome_version',str(module_path))
    module=importlib.util.module_from_spec(spec)
    sys.modules['android_chrome_version']=module
    spec.loader.exec_module(module)
    require(pathlib.Path(module.__file__).resolve()==module_path,'Unexpected auxiliary module')
    sys.argv=[str(requested)]+proof['allowed_arguments']
    capture=io.StringIO();result=None
    with contextlib.redirect_stdout(capture):
        try: runpy.run_path(str(requested),run_name='__main__')
        except SystemExit as error: result=error.code
    output=capture.getvalue()
    require(result==0 and output==proof['expected_output']
            and all(sha(path)==digest for path,digest in bound.items()),'Branding helper/output/final inputs failed')
    receipt={'script':str(requested),'arguments':proof['allowed_arguments'],'output':output,
             'script_sha256':sha(requested),'dispatcher_sha256':sha(__file__),
             'proof_sha256':sha(PROOF),'bound_inputs_unchanged':True,'returncode':result}
    with (OUT/receipt_name).open('a') as stream: stream.write(json.dumps(receipt)+'\n')
    sys.stdout.write(output)
    sys.exit(0)
require(sha(PROOF)=='439241db2103d6f9a3919cf40e6d041327233cb3f2829d3fae8bdf1b2330fae6','Timestamp proof changed')
proof=json.loads(PROOF.read_text())
require(proof['revision']=='04bca2584cddb39e134fc7c16bb2886e02863d0e'
        and len(proof['files'])==5 and proof['allowed_script']=='build/compute_build_timestamp.py'
        and proof['allowed_arguments']==['default'],'Timestamp proof scope changed')
bound={UPSTREAM/relative:item['sha256'] for relative,item in proof['files'].items()}
bound[UPSTREAM/proof['generated_commit_time']['relative']]=proof['generated_commit_time']['sha256']
require(all(sha(path)==digest for path,digest in bound.items()),'Timestamp input changed')
require((UPSTREAM/'build/util/LASTCHANGE.committime').read_text().strip()=='1791281250','Locked commit time changed')
sys.argv=[str(SCRIPT),'default']
capture=io.StringIO()
result=None
with contextlib.redirect_stdout(capture):
    try:
        runpy.run_path(str(SCRIPT),run_name='__main__')
    except SystemExit as error:
        result=error.code
require(result==0 and all(sha(path)==digest for path,digest in bound.items()),'Timestamp helper or final inputs failed')
output=capture.getvalue()
require(output.endswith('\n') and output[:-1].isdigit(),'Unexpected timestamp output')
receipt={'script':str(SCRIPT),'arguments':['default'],'timestamp':int(output),
         'script_sha256':sha(SCRIPT),'dispatcher_sha256':sha(__file__),
         'proof_sha256':sha(PROOF),'bound_inputs_unchanged':True,'returncode':result}
with (OUT/'timestamp-calls.jsonl').open('a') as stream: stream.write(json.dumps(receipt)+'\n')
sys.stdout.write(output)
