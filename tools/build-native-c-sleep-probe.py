import argparse,datetime,hashlib,importlib.util,json,os,pathlib,re,shutil,struct,subprocess,sys,time,traceback
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('--build-only',action='store_true')
for name in ['clang','libcxx-source','memory-cxx','integer-builtins']:p.add_argument('--'+name,required=True,type=pathlib.Path)
a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1];ws=repo.parent.parent;base=repo/'apps/native_c_sleep'
clock=repo/'apps/native_c_clock';legacy=repo/'apps/native_v8_runtime';src=legacy/'include';runtime=clock/'runtime'
profile=json.loads((base/'source-lock.json').read_text());cprofile=json.loads((clock/'source-lock.json').read_text())
out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
state=dict(scope='Native C nanosleep with actual LLVM time/TLS errno, full signed duration domain and external cancellation',
 all_required_checks_pass=False,native_c_sleep_guest_pass=False,native_isolate_pass=False,production_image_changed=False,capacity_changed=False,commands=[],guests=[],inputs={})
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
def loaded_sha(p):
 data=pathlib.Path(p).read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01'
 h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);assert h[0]==2 and h[1]==3 and h[2]==1 and h[8]==32
 digest=hashlib.sha256();digest.update(data[:16]);digest.update(struct.pack('<HHII',*h[:4]))
 for i in range(h[9]):
  ph=data[h[4]+i*32:h[4]+(i+1)*32];fields=struct.unpack('<8I',ph);digest.update(ph)
  if fields[0]==1:digest.update(data[fields[1]:fields[1]+fields[4]])
 return digest.hexdigest()

