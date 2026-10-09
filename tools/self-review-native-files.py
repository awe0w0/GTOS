#!/usr/bin/env python3
"""Self-review real native-file evidence and verify ISA/observer rejection."""
import argparse,hashlib,importlib.util,json,pathlib,re,subprocess,sys,traceback
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('builds',nargs='+',type=pathlib.Path);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
spec=importlib.util.spec_from_file_location('native_file_observer',repo/'apps/native_files/probe/observer.py');observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)
spec=importlib.util.spec_from_file_location('kernel_isa_audit',repo/'tools/audit-kernel-instructions.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
state={'all_required_checks_pass':False,'observer_positive_cases':0,'observer_negative_controls':0,'prohibited_isa_controls':0,'builds':[],'commands':[]}
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def run(cmd,log,expected=0):
 cmd=list(map(str,cmd))
 with log.open('wb') as f:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
 state['commands'].append({'argv':cmd,'exit_code':code,'log':str(log),'log_sha256':sha(log)})
 if code!=expected:raise RuntimeError(str(log)+'\n'+log.read_text(errors='replace')[-4000:])
 return log.read_text()
try:
 for build in a.builds:
  build=build.resolve();status=json.loads((build/'status.json').read_text());assert status['all_required_checks_pass'] and status['native_file_abi_guest_pass'] and not status['build_only']
  assert all(sha(path)==h for path,h in status['inputs'].items()),'Qualification input changed'
  for guest in status['guests']:
   level='O'+str(guest['opt']);phase=guest['phase'];path=build/level/phase/'guest.log';text=path.read_text();assert sha(path)==guest['guest_log_sha256']
   plans={mode:observer.geometry(build/level/('mode'+str(mode))/'user.stripped.elf') for mode in range(7)}
   result=observer.verify(text,phase,plans);assert result==guest['evidence'];state['observer_positive_cases']+=1
   controls=[];lines=text.splitlines()
   for i in range(len(lines)):
    controls.append(('delete-'+str(i),'\n'.join(lines[:i]+lines[i+1:])+'\n'));controls.append(('duplicate-'+str(i),'\n'.join(lines[:i]+[lines[i]]+lines[i:])+'\n'))
   for i,line in enumerate(lines):
    if not line.startswith('FILE '):continue
    for m in re.finditer(r'(\w+)=([0-9A-F]{8})',line):
     replacement='00000001' if m.group(2)=='00000000' else '00000000';changed=line[:m.start(2)]+replacement+line[m.end(2):]
     controls.append(('corrupt-'+str(i)+'-'+m.group(1),'\n'.join(lines[:i]+[changed]+lines[i+1:])+'\n'))
    controls.append(('extra-field-'+str(i),'\n'.join(lines[:i]+[line+' extra=00000001']+lines[i+1:])+'\n'))
   controls.extend([('unexpected-tail',text+'EXTRA\n'),('forged-pass','NATIVE FILE GUEST PASS\n'),('failure',text.replace('NATIVE FILE GUEST PASS','NATIVE FILE GUEST FAIL'))])
   for name,corrupted in controls:
    try:observer.verify(corrupted,phase,plans)
    except (AssertionError,IndexError,KeyError,ValueError):state['observer_negative_controls']+=1
    else:raise AssertionError('Observer accepted '+status['profile']+' '+level+' '+phase+' '+name)
  state['builds'].append({'profile':status['profile'],'status_sha256':sha(build/'status.json'),'guest_count':len(status['guests'])})
 # Mutate the actual new service, link into the real kernel, never execute.
 build=a.builds[0].resolve();status=json.loads((build/'status.json').read_text())
 source=(repo/'src/process/native_files.cpp').read_text();anchor='    InterruptGuard guard;statistics.calls++;';assert source.count(anchor)==1
 for opt in [0,2]:
  level='O'+str(opt)
  original=next(c['argv'] for c in status['commands'] if '-c' in c['argv'] and c['argv'][c['argv'].index('-c')+1].endswith('/src/process/native_files.cpp') and '-O'+str(opt) in c['argv'])
  objects=list(sorted((build/level/'kernel').glob('*.o')));assert any(x.name=='native_files.o' for x in objects)
  for name,instruction in [('x87','fldz'),('sse','pxor %xmm0,%xmm0'),('mmx','pxor %mm0,%mm0'),('avx','vpxor %xmm0,%xmm0,%xmm0'),('mxcsr','stmxcsr -4(%esp)')]:
   d=out/(level+'-'+name);d.mkdir();src=d/'native_files.cpp';src.write_text(source.replace(anchor,anchor+'\n    asm volatile("'+instruction+'");'))
   command=original[:];command[command.index('-c')+1]=str(src);command[command.index('-o')+1]=str(d/'native_files.o');command[command.index('-MF')+1]=str(d/'native_files.d')
   run(command,d/'compile.log')
   control_objects=[d/'native_files.o' if x.name=='native_files.o' else x for x in objects]
   run(['ld','-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*control_objects,build/level/'kernel.o'],d/'link.log')
   result=run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'audit.log',1)
   rejected=re.findall(r'(0x[0-9a-f]+): prohibited FP/SIMD: ([^\n]+)',result)
   assert len(rejected)==1 and rejected[0][1].split()[0]==instruction.split()[0],'ISA control did not reject injected instruction'
   symbol=audit.Elf(d/'kernel.bin').symbols['_ZN4gtos7process11NativeFiles4CallEjRNS_6memory19ProcessAddressSpaceEjjj']
   assert symbol['type']==2 and symbol['size'] and symbol['value']<=int(rejected[0][0],16)<symbol['value']+symbol['size'],'ISA rejection outside actual service'
   state['prohibited_isa_controls']+=1
   print('PASS NATIVE FILE ISA REJECTION '+level+' '+name,flush=True)
 state['all_required_checks_pass']=True
except BaseException as e:state['failure']=str(e);(out/'exception.log').write_text(traceback.format_exc())
(out/'status.json').write_text(json.dumps(state,indent=2)+'\n');print(json.dumps({k:v for k,v in state.items() if k!='commands'},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
