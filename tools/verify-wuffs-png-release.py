#!/usr/bin/env python3
"""Compare the actual pinned and concrete PNG release implementations with sanitizers."""
import argparse,datetime,hashlib,json,pathlib,subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--dependency-cache',required=True,type=pathlib.Path)
parser.add_argument('--derived',required=True,type=pathlib.Path)
parser.add_argument('--cc',default='gcc')
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
upstream=args.dependency_cache.resolve()/'wuffs-v0.3.c'
derived=args.derived.resolve()/'wuffs-png-gtos.c'
probe=repo/'apps/png_image_codec/wuffs_release_proof.c'
out=args.output.resolve()
assert not out.exists(),'Choose a fresh evidence directory'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(upstream)=='82c6741dd751eb962a287882991836498526eb02dbe0c7f19adc01810b8bac96'
adaptation=json.loads((args.derived/'wuffs-png-gtos.manifest.json').read_text())
assert adaptation['derived']['sha256']==sha(derived) and adaptation['pixel_specialization']
assert sha(derived)=='efdc0257e2512777e92ca4b4c350b738ba09b1b826749cbbf03c39d52efa4ae3'
out.mkdir(parents=True)
state=dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
           proof_source_sha256=sha(probe),upstream_sha256=sha(upstream),derived_sha256=sha(derived),
           passed=False,guest_pass=False,checks=[])
try:
    for name,source in (('upstream',upstream),('gtos-release',derived)):
        binary=out/(name+'.probe')
        command=[args.cc,'-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
                 '-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
                 '-Wl,--gc-sections','-DWUFFS_INPUT="'+str(source)+'"',str(probe),'-o',str(binary)]
        with (out/(name+'.compile.log')).open('wb') as log:
            compiled=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=240)
        assert compiled.returncode==0,name+' compilation failed'
        with (out/(name+'.output')).open('wb') as output,(out/(name+'.stderr')).open('wb') as error:
            ran=subprocess.run([str(binary)],stdout=output,stderr=error,timeout=240)
        assert ran.returncode==0 and not (out/(name+'.stderr')).read_bytes(),name+' proof failed'
        raw=(out/(name+'.output')).read_bytes()
        assert raw.decode().splitlines()[-1].startswith('CRC PASS 40965 rolling/segmentation cases; PIXEL 4290 differential cases; LAYOUT ')
        state['checks'].append(dict(profile=name,command=command,exit_code=ran.returncode,
                                    output_sha256=sha(out/(name+'.output')),summary=raw.decode().splitlines()[-1]))
    assert (out/'upstream.output').read_bytes()==(out/'gtos-release.output').read_bytes()
    state['passed']=True
finally:
    (out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
print('WUFFS PNG RELEASE DIFFERENTIAL PASS: 40965 CRC cases, 4290 pixel cases, ASan/UBSan clean')
