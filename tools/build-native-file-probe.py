#!/usr/bin/env python3
"""Qualify the native IA32 file ABI on real CPL3 and isolated ATA media."""
import argparse,datetime,hashlib,importlib.util,json,os,pathlib,re,shutil,struct,subprocess,sys,traceback

def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def loaded_sha(path):
    data=pathlib.Path(path).read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    assert data[:7]==b'\x7fELF\x01\x01\x01' and h[0]==2 and h[1]==3 and h[8]==32
    digest=hashlib.sha256();digest.update(data[:16]);digest.update(struct.pack('<HHII',*h[:4]))
    for i in range(h[9]):
        ph=data[h[4]+i*32:h[4]+(i+1)*32];p=struct.unpack('<8I',ph);digest.update(ph)
        if p[0]==1:digest.update(data[p[1]:p[1]+p[4]])
    return digest.hexdigest()
def guards(path):
    data=path.read_bytes();assert len(data)==2064*512
    for s in range(8):assert data[s*512:(s+1)*512]==bytes([0xa5+s])*512,'prefix modified'
    for s in range(2056,2064):assert data[s*512:(s+1)*512]==bytes([0x5a+s-2056])*512,'suffix modified'
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=pathlib.Path)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--cc',default=os.environ.get('CC','gcc'))
    parser.add_argument('--clang',type=pathlib.Path)
    parser.add_argument('--libcxx-source',type=pathlib.Path)
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args();assert bool(args.clang)==bool(args.libcxx_source),'Pass clang and libcxx-source together'
    repo=pathlib.Path(__file__).resolve().parents[1];base=repo/'apps/native_files';probe=base/'probe';out=args.output.resolve()
    assert not out.exists(),'Use a fresh directory; preserve earlier builds.';out.mkdir(parents=True)
    actual=bool(args.clang);kind='qualified-llvm' if actual else 'scalar-fixture'
    state={'scope':'Native file ABI in actual CPL3 with full FileStore and isolated ATA; not stdio/POSIX/browser',
        'profile':kind,'all_required_checks_pass':False,'native_file_abi_guest_pass':False,
        'native_isolate_pass':False,'browser_guest_pass':False,'production_image_changed':False,'capacity_changed':False,
        'build_only':args.build_only,'commands':[],'inputs':{},'objects':{},'elfs':{},'kernels':{},'loaded_kernels':{},'guests':[]}
    def save():(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
    def bind(path):
        path=pathlib.Path(path);path=(repo/path).resolve() if not path.is_absolute() else path.resolve();h=sha(path)
        if str(path) in state['inputs']:assert state['inputs'][str(path)]==h,'Source changed '+str(path)
        else:
            state['inputs'][str(path)]=h
            try:relative=path.relative_to(repo)
            except ValueError:relative=pathlib.Path('external')/hashlib.sha256(str(path).encode()).hexdigest()/path.name
            dest=out/'source-snapshot'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
    def run(cmd,log,expected=0,timeout=180):
        cmd=list(map(str,cmd))
        with log.open('wb') as f:
            try:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
            except subprocess.TimeoutExpired:code=124
        state['commands'].append({'argv':cmd,'exit_code':code,'log':str(log),'log_sha256':sha(log)});save()
        if code!=expected:
            guest=log.parent/'guest.log';detail=guest.read_text(errors='replace')[-5000:] if guest.exists() else ''
            raise RuntimeError(str(log)+' exit='+str(code)+'\n'+log.read_text(errors='replace')[-4000:]+'\n'+detail)
        return log.read_text()
    def deps(path):
        for token in path.read_text().replace('\\\n',' ').split(':',1)[1].split():bind(token)
    def boot(kernel,directory,phase,elfs,image,opt):
        (directory/'iso/boot/grub').mkdir(parents=True);shutil.copyfile(kernel,directory/'iso/boot/native.bin')
        for mode,elf in enumerate(elfs):shutil.copyfile(elf,directory/'iso/boot'/('file'+str(mode)+'.elf'))
        (directory/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native file ABI" {\n multiboot /boot/native.bin '+phase+'\n'+''.join(' module /boot/file'+str(mode)+'.elf\n' for mode in range(len(elfs)))+' boot\n}\n')
        run([bins['grub-mkrescue'],'--output='+str(directory/'native.iso'),directory/'iso'],directory/'grub.log')
        drive=['-drive','file='+str(image)+',format=raw,if=ide,index=0'] if image else []
        run([bins['qemu-system-i386'],'-L',os.environ.get('GTOS_QEMU_DATA_DIR','/usr/share/qemu'),'-machine','pc','-accel','tcg','-cpu','max',
            '-rtc','base=2000-01-01T00:00:00,clock=vm','-m','32' if opt==0 else '64','-smp','1' if opt==0 else '4',
            '-cdrom',directory/'native.iso','-boot','d',*drive,'-nic','none','-display','none','-monitor','none','-serial','none',
            '-debugcon','file:'+str(directory/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],directory/'host.log',33,200)
        return (directory/'guest.log').read_text()
    try:
        bind(__file__);lock_path=base/'source-lock.json';lock=None
        if lock_path.exists():
            lock=json.loads(lock_path.read_text());bind(lock_path)
            for name,h in lock['files'].items():assert sha(repo/name)==h,'Locked native-file source changed '+name;bind(name)
        cxx=pathlib.Path(shutil.which(args.cxx)).resolve();cc=pathlib.Path(shutil.which(args.cc)).resolve()
        bins={n:pathlib.Path(shutil.which(n)).resolve() for n in ['as','ld','nm','objcopy','grub-mkrescue','qemu-system-i386']}
        state['tools']={n:{'path':str(p),'sha256':sha(p)} for n,p in dict(bins,cxx=cxx,cc=cc).items()}
        state['compiler_version']=subprocess.check_output([cxx,'--version'],text=True).splitlines()[0]
        policy=(repo/'tools/kernel-cxxflags').read_text().split();bind('tools/kernel-cxxflags')
        assert policy==['-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize']
        common=['-m32','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pie',
            '-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude',
            '-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-Itests','-I'+str(probe),'-Wall','-Wextra','-Werror',*policy]
        cpp=['-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit','-Wno-write-strings']
        sources=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
            'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/native_files.cpp',
            'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp',
            'src/storage/filestore.cpp','src/storage/littlefs_port.cpp','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/drivers/ata.cpp']
        assembly=['tests/native_process_loader.s','src/process/native_fp.s','src/hardwarecommunication/interruptstubs.s']
        spec=importlib.util.spec_from_file_location('native_file_observer',probe/'observer.py');observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)
        bind(probe/'observer.py');bind(probe/'linker.ld');bind(probe/'stock.c');bind('tests/native_process_smoke.ld');bind('tools/audit-kernel-instructions.py')
        if actual:
            clang=args.clang.resolve();libcxx=args.libcxx_source.resolve();string=repo/'apps/native_c_string';clock=repo/'apps/native_c_clock';legacy=repo/'apps/native_v8_runtime'
            profile=json.loads((string/'source-lock.json').read_text());clock_profile=json.loads((clock/'source-lock.json').read_text())
            assert sha(clang)==clock_profile['clang_sha256'],'Require the qualified actual GN compiler'
            state['tools']['clang']={'path':str(clang),'sha256':sha(clang)}
            assert subprocess.check_output(['git','-C',libcxx,'rev-parse','HEAD'],text=True).strip()==clock_profile['libcxx_revision']
            bind(string/'source-lock.json');bind(clock/'source-lock.json')
            flags=profile['qualified_native_flags']+['-isystem',str(clock/'sdk'),'-isystem',str(legacy/'sdk'),'-isystem',str(libcxx/'include'),
                '-isystem',str(legacy/'c-sdk'),'-isystem',str(clang.parent.parent/'lib/clang/24/include'),'-I',str(string/'llvm-libc'),
                '-I',str(string/'probe'),'-I',str(legacy/'include'),'-I',str(clock/'runtime')]
        for opt in [0,2]:
            level='O'+str(opt);d=out/level;(d/'kernel').mkdir(parents=True);objects=[]
            for name in sources+['apps/native_v8_runtime/runtime/memory.cc']:
                src=repo/name;bind(src);obj=d/'kernel'/('byte_helpers.o' if name.endswith('/memory.cc') else pathlib.Path(name).stem+'.o');objects.append(obj)
                compile_flags=['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h'] if name.endswith('.c') else cpp
                run([cxx,*common,*compile_flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-'+obj.name]=sha(obj)
            for name,target in zip(assembly,['loader.o','native_fp_asm.o','stubs.o']):
                bind(name);obj=d/'kernel'/target;objects.append(obj);run([bins['as'],'--32',name,'-o',obj],d/(target+'.log'));state['objects'][level+'-'+target]=sha(obj)
            providers=[d/'kernel/byte_helpers.o']
            if actual:
                library=d/'actual-library';library.mkdir();providers=[]
                for name in ['memcpy','memset']:
                    src=string/'llvm-libc/src/string'/(name+'.cpp');obj=library/(name+'.o');bind(src)
                    run([clang,*flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                    assert sha(obj)==profile['executed_objects'][level+'-'+obj.name],'Qualified actual default provider changed'
                    state['objects'][level+'-actual-'+obj.name]=sha(obj);providers.append(obj)
            elfs=[];plans={}
            for mode in range(7):
                app=d/('mode'+str(mode));app.mkdir();src=probe/('unavailable.cc' if mode==6 else 'main.cc');bind(src);bind('apps/native_process_info_probe/start.s')
                user_compiler=clang if actual else cxx
                run([user_compiler,*common,*cpp,'-O'+str(opt),'-DGTOS_FILE_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',src,'-o',app/'main.o'],app/'main.log');deps(app/'main.d')
                run([bins['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
                run([bins['ld'],'-melf_i386','--gc-sections','-T',probe/'linker.ld','-Map',app/'user.map','-o',app/'user.elf',app/'start.o',app/'main.o',*providers],app/'link.log')
                assert not run([bins['nm'],'-u',app/'user.elf'],app/'undefined.log').strip()
                run([bins['objcopy'],'--strip-all',app/'user.elf',app/'user.stripped.elf'],app/'strip.log')
                plans[mode]=observer.geometry(app/'user.stripped.elf');state['elfs'][level+'-mode'+str(mode)]={'sha256':sha(app/'user.stripped.elf'),**plans[mode]};elfs.append(app/'user.stripped.elf')
            kernels={}
            for fixture in ['kernel','unavailable_kernel']:
                src=probe/(fixture+'.cpp');bind(src);obj=d/(fixture+'.o')
                run([cxx,*common,*cpp,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-'+obj.name]=sha(obj);binary=d/(fixture+'.bin')
                run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/(fixture+'.map'),'-o',binary,*objects,obj],d/(fixture+'-link.log'))
                assert not run([bins['nm'],'-u',binary],d/(fixture+'-undefined.log')).strip()
                run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/(fixture+'.map'),'--source-root',repo,binary],d/(fixture+'-integer-audit.log'))
                key=level+'-'+fixture;state['kernels'][key]=sha(binary);state['loaded_kernels'][key]=loaded_sha(binary);kernels[fixture]=binary
            if args.build_only:
                assert lock and kind in lock['profiles'],'Qualified profile unavailable'
                expected=lock['profiles'][kind];assert sha(cxx)==expected['cxx_sha256'],'Require qualified compiler for exact reproduction'
                if actual:assert sha(clang)==expected['clang_sha256']
                for key,h in state['objects'].items():
                    if key.startswith(level+'-'):assert h==expected['objects'][key],'Executed object differs '+key
                for key,h in state['loaded_kernels'].items():
                    if key.startswith(level+'-'):assert h==expected['loaded_kernels'][key],'Executed loaded kernel differs '+key
                for key,value in state['elfs'].items():
                    if key.startswith(level+'-'):assert value==expected['elfs'][key],'Executed ELF differs '+key
                continue
            run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Iinclude/storage/littlefs','-Isrc/process',probe/'stock.c',
                'src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','-o',d/'stock-verifier'],d/'stock-build.log')
            image=d/'filesystem.img';image.write_bytes(b''.join(bytes([0xa5+s])*512 for s in range(8))+b'\xff'*(2048*512)+b''.join(bytes([0x5a+s])*512 for s in range(8)));guards(image)
            for phase in ['writer','reader','detached','unmounted']:
                guest=d/phase;media=image if phase in ['writer','reader'] else None;before=sha(image)
                text=boot(kernels['kernel' if media else 'unavailable_kernel'],guest,phase,elfs[:6] if media else [elfs[6]],media,opt)
                evidence=observer.verify(text,phase,plans);(guest/'observations.json').write_text(json.dumps(evidence,indent=2)+'\n')
                guards(image);after=sha(image)
                if phase=='reader':assert before==after,'Cold reader altered ATA image'
                if media:
                    shutil.copyfile(image,guest/'post-guest.img');independent=run([d/'stock-verifier',image],guest/'stock.log')
                    assert independent=='STOCK NATIVE FILE INTEROPERABILITY PASS\n' and sha(image)==after,'Independent upstream reader changed disk'
                state['guests'].append({'opt':opt,'phase':phase,'evidence':evidence,'disk_before':before,'disk_after':after,'guest_log_sha256':sha(guest/'guest.log')});save()
                print('PASS NATIVE FILE '+str((kind,opt,phase)),flush=True)
        assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
        state.update(all_required_checks_pass=True,native_file_abi_guest_pass=not args.build_only,
            source_before_after_identical=True,qualification_reused=args.build_only,actual_default_llvm_objects_equal=actual)
    except BaseException as e:
        state['failure']=str(e);(out/'exception.log').write_text(traceback.format_exc())
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ['commands','inputs','objects','elfs','kernels','loaded_kernels','guests']},indent=2),flush=True)
    return 0 if state['all_required_checks_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
