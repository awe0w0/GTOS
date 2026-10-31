#!/usr/bin/env python3
"""Build pinned LLVM regular-file providers and qualify their actual GTOS CPL3 behavior."""
import argparse,datetime,hashlib,importlib.util,json,os,pathlib,shutil,struct,subprocess,sys,traceback

def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def elf32_sections(data):
    assert data[:5]==b'\x7fELF\x01', 'not a 32-bit ELF object'
    shoff,=struct.unpack_from('<I',data,0x20);shentsize,shnum,shstrndx=struct.unpack_from('<HHH',data,0x2e)
    raw=[]
    for index in range(shnum):
        base=shoff+index*shentsize
        name,kind,flags,address,offset,size=struct.unpack_from('<IIIIII',data,base)
        raw.append((name,kind,flags,offset,size))
    string_table=raw[shstrndx];names=data[string_table[3]:string_table[3]+string_table[4]]
    sections={}
    for name,kind,flags,offset,size in raw:
        end=names.index(b'\0',name)
        sections[names[name:end].decode('utf-8','replace')]=(kind,flags,offset,size)
    return sections


def elf32_identity(data):
    """The parts of a relocatable object that are not section content: the header fields that describe the ABI."""
    assert data[:5]==b'\x7fELF\x01', 'not a 32-bit ELF object'
    kind,machine,version,entry,programs,shoff,flags=struct.unpack_from('<HHIIIII',data,0x10)
    return {'ident':data[:16],'type':kind,'machine':machine,'version':version,'entry':entry,'phoff':programs,'flags':flags,
            'ehsize':struct.unpack_from('<H',data,0x28)[0],'phentsize':struct.unpack_from('<H',data,0x2a)[0],
            'phnum':struct.unpack_from('<H',data,0x2c)[0],'shentsize':struct.unpack_from('<H',data,0x2e)[0],
            'shnum':struct.unpack_from('<H',data,0x30)[0],'shstrndx':struct.unpack_from('<H',data,0x32)[0]}


def elf32_load_ranges(path):
    """The file ranges of the PT_LOAD segments, so a change can be attributed to loaded content or to table metadata."""
    data=pathlib.Path(path).read_bytes();assert data[:4]==b'\x7fELF' and data[4]==1 and data[5]==1,'Not a 32-bit little-endian ELF: '+str(path)
    phoff=struct.unpack_from('<I',data,28)[0];phentsize=struct.unpack_from('<H',data,42)[0];phnum=struct.unpack_from('<H',data,44)[0]
    assert phoff and phentsize==32 and phnum,'No program header table: '+str(path)
    ranges=[]
    for index in range(phnum):
        base=phoff+index*phentsize
        kind=struct.unpack_from('<I',data,base)[0]
        if kind!=1:continue
        offset=struct.unpack_from('<I',data,base+4)[0];filesz=struct.unpack_from('<I',data,base+16)[0]
        ranges.append((offset,offset+filesz))
    assert ranges,'No PT_LOAD segment: '+str(path)
    return tuple(sorted(ranges))

# The rig's clock is the guest's clock, and the consumer is told what it is: -rtc base=2000-01-01T00:00:00,clock=vm means the guest starts
# at this epoch second, so a time() reading is checked against the command line that boots it instead of against a guessed date.
RTC_BASE='base=2000-01-01T00:00:00,clock=vm'
RTC_EPOCH=946684800

def kernel_facts(path,run_token):
    """Read a kernel: its bytes, its loadable ranges, where the run directory sits, and the hash with that directory replaced."""
    data=pathlib.Path(path).read_bytes();ranges=elf32_load_ranges(path)
    offsets=[];start=0
    while True:
        found=data.find(run_token,start)
        if found<0:break
        offsets.append(found);start=found+1
    loaded_refs=[offset for offset in offsets if any(begin<=offset<end for begin,end in ranges)]
    # The placeholder keeps the run directory's length: a shorter one would shift every byte after it, so two builds of the same sources in
    # differently named run roots would compare unequal for a reason that has nothing to do with the kernel.
    placeholder=b'<run>/'+b'#'*(len(run_token)-6)
    assert len(placeholder)==len(run_token)
    return {'bytes':data,'load_ranges':ranges,'run_ref_offsets':offsets,'run_refs_in_loaded_segments':loaded_refs,
            'normalized_sha256':hashlib.sha256(data.replace(run_token,placeholder)).hexdigest()}