def user_pages(p):
 d=pathlib.Path(p).read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',d,16);pages=set()
 assert h[0]==2 and h[1]==3 and h[9]<=8
 for i in range(h[9]):
  ph=struct.unpack_from('<8I',d,h[4]+i*32)
  if ph[0]!=1:continue
  _,off,va,pa,fs,ms,flags,align=ph
  assert off+fs<=len(d) and fs<=ms and flags in [5,6] and va>=0x40000000 and va+ms<=0x40041000
  if ms:pages.update(range(va//4096,(va+ms+4095)//4096))
 assert 0<len(pages)<=256
 return len(pages)
try:
 bind(pathlib.Path(__file__));bind(base/'source-lock.json');bind(clock/'source-lock.json')
 assert sha(clock/'source-lock.json')==profile['c_clock_source_lock_sha256']
 for relative,h in profile['files'].items():
  path=base/relative;assert sha(path)==h,'Locked sleep input changed '+relative;bind(path)
 for relative,h in cprofile['files'].items():
  path=clock/relative;assert sha(path)==h,'Qualified C clock input changed '+relative;bind(path)
 for relative,h in cprofile['legacy_dependencies'].items():
  path=legacy/relative;assert sha(path)==h,'Qualified companion changed '+relative;bind(path)
 assert len(profile['executed_elfs'])==8 and len(profile['executed_loaded_kernels'])==2 and len(profile['executed_objects'])==10
 assert sha(pathlib.Path(__file__))==profile['build_driver_sha256']
 bind(legacy/'runtime/memory.cc');assert sha(legacy/'runtime/memory.cc')==profile['legacy_memory_source_sha256']
 clang=a.clang.absolute();cxx=a.memory_cxx.absolute();libcxx=a.libcxx_source.absolute()
 assert sha(clang)==cprofile['clang_sha256'] and sha(cxx)==cprofile['memory_cxx_sha256']
 assert subprocess.check_output(['git','-C',libcxx,'rev-parse','HEAD'],text=True).strip()==cprofile['libcxx_revision']
 bins={n:pathlib.Path(shutil.which(n)).absolute() for n in ('as','ld','nm','readelf','objdump','objcopy','ar','grub-mkrescue','qemu-system-i386')}
 state['tools']={n:dict(path=str(v),sha256=sha(v)) for n,v in dict(bins,clang=clang,cxx=cxx).items()}
 flags=cprofile['qualified_native_flags']+['-isystem',str(clock/'sdk'),'-isystem',str(legacy/'sdk'),'-isystem',str(libcxx/'include'),
  '-isystem',str(legacy/'c-sdk'),'-isystem',str(clang.parent.parent/'lib/clang/24/include'),
  '-I',str(clock/'llvm-libc'),'-I',str(base/'probe'),'-I',str(src),'-I',str(base/'runtime'),'-I',str(runtime),'-I',str(clock/'highway')]
 common=['-m32','-std=c++11','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-exceptions','-fno-rtti',
  '-fno-stack-protector','-fno-pie','-fno-threadsafe-statics','-fno-use-cxa-atexit','-fno-asynchronous-unwind-tables',
  '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Itests','-I'+str(base/'probe'),'-I'+str(src),
  '-D__GTOS__=1','-U__linux__','-U__unix__','-Ulinux','-Uunix','-Wall','-Wextra','-Werror']+(repo/'tools/kernel-cxxflags').read_text().split()
 sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
  'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp',
  'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp']
 assembly=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
 integer=a.integer_builtins.absolute();bind(integer);assert sha(integer)==cprofile['integer_builtins_sha256']
 libsources=[runtime/'clock-gettime.cc',runtime/'heap.cc',runtime/'emutls.cc',clock/'llvm-libc/src/errno/libc_errno.cpp',base/'runtime/nanosleep.cc']
 spec=importlib.util.spec_from_file_location('sleep_observer',base/'probe/observer.py');observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)
 state['built_objects']={};state['built_elfs']={};state['loaded_kernels']={}
 for opt in [0,2]:
  level=out/('O'+str(opt));level.mkdir();elfs=[];library=level/'library';library.mkdir();objects=[]
  for f in libsources:
   bind(f);obj=library/(f.stem+'.o');objects.append(obj)
   run([clang,*flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',f,'-o',obj],obj.with_suffix('.log'));dependencies(obj.with_suffix('.d'))
   state['built_objects']['O'+str(opt)+'-'+obj.name]=sha(obj)
  if opt==2:
   for name,expected in cprofile['executed_objects'].items():assert sha(library/name)==expected,'Qualified clock object changed '+name
  for mode in range(4):
   app=level/('mode'+str(mode));app.mkdir()
   run([clang,*flags,'-O'+str(opt),'-DGTOS_SLEEP_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',base/'probe/main.cc','-o',app/'main.o'],app/'main.log');dependencies(app/'main.d')
   run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',app/'memory.o'],app/'memory.log')
   run([bins['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
   run([bins['ld'],'-melf_i386','--gc-sections','-T',base/'probe/linker.ld','-Map',app/'probe.map','-o',app/'probe.elf',
    app/'start.o',app/'main.o',app/'memory.o',*objects,integer],app/'link.log')
   assert not run([bins['nm'],'-u',app/'probe.elf'],app/'undefined.log').strip()
   run([bins['readelf'],'-h','-l',app/'probe.elf'],app/'elf.log')
   for su in library.glob('*.su'):shutil.copyfile(su,app/su.name)
   run([bins['objdump'],'-d',app/'probe.elf'],app/'disassembly.log')
   run([bins['objcopy'],'--strip-all',app/'probe.elf',app/'probe.stripped.elf'],app/'strip.log')
   pages=user_pages(app/'probe.stripped.elf')
   key='O'+str(opt)+'-mode'+str(mode);state['built_elfs'][key]=dict(sha256=sha(app/'probe.stripped.elf'),load_pages=pages)
   if profile['executed_elfs']:assert state['built_elfs'][key]==profile['executed_elfs'][key],'Executed ELF differs '+key
   elfs.append(app/'probe.stripped.elf')
  for key,h in profile['executed_objects'].items():
   if key.startswith('O'+str(opt)+'-'):assert state['built_objects'][key]==h,'Executed object differs '+key
  archive=level/'libgtos_native_c_sleep_diagnostic.a'
  run([bins['ar'],'rcsD',archive,*objects,level/'mode0/memory.o'],level/'archive.log')
  closure=level/'component-closure.o'
  run([bins['ld'],'-melf_i386','-r','-o',closure,'--whole-archive',archive,'--no-whole-archive'],level/'closure.log')
  assert not run([bins['nm'],'-u',closure],level/'closure-undefined.log').strip()
  assert sha(archive)==profile['executed_archives']['O'+str(opt)]['archive_sha256']
  assert sha(closure)==profile['executed_archives']['O'+str(opt)]['closure_sha256']
  d=level/'kernel-build';(d/'kernel').mkdir(parents=True);(d/'iso/boot/grub').mkdir(parents=True)
  for f in [repo/n for n in sources]+[base/'probe/kernel.cpp']:
   bind(f);obj=d/'kernel'/(f.stem+'.o')
   run([cxx,*common,'-O'+str(opt),'-Wno-write-strings','-MD','-MF',obj.with_suffix('.d'),'-c',f,'-o',obj],obj.with_suffix('.log'));dependencies(obj.with_suffix('.d'))
  run([cxx,*common,'-O'+str(opt),'-c','apps/native_v8_runtime/runtime/memory.cc','-o',d/'kernel/byte_helpers.o'],d/'byte_helpers.log')
  for f,n in zip(assembly,['loader.o','native_fp_asm.o','stubs.o']):run([bins['as'],'--32',f,'-o',d/'kernel'/n],d/(n+'.log'))
  run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*sorted((d/'kernel').glob('*.o'))],d/'link.log')
  assert not run([bins['nm'],'-u',d/'kernel.bin'],d/'undefined.log').strip()
  run([pathlib.Path(sys.executable),'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'integer-audit.log')
  shutil.copyfile(d/'kernel.bin',d/'iso/boot/native.bin')
  for mode,elf in enumerate(elfs):shutil.copyfile(elf,d/'iso/boot'/('sleep'+str(mode)+'.elf'))
  (d/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Actual C sleep" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/sleep'+str(mode)+'.elf\n' for mode in range(4))+' boot\n}\n')
  run([bins['grub-mkrescue'],'--output='+str(d/'native.iso'),d/'iso'],d/'grub.log')
  state['loaded_kernels']['O'+str(opt)]=loaded_sha(d/'kernel.bin')
  if profile['executed_loaded_kernels']:assert state['loaded_kernels']['O'+str(opt)]==profile['executed_loaded_kernels']['O'+str(opt)],'Executed kernel differs'
  if a.build_only:continue
  for mem,cpus in [(32,1),(64,4)]:
   guest=d/(str(mem)+'M-'+str(cpus));guest.mkdir()
   run([bins['qemu-system-i386'],'-L',os.environ['GTOS_QEMU_DATA_DIR'],'-machine','pc','-accel','tcg','-cpu','max',
    '-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mem),'-smp',str(cpus),'-cdrom',d/'native.iso','-boot','d',
    '-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest/'guest.log'),
    '-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',33)
   evidence=observer.verify((guest/'guest.log').read_text())
   for c in evidence['cases']:assert c['load_pages']==state['built_elfs']['O'+str(opt)+'-mode'+str(c['mode'])]['load_pages']
   emit(guest/'observations.json',evidence)
   state['guests'].append(dict(opt=opt,memory=mem,cpus=cpus,evidence=evidence,guest_log_sha256=sha(guest/'guest.log')))
   print('PASS C SLEEP '+str((opt,mem,cpus)),flush=True)
 assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
 state.update(all_required_checks_pass=True,native_c_sleep_guest_pass=not a.build_only,qualification_reused=a.build_only,source_before_after_identical=True,
  exact_reap_checks=sum(len(g['evidence']['reaps'])+1 for g in state['guests']),actual_victim_cases=sum(len(g['evidence']['cases']) for g in state['guests']))
except BaseException as e:
 state['failure']=str(e);(out/'exception.log').write_text(traceback.format_exc())
finally:
 state['build_only']=a.build_only;state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
 print(json.dumps({k:v for k,v in state.items() if k not in ('inputs','commands','guests')},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
