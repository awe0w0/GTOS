import argparse,ast,datetime,hashlib,json,os,pathlib,re,shutil,struct,subprocess,sys,time,traceback
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('--locate',action='store_true');p.add_argument('--build-only',action='store_true')
for name in ['clang','libcxx-source','memory-cxx','integer-builtins']:p.add_argument('--'+name,required=True,type=pathlib.Path)
a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1];ws=repo.parent.parent;base=repo/'apps/native_c_clock'
legacy=repo/'apps/native_v8_runtime';src=legacy/'include';runtime=base/'runtime'
profile=json.loads((base/'source-lock.json').read_text())
out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
state=dict(scope='Real C clock_gettime, pinned LLVM TLS errno and actual Highway Start in production CPL3',all_required_checks_pass=False,native_c_clock_guest_pass=False,native_isolate_pass=False,production_image_changed=False,capacity_changed=False,commands=[],guests=[],inputs={})
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def emit(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def bind(p):
 p=pathlib.Path(p).absolute();h=sha(p)
 if str(p) in state['inputs']:assert state['inputs'][str(p)]==h,str(p)
 else:
  state['inputs'][str(p)]=h
  try:rel=p.relative_to(ws)
  except ValueError:rel=pathlib.Path('external')/hashlib.sha256(str(p).encode()).hexdigest()/p.name
  dest=out/'source-snapshot'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
def save():emit(out/'status.json',state)
def run(cmd,log,expected=0,timeout=180):
 cmd=[str(x) for x in cmd];start=time.monotonic()
 with log.open('wb') as f:
  try:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
  except subprocess.TimeoutExpired:code=124
 state['commands'].append(dict(argv=cmd,exit_code=code,seconds=time.monotonic()-start,log=str(log),log_sha256=sha(log)));save()
 if code!=expected:
  detail=log.read_text(errors='replace')[-6000:];guest=log.parent/'guest.log'
  if guest.is_file():detail+='\n'+guest.read_text(errors='replace')[-6000:]
  raise RuntimeError(str(log)+' exit='+str(code)+'\n'+detail)
 return log.read_text()
def dependencies(dep):
 for x in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
  path=pathlib.Path(x);bind(path if path.is_absolute() else repo/path)
def nums(line):return {k:int(v,16) for k,v in re.findall(r'(\w+)=([0-9A-F]+)',line)}
def rtc(line):
 words=[int(v,16) for v in line.split('bytes=',1)[1].split()];assert len(words)==10
 sec,minute,hour,day,month,year,century,fmt,divider,valid=words
 assert valid&128 and divider&0xf0==0x20 and not fmt&0x81
 def number(v):
  if fmt&4:return v
  assert v&15<=9 and v>>4<=9
  return (v>>4)*10+(v&15)
 h=number(hour&127)
 if fmt&2:assert not hour&128;hour=h
 else:assert 1<=h<=12;hour=h%12+(12 if hour&128 else 0)
 return int(datetime.datetime(number(century)*100+number(year),number(month),number(day),hour,number(minute),number(sec),tzinfo=datetime.timezone.utc).timestamp())*1000000
def observed(log,unsupported):
 known='UNHANDLED INTERRUPT 0x00000027';irq7=log.count(known);log=log.replace(known,'')
 assert '\nNATIVE C CLOCK SMOKE PASS\n' in log and 'FAILED' not in log and 'FAIL\n' not in log
 assert 'UNHANDLED INTERRUPT' not in log and 'PANIC' not in log
 before=rtc(next(x for x in log.splitlines() if x.startswith('UTC RTC before ')))
 after=rtc(next(x for x in log.splitlines() if x.startswith('UTC RTC after ')))
 assert before<=after
 cases=[];reaps=[];records=[];events=[];final=None;fp_prev=0
 for line in log.splitlines():
  if line.startswith('UTC RECORD words='):
   words=[int(x,16) for x in line.split('words=',1)[1].split()];assert len(words)==64
   records.append(words);events=[]
  elif line.startswith('UTC EVENT '):events.append(nums(line))
  elif line.startswith('UTC CASE '):
   c=nums(line);w=records[-1];data=struct.pack('<64I',*w)
   assert w[0]==1 and w[1]==c['mode'] and w[2]==2 and w[3]==0 and w[4]>=8192 and w[5]==42
   assert w[6]==0x80000000 and w[7]>0 and w[8]==5 and w[9]==9
   assert w[12]==0x80002000 and w[13]>0 and w[14]==65536 and w[15]==8176
   assert w[12]<=w[10]<=w[12]+65536-4 and w[11]==(9942 if unsupported else 0x5522)
   assert c['cs']==0x23 and c['cr3']>=4096 and c['cr3']!=c['kernel_cr3'] and c['cr3']&4095==0
   assert c['cost']>0 and c['dynamic']==19 and c['utc']==2 and c['mono']>=5 and c['errors']==(2 if unsupported else 0)
   assert len(events)==5 and [e['index'] for e in events]==list(range(5))
   assert [e['kind'] for e in events]==[1,2,3,1,2]
   for i,e in enumerate(events):
    kind,status,sec,nsec,err,reserved=struct.unpack_from('<IIqqII',data,64+i*32)
    assert kind==e['kind'] and reserved==0
    mono=e['ticks']*11931*1000000//1193182
    assert e['mono']==mono
    if kind==2 and unsupported:
     assert e['error']==0xffffffda and status==0xffffffff and err==9942 and (sec,nsec)==(51,52) and e['value']==0
    else:
     assert e['error']==status==0 and err==0x5522
     if kind==2:assert before<=e['value']-mono<=after
     else:assert e['value']==mono
     if kind==3:assert (sec,nsec)==(e['value']*1000,0)
     else:assert (sec,nsec)==(e['value']//1000000,e['value']%1000000*1000)
   assert events[3]['value']>events[0]['value']
   if c['mode']==1:assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==6 and c['cr2']==0xbfffcffc
   else:assert c['exit']==(73 if c['mode']==2 else 0) and c['vector']==c['pf']==c['cr2']==0
   c['events']=events;cases.append(c)
  elif line.startswith('UTC REAP '):
   r=nums(line);assert r['free']==r['expected'] and r['peers']==1
   assert r['fp_initialized']==r['fp_invalidated']+1 and r['fp_invalidated']==fp_prev+1
   assert r['fp_saves']>0 and r['fp_restores']>0 and r['fp_failures']==0;fp_prev=r['fp_invalidated'];reaps.append(r)
  elif line.startswith('UTC FINAL '):
   final=nums(line);assert final['free']==final['expected'] and final['fp_initialized']==final['fp_invalidated']==fp_prev+1 and final['fp_failures']==0
 assert [c['mode'] for c in cases]==([3] if unsupported else [0,1,2,0])
 assert len(cases)==len(records)==len(reaps) and final
 assert len({c['id'] for c in cases})==len(cases)
 assert final['anchor']==(0 if unsupported else events[1]['value']-events[1]['mono'])
 return dict(cases=cases,reaps=reaps,final=final,irq7_exact_known_tokens=irq7,exact_reap_checks=len(reaps)+1)
def loaded_sha(p):
 data=pathlib.Path(p).read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01'
 h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);assert h[0]==2 and h[1]==3 and h[2]==1 and h[8]==32
 digest=hashlib.sha256();digest.update(data[:16]);digest.update(struct.pack('<HHII',*h[:4]))
 for i in range(h[9]):
  ph=data[h[4]+i*32:h[4]+(i+1)*32];fields=struct.unpack('<8I',ph);digest.update(ph)
  if fields[0]==1:digest.update(data[fields[1]:fields[1]+fields[4]])
 return digest.hexdigest()

try:
 bind(pathlib.Path(__file__));bind(base/'source-lock.json')
 for relative,h in profile['files'].items():
  path=base/relative;assert sha(path)==h,'Locked module input changed '+relative;bind(path)
 for relative,h in profile['legacy_dependencies'].items():
  path=legacy/relative;assert sha(path)==h,'Locked companion header changed '+relative;bind(path)
 clang=a.clang.absolute();cxx=a.memory_cxx.absolute();libcxx=a.libcxx_source.absolute()
 assert sha(clang)==profile['clang_sha256'] and sha(cxx)==profile['memory_cxx_sha256']
 assert subprocess.check_output(['git','-C',libcxx,'rev-parse','HEAD'],text=True).strip()==profile['libcxx_revision']
 bins={n:pathlib.Path(shutil.which(n)).absolute() for n in ('as','ld','nm','readelf','objdump','objcopy','grub-mkrescue','qemu-system-i386')}
 state['tools']={n:dict(path=str(v),sha256=sha(v)) for n,v in dict(bins,clang=clang,cxx=cxx).items()}
 flags=profile['qualified_native_flags']+['-isystem',str(base/'sdk'),'-isystem',str(legacy/'sdk'),'-isystem',str(libcxx/'include'),
  '-isystem',str(legacy/'c-sdk'),'-isystem',str(clang.parent.parent/'lib/clang/24/include'),
  '-I',str(base/'llvm-libc'),'-I',str(base/'probe'),'-I',str(src),'-I',str(runtime),'-I',str(base/'highway')]
 common=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti',
  '-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables',
  '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Itests','-I'+str(base),'-I'+str(src),
  '-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+(repo/'tools/kernel-cxxflags').read_text().split()
 sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
  'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp',
  'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp']
 assembly=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
 integer=a.integer_builtins.absolute();bind(integer);assert sha(integer)==profile['integer_builtins_sha256']
 libsources=[runtime/'clock-gettime.cc',runtime/'heap.cc',runtime/'emutls.cc',base/'llvm-libc/src/errno/libc_errno.cpp']
 for opt in ([2] if a.locate else [0,2]):
  level=out/('O'+str(opt));level.mkdir();elfs=[];library=level/'library';library.mkdir()
  objects=[]
  for f in libsources:
   bind(f);obj=library/(f.stem+'.o');objects.append(obj)
   run([clang,*flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',f,'-o',obj],obj.with_suffix('.log'));dependencies(obj.with_suffix('.d'))
  if opt==2:
   for name,expected in profile['executed_objects'].items():assert sha(library/name)==expected,'Executed runtime object differs '+name
  for mode in range(4):
   app=level/('mode'+str(mode));app.mkdir()
   run([clang,*flags,'-O'+str(opt),'-DGTOS_C_CLOCK_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',base/'probe/main.cc','-o',app/'main.o'],app/'main.log');dependencies(app/'main.d')
   run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',app/'memory.o'],app/'memory.log')
   run([bins['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
   run([bins['ld'],'-melf_i386','--gc-sections','-T',base/'probe/linker.ld','-Map',app/'probe.map','-o',app/'probe.elf',
    app/'start.o',app/'main.o',app/'memory.o',*objects,integer],app/'link.log')
   assert not run([bins['nm'],'-u',app/'probe.elf'],app/'undefined.log').strip()
   run([bins['readelf'],'-h','-l',app/'probe.elf'],app/'elf.log')
   for su in library.glob('*.su'):shutil.copyfile(su,app/su.name)
   dis=run([bins['objdump'],'-d',app/'probe.elf'],app/'disassembly.log')
   run([bins['objcopy'],'--strip-all',app/'probe.elf',app/'probe.stripped.elf'],app/'strip.log')
   assert (app/'probe.stripped.elf').stat().st_size<=65536
   assert sha(app/'probe.stripped.elf')==profile['executed_elfs']['O'+str(opt)+'-mode'+str(mode)],'Executed ELF differs'
   elfs.append(app/'probe.stripped.elf')
  if opt==2:
   assert sha(level/'mode0/memory.o')==profile['executed_memory_object']
   archive=level/'libgtos_native_c_clock_diagnostic.a'
   run([pathlib.Path(shutil.which('ar')),'rcsD',archive,*objects,level/'mode0/memory.o'],level/'archive.log')
   closure=level/'component-closure.o'
   run([bins['ld'],'-melf_i386','-r','-o',closure,'--whole-archive',archive,'--no-whole-archive'],level/'closure.log')
   assert not run([bins['nm'],'-u',closure],level/'closure-undefined.log').strip()
  for unsupported in [0,1]:
   d=level/('unsupported'+str(unsupported));(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
   for f in [repo/n for n in sources]+[base/'probe/kernel.cpp']:
    bind(f);obj=d/'kernel'/(f.stem+'.o')
    run([cxx,*common,'-O'+str(opt),'-DGTOS_UTC_UNSUPPORTED='+str(unsupported),'-Wno-write-strings','-MD','-MF',obj.with_suffix('.d'),'-c',f,'-o',obj],obj.with_suffix('.log'));dependencies(obj.with_suffix('.d'))
   run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',d/'kernel/byte_helpers.o'],d/'byte_helpers.log')
   for f,n in zip(assembly,['loader.o','native_fp_asm.o','stubs.o']):run([bins['as'],'--32',f,'-o',d/'kernel'/n],d/(n+'.log'))
   run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*sorted((d/'kernel').glob('*.o'))],d/'link.log')
   assert not run([bins['nm'],'-u',d/'kernel.bin'],d/'undefined.log').strip()
   run([pathlib.Path(sys.executable),'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'integer-audit.log')
   shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
   for mode,elf in enumerate(elfs):shutil.copyfile(elf,d/'iso/boot'/('clock'+str(mode)+'.elf'))
   (d/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Actual C clock" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/clock'+str(mode)+'.elf\n' for mode in range(4))+' boot\n}\n')
   run([bins['grub-mkrescue'],'--output='+str(d/'native.iso'),d/'iso'],d/'grub.log')
   assert loaded_sha(d/'kernel.bin')==profile['executed_loaded_kernels']['O'+str(opt)+'-rtc'+str(unsupported)],'Executed loaded kernel differs'
   if a.build_only:continue
   for mem,cpus in ([(32,1)] if a.locate else [(32,1),(64,4)]):
    guest=d/(str(mem)+'M-'+str(cpus));guest.mkdir()
    run([bins['qemu-system-i386'],'-L',os.environ['GTOS_QEMU_DATA_DIR'],'-machine','pc','-accel','tcg','-cpu','max',
     '-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mem),'-smp',str(cpus),'-cdrom',d/'native.iso','-boot','d',
     '-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest/'guest.log'),
     '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',33)
    evidence=observed((guest/'guest.log').read_text(),bool(unsupported));emit(guest/'observations.json',evidence)
    state['guests'].append(dict(opt=opt,unsupported=unsupported,memory=mem,cpus=cpus,evidence=evidence,guest_log_sha256=sha(guest/'guest.log')))
    print('PASS C CLOCK '+str((opt,unsupported,mem,cpus)),flush=True)
 assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
 state.update(all_required_checks_pass=True,native_c_clock_guest_pass=not a.build_only,qualification_reused=a.build_only,source_before_after_identical=True,
  exact_reap_checks=sum(g['evidence']['exact_reap_checks'] for g in state['guests']),actual_victim_cases=sum(len(g['evidence']['cases']) for g in state['guests']))
except BaseException as e:
 state['failure']=str(e);(out/'exception.log').write_text(traceback.format_exc())
finally:
 state['build_only']=a.build_only;state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps({k:v for k,v in state.items() if k not in ('inputs','commands','guests')},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