def compare_kernel(facts,level,frozen):
    """Return the rule that justified a changed kernel file, or raise when the change is not the run directory.

    The loaded image is compared on the real file. The whole-file hash may differ only because the linker writes the object paths into tables
    that are not loaded; those occurrences are checked by count, by region and by the length the different path adds.
    """
    assert not facts['run_refs_in_loaded_segments'],'The run directory is inside a loaded segment: '+level+' '+str(facts['run_refs_in_loaded_segments'])
    metadata=frozen['metadata']
    if metadata is None:
        assert facts['normalized_sha256']==frozen['normalized'],'Kernel changed beyond the run directory: '+level
        return 'normalized-only (the frozen record has no offset table)'
    assert facts['load_ranges']==tuple(tuple(each) for each in metadata['load_ranges']),'Kernel segment layout changed: '+level
    recorded=metadata['run_ref_offsets'];assert len(facts['run_ref_offsets'])==len(recorded),'Kernel run-directory occurrences changed: '+level
    assert not metadata['run_refs_in_loaded_segments'],'The frozen kernel held the run directory in a loaded segment: '+level
    assert facts['normalized_sha256']==frozen['normalized'],'Kernel changed beyond the run directory: '+level
    # The tables follow the last loadable segment; an occurrence anywhere else would be content the loader maps.
    boundary=facts['load_ranges'][-1][1]
    assert all(offset>=boundary for offset in facts['run_ref_offsets']),'Kernel run directory moved into a loaded region: '+level
    assert all(offset>=metadata['load_ranges'][-1][1] for offset in recorded),'The frozen kernel held the run directory in a loaded region: '+level
    grown=len(facts['bytes'])-frozen['bytes']
    assert grown==len(recorded)*(len(facts['run_token'])-frozen['token_length']),'Kernel length change is not the run directory: '+level
    return 'same path count outside the loadable segments, length change explained by the path'

