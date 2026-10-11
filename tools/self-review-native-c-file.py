#!/usr/bin/env python3
"""Review executed regular-file providers, record rejection and actual kernel ISA controls."""
import argparse,hashlib,importlib.util,json,pathlib,re,subprocess,sys,traceback
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('build',type=pathlib.Path);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1];base=repo/'apps/native_c_file';out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
observer=load('c_file_review_observer',base/'probe/observer.py');audit=load('c_file_review_audit',repo/'tools/audit-kernel-instructions.py')
state={'all_required_checks_pass':False,'observer_positive_cases':0,'observer_negative_controls':0,'prohibited_isa_controls':0,'public_provider_counts':{},'maximum_static_frames':{},'commands':[],'self_sha256':sha(__file__)}
def run(cmd,log,expected=0):
    cmd=list(map(str,cmd))
    with log.open('wb') as stream:code=subprocess.run(cmd,cwd=repo,stdout=stream,stderr=subprocess.STDOUT,timeout=180).returncode
    state['commands'].append({'argv':cmd,'exit_code':code,'log':str(log),'log_sha256':sha(log)})
    if code!=expected:raise RuntimeError(str(log)+'\n'+log.read_text(errors='replace')[-4000:])
    return log.read_text()
try:
    build=a.build.resolve();status=json.loads((build/'status.json').read_text())
    assert status['all_required_checks_pass'] and status['native_c_file_guest_pass'] and len(status['guests'])==2
    assert all(sha(path)==h for path,h in status['inputs'].items()),'Qualification input changed'
    profile=json.loads((repo/'apps/native_files/source-lock.json').read_text())['profiles']['qualified-llvm']
    clock=json.loads((repo/'apps/native_c_clock/source-lock.json').read_text())
    assert status['tools']['cxx']['sha256']==profile['cxx_sha256']==clock['memory_cxx_sha256']
    assert status['tools']['clang']['sha256']==profile['clang_sha256']==clock['clang_sha256']
    manifest=json.loads((base/'upstream-lock.json').read_text())
    for guest in status['guests']:
        opt=guest['opt'];level='O'+str(opt);d=build/level;path=d/'guest/guest.log'
        assert sha(path)==guest['guest_log_sha256'];text=path.read_text()
        plans={mode:observer.geometry(d/('mode'+str(mode))/'user.stripped.elf',mode) for mode in range(6)}
        assert observer.verify(text,plans,guest['memory_mib'])==guest['evidence']
        state['observer_positive_cases']+=1;state['observer_negative_controls']+=observer.negative_controls(text,plans,guest['memory_mib'])
        for name,h in status['objects'].items():
            if name.startswith(level+'-kernel-') and not name.endswith('-probe.o'):
                old=level+'-'+name.split('-kernel-',1)[1]
                assert h==profile['objects'][old],'Previously qualified actual core object changed '+name
        names=run(['nm','-g','--defined-only',d/'mode0/user.elf'],out/(level+'-public.log'))
        symbols={line.split()[-1] for line in names.splitlines() if line.split()}
        assert set(manifest['providers'])<=symbols,'Linked public provider missing'
        state['public_provider_counts'][level]=len(manifest['providers'])
        frames=[]
        for f in list((d/'library').glob('*.su'))+list(d.glob('mode*/*.su')):
            for line in f.read_text().splitlines():
                fields=line.split('\t');assert len(fields)>=3 and fields[2] in ['static','dynamic,bounded'],'Unknown user frame '+line
                frames.append(int(fields[1]))
        assert frames and max(frames)<8176,'User static frame exceeds fixed usable stack'
        state['maximum_static_frames'][level]=max(frames)
    source=(repo/'src/process/native_files.cpp').read_text();anchor='    InterruptGuard guard;statistics.calls++;';assert source.count(anchor)==1
    for opt in [0,2]:
        level='O'+str(opt);d=build/level
        original=next(c['argv'] for c in status['commands'] if '-c' in c['argv'] and c['argv'][c['argv'].index('-c')+1].endswith('/src/process/native_files.cpp') and '-O'+str(opt) in c['argv'])
        objects=list(sorted(d.glob('*.o')));assert any(x.name=='native_files.o' for x in objects)
        for name,instruction in [('x87','fldz'),('sse','pxor %xmm0,%xmm0'),('mmx','pxor %mm0,%mm0'),('avx','vpxor %xmm0,%xmm0,%xmm0'),('mxcsr','stmxcsr -4(%esp)')]:
            control=out/(level+'-'+name);control.mkdir();src=control/'native_files.cpp'
            src.write_text(source.replace(anchor,anchor+'\n    asm volatile("'+instruction+'");'))
            command=original[:];command[command.index('-c')+1]=str(src);command[command.index('-o')+1]=str(control/'native_files.o');command[command.index('-MF')+1]=str(control/'native_files.d')
            run(command,control/'compile.log')
            linked=[control/'native_files.o' if x.name=='native_files.o' else x for x in objects]
            run(['ld','-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',control/'kernel.map','-o',control/'kernel.bin',*linked],control/'link.log')
            result=run([sys.executable,'tools/audit-kernel-instructions.py','--map',control/'kernel.map','--source-root',repo,control/'kernel.bin'],control/'audit.log',1)
            rejected=re.findall(r'(0x[0-9a-f]+): prohibited FP/SIMD: ([^\n]+)',result)
            assert len(rejected)==1 and rejected[0][1].split()[0]==instruction.split()[0]
            symbol=audit.Elf(control/'kernel.bin').symbols['_ZN4gtos7process11NativeFiles4CallEjRNS_6memory19ProcessAddressSpaceEjjj']
            assert symbol['type']==2 and symbol['size'] and symbol['value']<=int(rejected[0][0],16)<symbol['value']+symbol['size']
            state['prohibited_isa_controls']+=1
            print('PASS C FILE ACTUAL SERVICE ISA REJECTION '+level+' '+name,flush=True)
    state.update(all_required_checks_pass=True,build_status_sha256=sha(build/'status.json'),existing_core_objects_unchanged=True)
except BaseException as error:
    state['failure']=str(error);(out/'exception.log').write_text(traceback.format_exc())
(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps({k:v for k,v in state.items() if k!='commands'},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
