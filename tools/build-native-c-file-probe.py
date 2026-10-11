#!/usr/bin/env python3
"""Build pinned LLVM regular-file providers and qualify their actual GTOS CPL3 behavior."""
import argparse,datetime,hashlib,importlib.util,json,os,pathlib,shutil,subprocess,sys,traceback

def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=pathlib.Path)
    parser.add_argument('--clang',type=pathlib.Path,required=True)
    parser.add_argument('--libcxx-source',type=pathlib.Path,required=True)
    parser.add_argument('--integer-builtins',type=pathlib.Path,required=True)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--cc',default=os.environ.get('CC','gcc'))
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args();repo=pathlib.Path(__file__).resolve().parents[1];base=repo/'apps/native_c_file';probe=base/'probe';out=args.output.resolve()
    assert not out.exists(),'Use a fresh directory; keep prior logs/cache.';out.mkdir(parents=True)
    state={'scope':'21 pinned LLVM byte-oriented regular-file providers with native callbacks; not full stdio/POSIX/libc++ streams/browser',
        'all_required_checks_pass':False,'native_c_file_guest_pass':False,'native_isolate_pass':False,'browser_guest_pass':False,
        'capacity_changed':False,'production_image_changed':False,'commands':[],'inputs':{},'objects':{},'elfs':{},'kernels':{},'loaded_kernels':{},'guests':[]}
    def save():(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
    def bind(path):
        path=pathlib.Path(path);path=(repo/path).resolve() if not path.is_absolute() else path.resolve();h=sha(path)
        if str(path) in state['inputs']:assert state['inputs'][str(path)]==h,'Source changed '+str(path);return
        state['inputs'][str(path)]=h
        try:relative=path.relative_to(repo)
        except ValueError:relative=pathlib.Path('external')/hashlib.sha256(str(path).encode()).hexdigest()/path.name
        dest=out/'source-snapshot'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
    def run(cmd,log,expected=0,timeout=180):
        cmd=list(map(str,cmd))
        with log.open('wb') as stream:
            try:code=subprocess.run(cmd,cwd=repo,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout).returncode
            except subprocess.TimeoutExpired:code=124
        state['commands'].append({'argv':cmd,'exit_code':code,'log':str(log),'log_sha256':sha(log)});save()
        if code!=expected:
            guest=log.parent/'guest.log'
            raise RuntimeError(str(log)+' exit='+str(code)+'\n'+log.read_text(errors='replace')[-4000:]+'\n'+(guest.read_text(errors='replace')[-6000:] if guest.exists() else ''))
        return log.read_text()
    def deps(path):
        for token in path.read_text().replace('\\\n',' ').split(':',1)[1].split():bind(token)
    try:
        helper_path=repo/'tools/build-native-file-probe.py';bind(helper_path)
        spec=importlib.util.spec_from_file_location('file_abi_build',helper_path);helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        spec=importlib.util.spec_from_file_location('c_file_observer',probe/'observer.py');observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)
        bind(pathlib.Path(__file__).resolve());bind(probe/'observer.py');bind(probe/'stock.c');bind(probe/'linker.ld');bind('tools/audit-kernel-instructions.py');bind('tests/native_process_smoke.ld')
        manifest=json.loads((base/'upstream-lock.json').read_text());bind(base/'upstream-lock.json')
        assert manifest['revision']=='ebe33e01982dbbf879661e3b6b78450f3020a53f'
        for name,item in manifest['upstream_files'].items():
            p=base/'llvm-libc'/name
            expected=manifest['adapted_sha256'].get(name,item['sha256'])
            assert sha(p)==expected,'Pinned file changed '+name;bind(p)
        lock=None
        if (base/'source-lock.json').exists():
            lock=json.loads((base/'source-lock.json').read_text());bind(base/'source-lock.json')
            for name,h in lock['files'].items():assert sha(repo/name)==h,'Qualified source changed '+name;bind(name)
        cxx=pathlib.Path(shutil.which(args.cxx)).resolve();cc=pathlib.Path(shutil.which(args.cc)).resolve()
        bins={n:pathlib.Path(shutil.which(n)).resolve() for n in ['as','ld','nm','objcopy','grub-mkrescue','qemu-system-i386']}
        clang=args.clang.resolve();libcxx=args.libcxx_source.resolve();string=repo/'apps/native_c_string';clock=repo/'apps/native_c_clock';legacy=repo/'apps/native_v8_runtime'
        profile=json.loads((string/'source-lock.json').read_text());clock_profile=json.loads((clock/'source-lock.json').read_text())
        assert sha(clang)==clock_profile['clang_sha256'],'Use qualified Chromium LLVM24 compiler'
        assert subprocess.check_output(['git','-C',libcxx,'rev-parse','HEAD'],text=True).strip()==clock_profile['libcxx_revision']
        bind(string/'source-lock.json');bind(clock/'source-lock.json')
        state['tools']={n:{'path':str(p),'sha256':sha(p)} for n,p in dict(bins,cxx=cxx,cc=cc,clang=clang).items()}
        state['qemu_version']=subprocess.check_output([bins['qemu-system-i386'],'--version'],text=True).splitlines()[0]
        if not args.build_only:assert 'version 8.' in state['qemu_version'],'Qualified instruction-counted QEMU8 required'
        flags=profile['qualified_native_flags']+['-DLIBC_THREAD_MODE=LIBC_THREAD_MODE_PLATFORM']
        for p in [base/'sdk',clock/'sdk',legacy/'sdk',libcxx/'include',legacy/'c-sdk',clang.parent.parent/'lib/clang/24/include']:flags+=['-isystem',str(p)]
        for p in [base/'llvm-libc',string/'llvm-libc',clock/'llvm-libc',legacy/'include',clock/'runtime',repo/'include']:flags+=['-I',str(p)]
        # Preserve old provider roots: __FILE__ in upstream assertions is part
        # of their actual object bytes even when instructions are unchanged.
        system=[]
        for p in [clock/'sdk',legacy/'sdk',libcxx/'include',legacy/'c-sdk',clang.parent.parent/'lib/clang/24/include']:system+=['-isystem',str(p)]
        string_flags=profile['qualified_native_flags']+system
        for p in [string/'llvm-libc',string/'probe',legacy/'include',clock/'runtime']:string_flags+=['-I',str(p)]
        support_flags=clock_profile['qualified_native_flags']+system
        for p in [clock/'llvm-libc',clock/'probe',legacy/'include',clock/'runtime',clock/'highway']:support_flags+=['-I',str(p)]
        policy=(repo/'tools/kernel-cxxflags').read_text().split();bind('tools/kernel-cxxflags')
        assert policy==['-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize']
        kernel_flags=['-m32','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pie','-fno-asynchronous-unwind-tables',
            '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-Itests',
            '-I'+str(probe),'-Wall','-Wextra','-Werror',*policy]
        cpp=['-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit','-Wno-write-strings']
        core=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
            'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/native_files.cpp',
            'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp',
            'src/storage/filestore.cpp','src/storage/littlefs_port.cpp','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/drivers/ata.cpp','apps/native_v8_runtime/runtime/memory.cc']
        integer=args.integer_builtins.resolve();bind(integer)
        assert sha(integer)==clock_profile['integer_builtins_sha256'],'Require pinned real integer builtins archive'
        for opt in [0,2]:
            level='O'+str(opt);d=out/level;(d/'library').mkdir(parents=True);library=[];kernel=[]
            sources=[base/'llvm-libc'/p for p in manifest['sources']]+[base/'platform_file.cpp',base/'standard_streams_unavailable.cpp']
            byte_sources=[string/'llvm-libc'/('src/wchar/' if n.startswith('w') else 'src/string/')/(n+'.cpp') for n in profile['actual_gn_objects']]
            sources+=byte_sources+[clock/'runtime/heap.cc',clock/'runtime/emutls.cc',clock/'llvm-libc/src/errno/libc_errno.cpp']
            for source in sources:
                bind(source);obj=d/'library'/(source.stem+'.o');library.append(obj)
                provider_flags=string_flags if source in byte_sources else support_flags if source.name in ['heap.cc','emutls.cc','libc_errno.cpp'] else flags
                run([clang,*provider_flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',source,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-library-'+obj.name]=sha(obj)
                if source in byte_sources or source.name in ['heap.cc','emutls.cc','libc_errno.cpp']:
                    assert sha(obj)==profile['executed_objects'][level+'-'+obj.name],'Existing runtime/provider object changed '+str(obj)
            library.append(integer)
            plans={};elfs=[]
            for mode in range(6):
                app=d/('mode'+str(mode));app.mkdir();bind(probe/'main.cc');bind('apps/native_process_info_probe/start.s')
                run([clang,*flags,'-I',probe,'-O'+str(opt),'-DGTOS_FILE_MODE='+str(mode),'-MD','-MF',app/'main.d','-c',probe/'main.cc','-o',app/'main.o'],app/'main.log');deps(app/'main.d')
                run([bins['as'],'--32','apps/native_process_info_probe/start.s','-o',app/'start.o'],app/'start.log')
                run([bins['ld'],'-melf_i386','--gc-sections','-T',probe/'linker.ld','-Map',app/'user.map','-o',app/'user.elf',app/'start.o',app/'main.o',*library],app/'link.log')
                assert not run([bins['nm'],'-u',app/'user.elf'],app/'undefined.log').strip()
                run([bins['objcopy'],'--strip-all',app/'user.elf',app/'user.stripped.elf'],app/'strip.log')
                plans[mode]=observer.geometry(app/'user.stripped.elf',mode)
                state['elfs'][level+'-mode'+str(mode)]={'sha256':sha(app/'user.stripped.elf'),**plans[mode]};elfs.append(app/'user.stripped.elf')
            for name in core:
                src=repo/name;bind(src);obj=d/('byte_helpers.o' if name.endswith('/memory.cc') else pathlib.Path(name).stem+'.o');kernel.append(obj)
                options=['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h'] if name.endswith('.c') else cpp
                run([cxx,*kernel_flags,*options,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-kernel-'+obj.name]=sha(obj)
            for source,name in [('tests/native_process_loader.s','loader.o'),('src/process/native_fp.s','native_fp_asm.o'),('src/hardwarecommunication/interruptstubs.s','stubs.o')]:
                bind(source);obj=d/name;kernel.append(obj);run([bins['as'],'--32',source,'-o',obj],d/(name+'.log'));state['objects'][level+'-kernel-'+name]=sha(obj)
            bind(probe/'kernel.cpp');obj=d/'probe.o';kernel.append(obj)
            run([cxx,*kernel_flags,*cpp,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',probe/'kernel.cpp','-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
            state['objects'][level+'-kernel-'+obj.name]=sha(obj)
            binary=d/'kernel.bin'
            run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',binary,*kernel],d/'kernel-link.log')
            assert not run([bins['nm'],'-u',binary],d/'kernel-undefined.log').strip()
            run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,binary],d/'kernel-audit.log')
            state['kernels'][level]=sha(binary);state['loaded_kernels'][level]=helper.loaded_sha(binary)
            if args.build_only:
                assert lock,'Build-only needs completed source lock'
                assert state['loaded_kernels'][level]==lock['loaded_kernels'][level]
                for key,h in state['objects'].items():
                    if key.startswith(level+'-'):assert h==lock['objects'][key],key
                for key,v in state['elfs'].items():
                    if key.startswith(level+'-'):assert v==lock['elfs'][key],key
                continue
            guest=d/'guest';(guest/'iso/boot/grub').mkdir(parents=True);shutil.copyfile(binary,guest/'iso/boot/native.bin')
            for mode,elf in enumerate(elfs):shutil.copyfile(elf,guest/'iso/boot'/('file'+str(mode)+'.elf'))
            (guest/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native LLVM FILE" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/file'+str(mode)+'.elf\n' for mode in range(6))+' boot\n}\n')
            run([bins['grub-mkrescue'],'--output='+str(guest/'native.iso'),guest/'iso'],guest/'grub.log')
            image=guest/'filesystem.img';image.write_bytes(b''.join(bytes([0xa5+s])*512 for s in range(8))+b'\xff'*(2048*512)+b''.join(bytes([0x5a+s])*512 for s in range(8)));helper.guards(image)
            mib=32 if opt==0 else 64
            run([bins['qemu-system-i386'],'-L',os.environ.get('GTOS_QEMU_DATA_DIR','/usr/share/qemu'),'-machine','pc','-accel','tcg,thread=single',
                '-icount','shift=0,align=off,sleep=off','-cpu','max','-rtc','base=2000-01-01T00:00:00,clock=vm','-m',str(mib),'-smp','1' if opt==0 else '4',
                '-cdrom',guest/'native.iso','-boot','d','-drive','file='+str(image)+',format=raw,if=ide,index=0','-nic','none','-display','none','-monitor','none','-serial','none',
                '-debugcon','file:'+str(guest/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',33,240)
            text=(guest/'guest.log').read_text();evidence=observer.verify(text,plans,mib);controls=observer.negative_controls(text,plans,mib)
            (guest/'observations.json').write_text(json.dumps(evidence,indent=2)+'\n');helper.guards(image);before=sha(image)
            run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Iinclude/storage/littlefs','-Isrc/process',probe/'stock.c','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','-o',d/'stock-reader'],d/'stock-build.log')
            assert run([d/'stock-reader',image],guest/'stock.log')=='STOCK C FILE INTEROPERABILITY PASS\n' and sha(image)==before
            state['guests'].append({'opt':opt,'memory_mib':mib,'evidence':evidence,'negative_observer_controls':controls,'disk_sha256':before,'guest_log_sha256':sha(guest/'guest.log')});save()
            print('PASS TRUE NATIVE C FILE '+level,flush=True)
        assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
        state.update(all_required_checks_pass=True,native_c_file_guest_pass=not args.build_only,source_before_after_identical=True,qualification_reused=args.build_only)
    except BaseException as error:
        state['failure']=str(error);(out/'exception.log').write_text(traceback.format_exc())
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ['commands','inputs','objects','elfs','kernels','guests']},indent=2),flush=True)
    return 0 if state['all_required_checks_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
