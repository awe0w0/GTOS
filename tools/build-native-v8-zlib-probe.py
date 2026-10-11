#!/usr/bin/env python3
import argparse,datetime,hashlib,importlib.util,json,os,pathlib,re,shlex,shutil,signal,subprocess,sys,traceback

parser=argparse.ArgumentParser(description='Qualify actual zlib and native file consumers from a retained complete native V8 build.')
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--complete-build',type=pathlib.Path,required=True)
parser.add_argument('--qualified-file-build',type=pathlib.Path,required=True)
parser.add_argument('--clang',type=pathlib.Path,required=True)
parser.add_argument('--cxx',type=pathlib.Path,required=True)
parser.add_argument('--integer-builtins',type=pathlib.Path,required=True)
parser.add_argument('--component-cache',type=pathlib.Path)
args=parser.parse_args()
REPO=pathlib.Path(__file__).resolve().parents[1]
FULL=args.complete_build.resolve();PROBE=REPO/'apps/native_v8_integration/probe/zlib';OUT=args.output.resolve()
assert not OUT.exists(),'Fresh output required; retain failures.'
OUT.mkdir()
state={'scope':'固定 zlib gzip 解码、原生文件后端、容量失败与回滚、入口清理的 CPL3 验证；完整压缩及 V8 Isolate 尚未验收',
       'all_required_checks_pass':False,'native_isolate_pass':False,'browser_guest_pass':False,'capacity_changed':False,
       'production_image_changed':False,'inputs':{},'commands':[],'objects':{},'elfs':{},'guests':[]}
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def bind(p):
    p=pathlib.Path(p).resolve()
    if str(p) in state['inputs']:return
    digest=sha(p)
    state['inputs'][str(p)]=digest
    target=OUT/'source-snapshot'/hashlib.sha256(str(p).encode()).hexdigest()/p.name
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
def run(argv,log,cwd=REPO,expected=0,timeout=180):
    argv=list(map(str,argv));log.parent.mkdir(parents=True,exist_ok=True)
    with log.open('wb') as stream:
        process=subprocess.Popen(argv,cwd=cwd,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:code=process.wait(timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);process.wait();code=124
    state['commands'].append(dict(argv=argv,cwd=str(cwd),exit_code=code,log=str(log),log_sha256=sha(log)));save()
    if code!=expected:raise RuntimeError(str(log)+' exit='+str(code)+'\n'+log.read_text(errors='replace')[-4000:])
    return log.read_text()
def deps(p):
    for token in p.read_text().replace('\\\n',' ').split(':',1)[1].split():
        path=pathlib.Path(token);bind(path if path.is_absolute() else FULL/'build'/path)
def load_module(name,p):
    spec=importlib.util.spec_from_file_location(name,p);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
def component(argv,obj,log):
    argv=list(map(str,argv))
    if args.component_cache is None:
        run(argv,log,FULL/'build');deps(obj.with_suffix('.d'));return
    cache=args.component_cache.resolve();status=json.loads((cache/'status.json').read_text())
    bind(cache/'status.json')
    old=cache/obj.relative_to(OUT);key=obj.parent.name+'-'+obj.name
    if key not in status['objects']:
        run(argv,log,FULL/'build');deps(obj.with_suffix('.d'));return
    assert sha(old)==status['objects'][key]==probe_lock['consumer_components'][key],'Cached component changed: '+str(old)
    assert status['tools']['clang']==state['tools']['clang'],'Cached compiler changed'
    commands=[c for c in status['commands'] if '-o' in c.get('argv',[]) and c['argv'][c['argv'].index('-o')+1]==str(old)]
    assert len(commands)==1 and commands[0]['exit_code']==0 and commands[0]['cwd']==str(FULL/'build')
    def normalize(words):
        result=[];i=0
        while i<len(words):
            if words[i] in ['-o','-MF']:i+=2;continue
            if words[i].startswith('-fcrash-diagnostics-dir='):i+=1;continue
            result.append(words[i]);i+=1
        return result
    if normalize(commands[0]['argv'])!=normalize(argv):
        run(argv,log,FULL/'build');deps(obj.with_suffix('.d'));return
    dep=old.with_suffix('.d');bind(dep)
    inputs=[]
    for token in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
        p=pathlib.Path(token);p=(p if p.is_absolute() else FULL/'build'/p).resolve()
        assert str(p) in status['inputs'] and sha(p)==status['inputs'][str(p)],'Cached dependency changed: '+str(p)
        bind(p);inputs.append(str(p))
    bind(old);shutil.copyfile(old,obj);shutil.copyfile(dep,obj.with_suffix('.d'))
    entry=dict(kind='reuse_component',source=str(old),sha256=sha(old),command=argv,dependencies=inputs)
    log.write_text(json.dumps(entry,indent=2)+'\n');state.setdefault('reused_components',[]).append(entry);save()
def flags(entry,opt):
    words=shlex.split(entry['command']);result=[];index=1
    while index<len(words):
        word=words[index]
        if word in ['-c','-o','-MF']:index+=2;continue
        if word=='-MMD' or (opt==0 and word.startswith('-O')) or word.startswith('-fcrash-diagnostics-dir='):index+=1;continue
        result.append(word);index+=1
    return result+(['-O0'] if opt==0 else [])+['-fcrash-diagnostics-dir='+str(OUT/'crashreports')]
try:
    probe_lock_path=PROBE/'source-lock.json';bind(probe_lock_path)
    probe_lock=json.loads(probe_lock_path.read_text())
    for relative,digest in probe_lock['files'].items():
        pure=pathlib.PurePosixPath(relative)
        assert not pure.is_absolute() and '..' not in pure.parts
        path=REPO/relative;assert not path.is_symlink() and REPO in path.resolve().parents
        assert sha(path)==digest,'Probe source changed: '+relative;bind(path)
    complete=json.loads((FULL/'status.json').read_text());assert complete['bound_inputs_unchanged'] and complete['gn_pass']
    bind(FULL/'status.json');bind(FULL/'actual-compile-database.log');bind(__file__)
    frozen=FULL/'module-input-snapshot';assert frozen.exists(),'Retain completed build inputs before editing the module.'
    for name in ['source-lock.json','payload-lock.json']:
        expected=[digest for path,digest in complete['inputs'].items() if path.endswith('/apps/native_v8_integration/'+name)]
        assert len(expected)==1 and sha(frozen/name)==expected[0];bind(frozen/name)
    assert sha(frozen/'payload-lock.json')==probe_lock['integration_payload_sha256'],'Wrong qualified integration payload'
    payload=json.loads((frozen/'payload-lock.json').read_text())
    for name,item in payload['files'].items():
        assert sha(frozen/name)==item['sha256'],name
        if name.startswith('overlay/'):source_path=FULL/'source'/name[len('overlay/'):]
        elif name.startswith(('sdk/','c-sdk/')):source_path=FULL/'source/tools/gtos_isolate_diagnostic'/name
        else:continue
        assert sha(source_path)==item['sha256'],source_path
    lock_path=REPO/'apps/native_c_file/source-lock.json';bind(lock_path);lock=json.loads(lock_path.read_text())
    for name,digest in lock['files'].items():assert sha(REPO/name)==digest,name
    helper=load_module('exit_file_helper',REPO/'tools/build-native-file-probe.py')
    geometry=load_module('zlib_geometry',PROBE/'geometry.py').geometry
    observer=load_module('exit_observer',PROBE/'observer.py')
    for p in [REPO/'tools/build-native-file-probe.py',REPO/'apps/native_c_file/probe/observer.py',REPO/'tools/audit-kernel-instructions.py',REPO/'tools/kernel-cxxflags',REPO/'tests/native_process_smoke.ld']:
        bind(p)
    for p in PROBE.iterdir():
        if p.is_file():bind(p)
    clang=args.clang.resolve();cxx=args.cxx.resolve()
    assert sha(cxx)==lock['tools']['cxx']['sha256'];assert sha(clang)==lock['tools']['clang']['sha256']
    bins={n:pathlib.Path(shutil.which(n)).resolve() for n in ['as','ld','nm','objcopy','grub-mkrescue','qemu-system-i386']}
    state['tools']={n:{'path':str(p),'sha256':sha(p)} for n,p in dict(bins,clang=clang,cxx=cxx).items()}
    for n in ['as','ld','nm','objcopy']:assert sha(bins[n])==lock['tools'][n]['sha256']
    assert 'version 8.' in subprocess.check_output([bins['qemu-system-i386'],'--version'],text=True)
    integer=args.integer_builtins.resolve();bind(integer)
    source_lock=json.loads((frozen/'source-lock.json').read_text());assert sha(integer)==source_lock['tool_sha256']['integer_builtins']
    resource=(clang.parent/'../lib/clang/24/include').resolve()
    for name,digest in source_lock['clang_resource_files'].items():assert sha(resource/name)==digest,name
    database=json.loads((FULL/'actual-compile-database.log').read_text())
    entry=next(e for e in database if e.get('output')=='obj/tools/gtos_isolate_diagnostic/consumer.main.o')
    old_full=str(pathlib.Path(entry['directory']).parent)
    entry['command']=entry['command'].replace(old_full,str(FULL))
    words=shlex.split(entry['command']);old_resource=next(words[i+1] for i,w in enumerate(words[:-1]) if w=='-isystem' and '/lib/clang/' in words[i+1])
    entry['command']=entry['command'].replace(old_resource,str(clang.parent/'../lib/clang/24/include'))
    real=FULL/'source';runtime=real/'tools/gtos_isolate_diagnostic/runtime'
    native=[runtime/'thread-exit.cc',real/'tools/gtos_isolate_diagnostic/native-entry.cc']
    bind(real/'tools/gtos_isolate_diagnostic/native-start.s')
    policy=(REPO/'tools/kernel-cxxflags').read_text().split();assert policy==['-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize']
    kernel_flags=['-m32','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pie','-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-Itests','-I'+str(PROBE),'-Wall','-Wextra','-Werror',*policy,'-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit','-Wno-write-strings']
    for opt in [0,2]:
        level='O'+str(opt);d=OUT/level;d.mkdir();qualified=args.qualified_file_build.resolve()/level
        library=sorted((qualified/'library').glob('*.o'))
        assert {p.name for p in library}=={name[len(level+'-library-'):] for name in lock['objects'] if name.startswith(level+'-library-')}
        for p in library:assert sha(p)==lock['objects'][level+'-library-'+p.name],'Qualified FILE object changed '+str(p);bind(p)
        kernel=[]
        for name,digest in lock['objects'].items():
            prefix=level+'-kernel-'
            if name.startswith(prefix) and name!=prefix+'probe.o':
                p=qualified/name[len(prefix):];assert sha(p)==digest;bind(p);kernel.append(p)
        native_flags=flags(entry,opt)+['-I',str(PROBE),'-I',str(REPO/'apps/native_c_clock/runtime'),'-I',str(REPO/'include')]
        compiled=[]
        for p in native:
            bind(p);obj=d/(p.stem+'.o');component([clang,*native_flags,'-MD','-MF',obj.with_suffix('.d'),'-c',p,'-o',obj],obj,obj.with_suffix('.log'));compiled.append(obj);state['objects'][level+'-'+obj.name]=sha(obj)
        extras=[e for e in database if (e.get('output','').startswith('obj/third_party/zlib/libchrome_zlib.')
                or e.get('output','').endswith('native_string_libc.strerror.o')
                or 'native_printf_libc.' in e.get('output','')) and e.get('output','').endswith('.o') and e.get('file','').endswith(('.c','.cc','.cpp'))]
        assert any(e['file'].endswith('/strerror.cpp') for e in extras)
        assert any(e['file'].endswith('/error_to_string.cpp') for e in extras)
        assert any(e['file'].endswith('/inflate.c') and '/contrib/' not in e['file'] for e in extras)
        for e in extras:
            p=(FULL/'build'/e['file']).resolve();bind(p)
            obj=d/(pathlib.Path(e['output']).stem+'.o')
            component([clang,*flags(e,opt),'-MD','-MF',obj.with_suffix('.d'),'-c',e['file'],'-o',obj],obj,obj.with_suffix('.log'))
            compiled.append(obj);state['objects'][level+'-'+obj.name]=sha(obj)
            actual=FULL/'build'/e['output']
            if opt==2 and actual.exists():
                assert sha(actual)==sha(obj),'Actual GN object differs: '+str(actual)
                state.setdefault('actual_complete_objects_identical',[]).append(str(actual))
        elfs=[];plans={}
        for mode in range(6):
            app=d/('mode'+str(mode));app.mkdir();obj=app/'main.o'
            run([clang,*native_flags,'-DGTOS_ZLIB_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',PROBE/'main.cc','-o',obj],app/'main.log',FULL/'build');deps(app/'main.d')
            run([bins['as'],'--32',real/'tools/gtos_isolate_diagnostic/native-start.s','-o',app/'start.o'],app/'start.log')
            run([bins['ld'],'-melf_i386','--gc-sections','-T',PROBE/'linker.ld','-Map',app/'user.map','-o',app/'user.elf',app/'start.o',obj,*compiled,*library,integer],app/'link.log')
            assert not run([bins['nm'],'-u',app/'user.elf'],app/'undefined.log').strip()
            symbols=run([bins['nm'],'-C',app/'user.elf'],app/'symbols.log')
            assert all(name in symbols for name in ['adler32','gzread','strerror']+(['deflateInit2_'] if mode<2 else []))
            run([bins['objcopy'],'--strip-all',app/'user.elf',app/'user.stripped.elf'],app/'strip.log')
            plans[mode]=geometry(app/'user.stripped.elf',mode);elfs.append(app/'user.stripped.elf');state['elfs'][level+'-mode'+str(mode)]={'sha256':sha(elfs[-1]),**plans[mode]}
        run([cxx,*kernel_flags,'-O'+str(opt),'-MD','-MF',d/'probe.d','-c',PROBE/'kernel.cpp','-o',d/'probe.o'],d/'probe.log');
        for token in (d/'probe.d').read_text().replace('\\\n',' ').split(':',1)[1].split():bind(REPO/token)
        kernel.append(d/'probe.o')
        run([bins['ld'],'-melf_i386','--gc-sections','-T',REPO/'tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*kernel],d/'kernel-link.log')
        run([sys.executable,REPO/'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',REPO,d/'kernel.bin'],d/'kernel-audit.log')
        for have in [True]:
            guest=d/('guest-rdrand' if have else 'guest-no-rdrand');(guest/'iso/boot/grub').mkdir(parents=True);shutil.copyfile(d/'kernel.bin',guest/'iso/boot/native.bin')
            for mode,p in enumerate(elfs):shutil.copyfile(p,guest/'iso/boot'/('exit'+str(mode)+'.elf'))
            (guest/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Actual native zlib consumers" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/exit'+str(m)+'.elf\n' for m in range(6))+' boot\n}\n')
            run([bins['grub-mkrescue'],'--output='+str(guest/'native.iso'),guest/'iso'],guest/'grub.log')
            image=guest/'filesystem.img';image.write_bytes(b''.join(bytes([0xa5+s])*512 for s in range(8))+b'\xff'*(2048*512)+b''.join(bytes([0x5a+s])*512 for s in range(8)));helper.guards(image)
            mib=32 if opt==0 else 64
            run([bins['qemu-system-i386'],'-L',os.environ['GTOS_QEMU_DATA_DIR'],'-machine','pc','-accel','tcg,thread=single','-icount','shift=0,align=off,sleep=off','-cpu','max' if have else 'max,-rdrand','-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mib),'-smp','1' if opt==0 else '4','-cdrom',guest/'native.iso','-boot','d','-drive','file='+str(image)+',format=raw,if=ide,index=0','-nic','none','-display','none','-monitor','none','-serial','none','-debugcon','file:'+str(guest/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',expected=33,timeout=240)
            text=(guest/'guest.log').read_text();evidence=observer.verify(text,plans,mib,have);helper.guards(image)
            rejected=observer.negative_controls(text,plans,mib,have)
            state['guests'].append(dict(opt=opt,rdrand_enabled=have,memory_mib=mib,evidence=evidence,negative_observer_controls=rejected,guest_log_sha256=sha(guest/'guest.log'),disk_sha256=sha(image)));save();print('PASS actual native zlib '+level+' rdrand='+str(have),flush=True)
    assert state['objects']==probe_lock['consumer_components'],'Qualified consumer component bytes changed'
    assert all(sha(p)==h for p,h in state['inputs'].items()),'Bound inputs changed'
    assert all(sha(value['path'])==value['sha256'] for value in state['tools'].values()),'Tools changed'
    state['all_required_checks_pass']=True;state['source_before_after_identical']=True
except BaseException as error:
    state['failure']=str(error);(OUT/'exception.log').write_text(traceback.format_exc())
state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
print(json.dumps({k:v for k,v in state.items() if k not in ['inputs','commands','objects','elfs','reused_components']},indent=2),flush=True)
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