def provider_identity(rebuilt_path,qualified_path,want,roots):
    """Compare a rebuilt provider object with the recorded one.

    Identical bytes pass. Otherwise every section must be identical, except sections whose content is exactly the recorded content with the
    recorded source root replaced by this checkout's root; those sections are returned so the record can name them.
    """
    data=pathlib.Path(rebuilt_path).read_bytes()
    if hashlib.sha256(data).hexdigest()==want:return True,[]
    qualified=pathlib.Path(qualified_path)
    assert qualified.is_file(),'No recorded object to compare with at '+str(qualified)
    reference=qualified.read_bytes()
    assert hashlib.sha256(reference).hexdigest()==want,'The recorded object does not match the recorded identity: '+str(qualified)
    build_root,recorded_root=roots
    recorded_header=elf32_identity(reference);rebuilt_header=elf32_identity(data)
    assert recorded_header==rebuilt_header,'ELF header changed: '+str(sorted(k for k in recorded_header if recorded_header[k]!=rebuilt_header[k]))
    left=elf32_sections(reference);right=elf32_sections(data)
    assert set(left)==set(right),'Section set changed: '+str(sorted(set(left)^set(right)))
    replaced=[]
    for label in sorted(left):
        kind,flags,offset,size=left[label];kind2,flags2,offset2,size2=right[label]
        assert kind==kind2,'Section kind changed: '+label
        assert flags==flags2,'Section flags changed: '+label
        if kind==8:
            assert size==size2,'NOBITS section size changed: '+label+' '+str(size)+' -> '+str(size2)
            continue
        recorded=reference[offset:offset+size];rebuilt=data[offset2:offset2+size2]
        if recorded==rebuilt:continue
        if recorded.replace(recorded_root.encode(),build_root.encode())==rebuilt:replaced.append(label);continue
        return False,[]
    assert replaced,'The object differs from the recorded one outside any section'
    return True,replaced
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=pathlib.Path)
    parser.add_argument('--clang',type=pathlib.Path,required=True)
    parser.add_argument('--libcxx-source',type=pathlib.Path,required=True)
    parser.add_argument('--integer-builtins',type=pathlib.Path,required=True)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--cc',default=os.environ.get('CC','gcc'))
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--source-manifest',type=pathlib.Path)
    parser.add_argument('--expected-deltas',type=pathlib.Path)
    parser.add_argument('--macro-prefix-map')
    parser.add_argument('--tool-drift')
    parser.add_argument('--qualified-object-root',type=pathlib.Path)
    parser.add_argument('--entry-head')
    parser.add_argument('--guests-only',action='store_true')
    parser.add_argument('--libxml-root')
    parser.add_argument('--libxml-config')
    parser.add_argument('--private-subset')
    parser.add_argument('--printf-runtime')
    parser.add_argument('--native-file-library')
    parser.add_argument('--published-root',type=pathlib.Path)
    args=parser.parse_args();modules=6+(1 if args.libxml_root else 0);repo=pathlib.Path(__file__).resolve().parents[1];base=repo/'apps/native_c_file';probe=base/'probe';out=args.output.resolve()
    assert not out.exists(),'Use a fresh directory; keep prior logs/cache.';out.mkdir(parents=True)
    manifest_path=args.source_manifest.resolve() if args.source_manifest is not None else None
    candidate=json.loads(manifest_path.read_text()) if manifest_path is not None and manifest_path.exists() else None
    recorded={'objects':{},'elfs':{},'kernels':{},'loaded_kernels':{},'kernels_normalized':{},'kernel_run_refs':{},
              'kernels_bytes':{},'kernel_paths':{},'kernel_metadata':{}};provider_objects={};path_only_kernels=[]
    run_token=(str(out) + '/').encode()
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
        lock=None;use_candidate=manifest_path is not None;candidate_files=None;drift={}
        if (base/'source-lock.json').exists():
            lock=json.loads((base/'source-lock.json').read_text());bind(base/'source-lock.json')
            legacy=lock['files']
            if use_candidate:
                if candidate is None:
                    candidate_files={name:sha(repo/name) for name in legacy}
                    drift={name:h for name,h in candidate_files.items() if h!=legacy[name]}
                    assert args.expected_deltas,'A frozen candidate must name the current-tree deltas it accepts'
                    bind(args.expected_deltas.resolve())
                    expected=json.loads(args.expected_deltas.resolve().read_text())
                    named={entry['relative']:entry['current']['sha256'] for entry in expected['differences']}
                    tool=set(t for t in (args.tool_drift or '').split(',') if t)
                    assert all(t.startswith('tools/') for t in tool),'Only build tools may drift inside the round'
                    unexpected=sorted(set(drift)-set(named)-tool);assert not unexpected,'Unexpected current-tree drift: '+str(unexpected)
                    absent=sorted(set(named)-set(drift));assert not absent,'A named delta is no longer present: '+str(absent)
                    wrong=sorted(n for n in set(drift)&set(named)-tool if drift[n]!=named[n])
                    assert not wrong,'A named delta does not match its recorded identity: '+str(wrong)
                else:
                    assert candidate['schema']=='gtos-native-c-file-current-candidate/1','Not a current-source candidate manifest'
                    assert candidate['derived_from_lock_sha256']==sha(base/'source-lock.json'),'The candidate was derived from a different lock'
                    candidate_files=candidate['files']
                    assert set(candidate_files)==set(legacy),'The candidate does not cover the pinned set'
                    drift={name:h for name,h in candidate_files.items() if h!=legacy[name]}
                print('CANDIDATE DELTAS '+str(len(drift))+' of '+str(len(legacy)),flush=True)
            for name,h in (candidate_files or legacy).items():
                assert sha(repo/name)==h,'Qualified source changed '+name;bind(name)
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
        alias=(['-fmacro-prefix-map='+args.macro_prefix_map,'-ffile-prefix-map='+args.macro_prefix_map]
               if args.macro_prefix_map else [])
        flags=profile['qualified_native_flags']+['-DLIBC_THREAD_MODE=LIBC_THREAD_MODE_PLATFORM']+alias
        for p in [base/'sdk',clock/'sdk',legacy/'sdk',libcxx/'include',legacy/'c-sdk',clang.parent.parent/'lib/clang/24/include']:flags+=['-isystem',str(p)]
        for p in [base/'llvm-libc',string/'llvm-libc',clock/'llvm-libc',legacy/'include',clock/'runtime',repo/'include']:flags+=['-I',str(p)]
        # Preserve old provider roots: __FILE__ in upstream assertions is part
        # of their actual object bytes even when instructions are unchanged.
        system=[]
        for p in [clock/'sdk',legacy/'sdk',libcxx/'include',legacy/'c-sdk',clang.parent.parent/'lib/clang/24/include']:system+=['-isystem',str(p)]
        string_flags=profile['qualified_native_flags']+system+alias
        for p in [string/'llvm-libc',string/'probe',legacy/'include',clock/'runtime']:string_flags+=['-I',str(p)]
        support_flags=clock_profile['qualified_native_flags']+system+alias
        for p in [clock/'llvm-libc',clock/'probe',legacy/'include',clock/'runtime',clock/'highway']:support_flags+=['-I',str(p)]
        policy=(repo/'tools/kernel-cxxflags').read_text().split();bind('tools/kernel-cxxflags')
        assert policy==['-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize']
        kernel_flags=['-m32','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pie','-fno-asynchronous-unwind-tables',
            '-ffunction-sections','-fdata-sections','-fstack-usage','-Iinclude','-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-Itests',
            '-I'+str(probe),'-Wall','-Wextra','-Werror',*policy]
        cpp=['-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit','-Wno-write-strings']
        core=['src/gdt.cpp','src/multitasking.cpp','src/syscalls.cpp','src/hardwarecommunication/interrupts.cpp','src/hardwarecommunication/port.cpp',
            'src/process/native_windows.cpp',
            'src/process/native_runtime.cpp','src/process/native_realtime.cpp','src/process/resources.cpp','src/process/native_surface.cpp','src/process/native_fp.cpp','src/process/native_files.cpp',
            'src/process/elf32.cpp','src/memory/process_address_space.cpp','src/memory/paging.cpp','src/memory/physical.cpp','src/memory/bootstrap.cpp',
            'src/storage/filestore.cpp','src/storage/littlefs_port.cpp','src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/drivers/ata.cpp','apps/native_v8_runtime/runtime/memory.cc']
        integer=args.integer_builtins.resolve();bind(integer)
        assert sha(integer)==clock_profile['integer_builtins_sha256'],'Require pinned real integer builtins archive'
        def build_libxml_module(d,opt):
            """Compile the pinned libxml2 consumer and link it as the seventh guest module."""
            import re
            out=d/'libxml';out.mkdir(parents=True,exist_ok=True)
            src=pathlib.Path(args.libxml_root);assert src.is_dir(),'The pinned libxml sources are missing: '+str(src)
            buildGn=pathlib.Path(re.sub(r'/src$','',str(src))+'/BUILD.gn').read_text()
            sources=[name for name in re.findall(r'^[ \t]*"src/([A-Za-z0-9_]+\.c)",',buildGn,re.M)]
            assert len(sources)>=20,'The BUILD.gn source list is not the pinned one: '+str(len(sources))
            cfg=pathlib.Path(args.libxml_config);assert cfg.is_dir(),'The libxml configuration is missing: '+str(cfg)
            subset=pathlib.Path(args.private_subset)
            clangInclude=str(clang.parent.parent/'lib/clang/24/include')
            xmlflags=['--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-fno-unwind-tables',
                '-fno-asynchronous-unwind-tables','-mno-mmx','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-nostdinc',
                '-isystem',str(legacy/'sdk'),'-isystem',str(libcxx/'include'),'-isystem',str(legacy/'c-sdk'),'-isystem',clangInclude,
                '-D__GTOS__=1','-DLIBXML_STATIC=','-femulated-tls','-O'+str(opt),'-I'+str(subset/'include'),'-I'+str(repo/'include'),
                '-I'+str(cfg),'-I'+str(cfg/'include'),'-I'+str(src/'include'),'-I'+str(src)]
            compiled=0
            for name in sources:
                run([clang,'-x','c','-std=c99',*xmlflags,'-MD','-MF',out/(name+'.d'),'-c',src/name,'-o',out/(name+'.o')],out/(name+'.err'));compiled+=1
            for name in ('posix_file.cpp','link_closure/libc_closure.cpp','link_closure/math_closure.cpp','probe/main.cpp'):
                source=subset/name
                # The consumer compares time() with RTC_EPOCH, which is the epoch of the -rtc base in RTC_BASE above; the definition is
                # derived from that one constant so the two cannot drift apart.
                extra=['-DGTOS_RTC_EPOCH=%d'%RTC_EPOCH] if name=='probe/main.cpp' else []
                run([clang,*xmlflags,*extra,'-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-c',source,'-o',out/(source.name+'.o')],out/(source.name+'.err'));compiled+=1
            startObj=out/'start.o'
            run([bins['as'],'--32',subset/'probe/exit-start.s','-o',startObj],out/'start.log')
            # Exactly the seven objects the printf unit qualified: the tree also holds heap.o and libc_errno.o, which the FILE library
            # already links, and linking both would define malloc twice.
            measured=['2accc7918144ae071581dce79aec5cd991e13225a90a0ffb9243e62869a0cb4d/native_printf_libc.snprintf.o',
                '9c873c85e6f0c9915e81bceb27b1d99e03b87915bb879557d8f1ac6ee196053a/native_printf_libc.vsnprintf.o',
                '6d0c352e0dbca07f2e6fe9e76c41b83237a82af690c499c56c30c7a68d268f3b/native_printf_libc.snprintf_modular.o',
                '69a74036b6699827904d5dd4486d57ed110bf421ea0c66dd0c3f01e4c9d64b61/native_printf_libc.vsnprintf_modular.o',
                '84e54bb3d7b4e446632cbac526b74b45d4157fc7b720de60ca4736771e08be27/native_printf_libc.float_impl.o',
                '636c90b9b60b18437ad2629e3d9c41905a1f6044cfa58aef189a092cf6adc6c0/native_printf_libc.error_to_string.o',
                '5fe56d41fe59c161b496966e32fc6303bcd94aec1e4b16ba73e3bfa47370d2f5/native_runtime.abort.o']
            printfObjects=[pathlib.Path(args.printf_runtime)/name for name in measured]
            missing=[str(pathlib.Path(args.printf_runtime)/name) for name in measured if not (pathlib.Path(args.printf_runtime)/name).is_file()]
            assert not missing,'The measured printf runtime is incomplete: '+str(missing)
            library=sorted(pathlib.Path(args.native_file_library).glob('*.o'))
            assert len(library)>=10,'The qualified native FILE library is incomplete: '+str(len(library))
            elf=d/'user.elf';strip=d/'user.stripped.elf'
            objects=[obj for obj in sorted(out.glob('*.o')) if obj!=startObj]
            run([bins['ld'],'-melf_i386','-static','-T',subset/'probe/linker.ld','-o',elf,startObj,*objects,*library,*printfObjects],out/'link.log')
            undefined=run([bins['nm'],'-u',elf],out/'undefined.log').strip()
            assert not undefined,'The libxml consumer has undefined symbols:\n'+undefined
            run([bins['objcopy'],'--strip-all',elf,strip],out/'strip.log')
            symbols=run([bins['nm'],'-C',elf],out/'symbols.log')
            assert ' T xmlReadMemory' in symbols and ' T xmlReadFile' in symbols,'The parser the consumer calls was not linked in'
            print('LIBXML MODULE objects='+str(compiled)+' library='+str(len(library))+' printf='+str(len(printfObjects)),flush=True)
            return strip

        def prepare_images(d,binary,elfs):
            """Write the ISO and the disk image. This runs in the build stage, which boots nothing."""
            guest=d/'guest';(guest/'iso/boot/grub').mkdir(parents=True,exist_ok=True);shutil.copyfile(binary,guest/'iso/boot/native.bin')
            for mode,elf in enumerate(elfs):shutil.copyfile(elf,guest/'iso/boot'/('file'+str(mode)+'.elf'))
            (guest/'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native LLVM FILE" {\n multiboot /boot/native.bin\n'+''.join(' module /boot/file'+str(mode)+'.elf\n' for mode in range(modules))+' boot\n}\n')
            grubModules=pathlib.Path(os.environ['GTOS_GRUB_MODULES']) if os.environ.get('GTOS_GRUB_MODULES') else \
                    bins['grub-mkrescue'].parent.parent/'lib/grub/i386-pc'
            rescue=[bins['grub-mkrescue']]+(['--directory='+str(grubModules)] if grubModules.is_dir() else [])
            run(rescue+['--output='+str(guest/'native.iso'),guest/'iso'],guest/'grub.log')
            volume=(guest/'native.iso').read_bytes()[17*2048:17*2048+6]
            assert volume[0]==0 and volume[1:6]==b'CD001','The guest image has no El Torito boot record: '+str(guest/'native.iso')
            image=guest/'filesystem.img'
            image.write_bytes(b''.join(bytes([0xa5+s])*512 for s in range(8))+b'\xff'*(2048*512)+b''.join(bytes([0x5a+s])*512 for s in range(8)))
            helper.guards(image)
            return guest/'native.iso',image

        def boot(level,opt,d,iso,image_source,reader,plans):
            """Boot one published image and cross-check the disk with the published reader."""
            guest=d/'guest';guest.mkdir(parents=True,exist_ok=True)
            image=guest/'filesystem.img';shutil.copyfile(image_source,image)
            assert sha(image)==sha(image_source),'The working copy is not the published image: '+str(image_source)
            mib=32 if opt==0 else 64
            run([bins['qemu-system-i386'],'-L',os.environ.get('GTOS_QEMU_DATA_DIR','/usr/share/qemu'),'-machine','pc','-accel','tcg,thread=single',
                '-icount','shift=0,align=off,sleep=off','-cpu','max','-rtc',RTC_BASE,'-m',str(mib),'-smp','1' if opt==0 else '4',
                '-cdrom',str(iso),'-boot','d','-drive','file='+str(image)+',format=raw,if=ide,index=0','-nic','none','-display','none','-monitor','none','-serial','none',
                '-debugcon','file:'+str(guest/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],guest/'host.log',33,240)
            text=(guest/'guest.log').read_text();evidence=observer.verify(text,plans,mib);controls=observer.negative_controls(text,plans,mib)
            (guest/'observations.json').write_text(json.dumps(evidence,indent=2)+'\n');helper.guards(image);before=sha(image)
            assert run([str(reader),str(image)],guest/'stock.log')=='STOCK C FILE INTEROPERABILITY PASS\n' and sha(image)==before
            state['guests'].append({'opt':opt,'memory_mib':mib,'evidence':evidence,'negative_observer_controls':controls,
                                    'disk_sha256':before,'guest_log_sha256':sha(guest/'guest.log'),
                                    'image_source':str(image_source),'reader':str(reader)});save()
            print('PASS TRUE NATIVE C FILE '+level,flush=True)

        for opt in [0,2]:
            level='O'+str(opt);d=out/level;(d/'library').mkdir(parents=True);library=[];kernel=[]
            if args.guests_only:
                assert args.published_root is not None,'Guests-only needs the published artifact root'
                assert candidate is not None,'Guests-only compares the published images against the frozen candidate'
                plans={mode:{k:v for k,v in candidate['elfs'][level+'-mode'+str(mode)].items() if k!='sha256'} for mode in range(modules)}
                published=args.published_root/level
                entry=json.loads((args.published_root/'published.json').read_text())['artifacts'][level]
                for name,key in [('native.iso','iso'),('filesystem.img','image'),('stock-reader','reader')]:
                    path=published/name
                    assert path.is_file(),'The published '+name+' is missing: '+str(path)
                    assert sha(path)==entry[key]['sha256'],'The published '+name+' is not the copy that was recorded: '+str(path)
                    assert path.stat().st_size==entry[key]['bytes'],'The published '+name+' changed size: '+str(path)
                boot(level,opt,d,published/'native.iso',published/'filesystem.img',published/'stock-reader',plans)
                continue

            sources=[base/'llvm-libc'/p for p in manifest['sources']]+[base/'platform_file.cpp',base/'standard_streams_unavailable.cpp']
            byte_sources=[string/'llvm-libc'/('src/wchar/' if n.startswith('w') else 'src/string/')/(n+'.cpp') for n in profile['actual_gn_objects']]
            sources+=byte_sources+[clock/'runtime/heap.cc',clock/'runtime/emutls.cc',clock/'llvm-libc/src/errno/libc_errno.cpp']
            for source in sources:
                bind(source);obj=d/'library'/(source.stem+'.o');library.append(obj)
                provider_flags=string_flags if source in byte_sources else support_flags if source.name in ['heap.cc','emutls.cc','libc_errno.cpp'] else flags
                run([clang,*provider_flags,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',source,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-library-'+obj.name]=sha(obj)
                if source in byte_sources or source.name in ['heap.cc','emutls.cc','libc_errno.cpp']:
                    key=level+'-'+obj.name;want=profile['executed_objects'][key]
                    if use_candidate and args.qualified_object_root is not None:
                        recorded_object=args.qualified_object_root.resolve()/level/'library'/obj.name
                        same,replaced=provider_identity(obj,recorded_object,want,tuple(args.macro_prefix_map.split('=',1)))
                        assert same,'Provider object changed beyond the recorded source root: '+str(obj)
                        if replaced:state.setdefault('provider_root_substitutions',{})[key]=replaced
                    else:
                        assert sha(obj)==want,'Existing runtime/provider object changed '+str(obj)
                    if use_candidate:provider_objects[key]=sha(obj)
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
            if modules>6:
                # The real libxml consumer, compiled from the pinned sources in this run, is the seventh guest module.
                app=d/'mode6';app.mkdir();bind(args.private_subset+'/probe/main.cpp');bind(args.private_subset+'/probe/exit-start.s')
                bind(args.private_subset+'/probe/linker.ld')
                strip=build_libxml_module(app,opt)
                plans[6]=observer.geometry(strip,6)
                state['elfs'][level+'-mode6']={'sha256':sha(strip),**plans[6]};elfs.append(strip)
            for name in core:
                src=repo/name;bind(src);obj=d/('byte_helpers.o' if name.endswith('/memory.cc') else pathlib.Path(name).stem+'.o');kernel.append(obj)
                options=['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h'] if name.endswith('.c') else cpp
                run([cxx,*kernel_flags,*options,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',src,'-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
                state['objects'][level+'-kernel-'+obj.name]=sha(obj)
            for source,name in [('tests/native_process_loader.s','loader.o'),('src/process/native_fp.s','native_fp_asm.o'),('src/hardwarecommunication/interruptstubs.s','stubs.o')]:
                bind(source);obj=d/name;kernel.append(obj);run([bins['as'],'--32',source,'-o',obj],d/(name+'.log'));state['objects'][level+'-kernel-'+name]=sha(obj)
            bind(probe/'pure_virtual.cpp');obj=d/'pure_virtual.o';kernel.append(obj)
            run([cxx,*kernel_flags,*cpp,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',probe/'pure_virtual.cpp','-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
            state['objects'][level+'-kernel-'+obj.name]=sha(obj)
            bind(probe/'kernel.cpp');obj=d/'probe.o';kernel.append(obj)
            run([cxx,*kernel_flags,*cpp,'-O'+str(opt),'-MD','-MF',obj.with_suffix('.d'),'-c',probe/'kernel.cpp','-o',obj],obj.with_suffix('.log'));deps(obj.with_suffix('.d'))
            state['objects'][level+'-kernel-'+obj.name]=sha(obj)
            binary=d/'kernel.bin'
            run([bins['ld'],'-melf_i386','--gc-sections','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',binary,*kernel],d/'kernel-link.log')
            assert not run([bins['nm'],'-u',binary],d/'kernel-undefined.log').strip()
            run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,binary],d/'kernel-audit.log')
            state['kernels'][level]=sha(binary);state['loaded_kernels'][level]=helper.loaded_sha(binary)
            level_objects={k:v for k,v in state['objects'].items() if k.startswith(level+'-')}
            level_elfs={k:v for k,v in state['elfs'].items() if k.startswith(level+'-')}
            if use_candidate:
                facts=kernel_facts(binary,run_token);facts['run_token']=run_token
                kernel_bytes=facts['bytes'];normalized=facts['normalized_sha256']
                metadata={'load_ranges':[list(each) for each in facts['load_ranges']],'run_ref_offsets':facts['run_ref_offsets'],
                          'run_refs_in_loaded_segments':facts['run_refs_in_loaded_segments'],'normalized_sha256':normalized}
                assert not facts['run_refs_in_loaded_segments'],'The run directory is inside a loaded segment: '+level
                if candidate is None:
                    recorded['objects'].update(level_objects);recorded['elfs'].update(level_elfs)
                    recorded['kernels'][level]=state['kernels'][level];recorded['loaded_kernels'][level]=state['loaded_kernels'][level]
                    recorded['kernels_normalized'][level]=normalized;recorded['kernels_bytes'][level]=len(kernel_bytes)
                    recorded['kernel_run_refs'][level]=len(facts['run_ref_offsets']);recorded['kernel_paths'][level]=str(binary)
                    recorded['kernel_metadata'][level]=metadata
                else:
                    # The loaded image is hashed from the real file; nothing is normalised before that comparison.
                    assert state['loaded_kernels'][level]==candidate['loaded_kernels'][level],'Loaded kernel changed since the freeze: '+level
                    if state['kernels'][level]!=candidate['kernels'][level]:
                        frozen_record={'metadata':(candidate.get('kernel_metadata') or {}).get(level),
                                       'normalized':candidate['kernels_normalized'][level],
                                       'bytes':candidate['kernels_bytes'][level],
                                       'token_length':len((candidate['run_root'] + '/').encode())}
                        metadata_rule=compare_kernel(facts,level,frozen_record)
                        path_only_kernels.append(level)
                        state.setdefault('kernel_metadata_rule',{})[level]=metadata_rule
                    for key,h in level_objects.items():assert h==candidate['objects'][key],'Object changed since the freeze: '+key
                    for key,v in level_elfs.items():assert v==candidate['elfs'][key],'ELF changed since the freeze: '+key
                state.setdefault('kernel_metadata',{})[level]=metadata
            reader=d/'stock-reader'
            run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Iinclude/storage/littlefs','-Isrc/process',probe/'stock.c',
                 'src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','-o',reader],d/'stock-build.log')
            iso,image=prepare_images(d,binary,elfs)
            state.setdefault('artifacts',{})[level]={'iso':{'path':str(iso),'sha256':sha(iso),'bytes':iso.stat().st_size},
                                                     'image':{'path':str(image),'sha256':sha(image),'bytes':image.stat().st_size},
                                                     'reader':{'path':str(reader),'sha256':sha(reader),'bytes':reader.stat().st_size},
                                                     'kernel':{'path':str(binary),'sha256':sha(binary),'bytes':binary.stat().st_size},
                                                     'elfs':[{'path':str(elf),'sha256':sha(elf),'bytes':elf.stat().st_size} for elf in elfs]};save()
            if args.build_only:
                if not use_candidate:
                    assert lock,'Build-only needs completed source lock'
                    assert state['loaded_kernels'][level]==lock['loaded_kernels'][level]
                    for key,h in level_objects.items():assert h==lock['objects'][key],key
                    for key,v in level_elfs.items():assert v==lock['elfs'][key],key
                continue
        if use_candidate and candidate is None:
            manifest_path.write_text(json.dumps({
                'schema':'gtos-native-c-file-current-candidate/1',
                'derived_from_lock_sha256':sha(base/'source-lock.json'),
                'lock_path':str(base/'source-lock.json'),
                'entry_head':args.entry_head or 'unrecorded',
                'deltas':sorted(drift),'tool_drift':sorted(set(drift)&set(t for t in (args.tool_drift or '').split(',') if t)),
                'files':candidate_files,'provider_objects':provider_objects,
                'run_root':str(out),
                'provider_root_substitutions':state.get('provider_root_substitutions',{}),
                'qualified_object_root':str(args.qualified_object_root.resolve()) if args.qualified_object_root else None,
                'objects':recorded['objects'],'elfs':recorded['elfs'],'kernels':recorded['kernels'],
                'kernels_normalized':recorded['kernels_normalized'],'kernels_bytes':recorded['kernels_bytes'],
                'kernel_run_refs':recorded['kernel_run_refs'],'kernel_metadata':recorded['kernel_metadata'],
                'loaded_kernels':recorded['loaded_kernels'],'tools':state['tools'],
                'macro_prefix_map':args.macro_prefix_map,
                'objects_equal_legacy':{k:(v==lock['objects'].get(k)) for k,v in recorded['objects'].items()},
                'elfs_equal_legacy':{k:(v==lock['elfs'].get(k)) for k,v in recorded['elfs'].items()},
                'kernels_equal_legacy':{k:(v==lock.get('kernels',{}).get(k)) for k,v in recorded['kernels'].items()},
                'loaded_kernels_equal_legacy':{k:(v==lock['loaded_kernels'].get(k)) for k,v in recorded['loaded_kernels'].items()},
                'frozen_at':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2,sort_keys=True)+'\n')
            print('FROZEN CANDIDATE '+str(manifest_path),flush=True)
        assert all(sha(p)==h for p,h in state['inputs'].items()),'Source changed during qualification'
        state.update(all_required_checks_pass=True,native_c_file_guest_pass=not args.build_only,source_before_after_identical=True,qualification_reused=args.build_only)
    except BaseException as error:
        state['failure']=str(error);(out/'exception.log').write_text(traceback.format_exc())
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ['commands','inputs','objects','elfs','kernels','guests']},indent=2),flush=True)
    return 0 if state['all_required_checks_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
