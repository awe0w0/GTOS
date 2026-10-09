#!/usr/bin/env python3
"""Qualify the full IA32 kernel FileStore against actual isolated ATA media."""
import argparse,datetime,hashlib,json,os,pathlib,re,shutil,subprocess,sys,traceback

def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def observe(text,phase,cut=None):
    lines=text.splitlines()
    if phase=='stop':
        assert lines==['FILESTORE GUEST BOOT','CUT AFTER COMPLETE ATA SECTOR writes='+format(cut,'08X')]
        return {'completed_sectors':cut}
    extra=[]
    if phase=='recover':
        matches=re.findall(r'^RECOVER CONTENT=(old|new)$',text,re.M)
        assert len(matches)==1;extra=['RECOVER CONTENT='+matches[0]]
    if phase=='complete':
        matches=re.findall(r'^CUT COMPLETE writes=([0-9A-F]{8})$',text,re.M)
        assert len(matches)==1;extra=['CUT COMPLETE writes='+matches[0]]
    expression=r'SUMMARY phase='+phase+r' checks=([0-9A-F]{8}) reads=([0-9A-F]{8}) writes=([0-9A-F]{8}) flushes=([0-9A-F]{8}) stack_observed=([0-9A-F]{8}) open=([0-9A-F]{8})'
    matches=re.findall(expression,text);assert len(matches)==1
    summary=re.search(expression,text).group(0)
    assert lines==['FILESTORE GUEST BOOT',*extra,summary,'FILESTORE GUEST PASS']
    result=dict(zip(['checks','reads','writes','flushes','stack_observed','open'],[int(x,16) for x in matches[0]]))
    assert 0<result['stack_observed']<16384-512 and result['open']==0 and result['reads']>0
    if phase in ('writer','reader'):
        assert result['checks']>20000 and result['reads']>100 and result['writes']>0 and result['flushes']>0
    elif phase=='recover':
        assert result['checks']>8000 and result['writes']==0 and result['flushes']==0
        result['content']=extra[0].split('=')[1]
    else:
        result['replacement_writes']=int(extra[0].split('=')[1],16)
        assert result['writes']==result['replacement_writes'] and 10<=result['writes']<=128
    return result

def guards(path):
    data=pathlib.Path(path).read_bytes();assert len(data)==2064*512
    for s in range(8):assert data[s*512:(s+1)*512]==bytes([0xA5+s])*512,'prefix changed'
    for s in range(2056,2064):assert data[s*512:(s+1)*512]==bytes([0x5A+s-2056])*512,'suffix changed'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=pathlib.Path)
    parser.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    parser.add_argument('--cc',default=os.environ.get('CC','gcc'))
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args()
    repo=pathlib.Path(__file__).resolve().parents[1];out=args.output.resolve()
    assert not out.exists(),'Use a fresh output directory; prior runs are retained.'
    out.mkdir(parents=True)
    state={'scope':'Full kernel IA32 FileStore on actual isolated ATA; CPL0 only',
        'all_required_checks_pass':False,'filesystem_backend_guest_pass':False,
        'native_file_abi_pass':False,'native_isolate_pass':False,'production_image_changed':False,
        'existing_capacity_changed':False,'physical_power_loss_claim':False,
        'commands':[],'inputs':{},'objects':{},'kernels':{},'guests':[],'cuts':[],'build_only':args.build_only}
    def save():(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
    def bind(path):
        path=pathlib.Path(path);path=(repo/path).resolve() if not path.is_absolute() else path.resolve()
        value=sha(path)
        if str(path) in state['inputs']:assert state['inputs'][str(path)]==value
        else:
            state['inputs'][str(path)]=value
            try:relative=path.relative_to(repo)
            except ValueError:return
            target=out/'source-snapshot'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
    def run(cmd,log,expected=0,timeout=180):
        cmd=[str(x) for x in cmd]
        with log.open('wb') as f:
            try:code=subprocess.run(cmd,cwd=repo,stdout=f,stderr=subprocess.STDOUT,timeout=timeout).returncode
            except subprocess.TimeoutExpired:code=124
        state['commands'].append({'argv':cmd,'exit_code':code,'log':str(log),'log_sha256':sha(log)});save()
        if code!=expected:
            detail=log.read_text(errors='replace')[-5000:];guest=log.parent/'guest.log'
            if guest.exists():detail+='\n'+guest.read_text(errors='replace')[-5000:]
            raise RuntimeError(str(log)+' exit='+str(code)+'\n'+detail)
        return log.read_text()
    def dependencies(path):
        for token in path.read_text().replace('\\\n',' ').split(':',1)[1].split():bind(token)
    def boot(kernel,directory,command,image,expected):
        directory.mkdir(parents=True);iso=directory/'iso/boot/grub';iso.mkdir(parents=True)
        shutil.copyfile(kernel,iso.parent/'native.bin')
        (iso/'grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Isolated actual ATA filesystem" {\n multiboot /boot/native.bin '+command+'\n boot\n}\n')
        run([bins['grub-mkrescue'],'--output='+str(directory/'native.iso'),directory/'iso'],directory/'grub.log')
        run([bins['qemu-system-i386'],'-L',os.environ.get('GTOS_QEMU_DATA_DIR','/usr/share/qemu'),'-machine','pc','-accel','tcg','-cpu','max',
            '-m','32' if opt==0 else '64','-smp','1' if opt==0 else '4','-cdrom',directory/'native.iso','-boot','d',
            '-drive','file='+str(image)+',format=raw,if=ide,index=0','-nic','none','-display','none','-monitor','none','-serial','none',
            '-debugcon','file:'+str(directory/'guest.log'),'-device','isa-debug-exit,iobase=0xf4,iosize=4','-no-reboot'],directory/'qemu.log',expected,120)
        guards(image);return (directory/'guest.log').read_text()
    try:
        bind(__file__)
        profile_path=repo/'docs/littlefs/source-lock.json';profile=None
        if profile_path.exists():
            profile=json.loads(profile_path.read_text());bind(profile_path)
            for name,value in profile['files'].items():assert sha(repo/name)==value,'Locked filesystem input changed '+name;bind(name)
        cxx=pathlib.Path(shutil.which(args.cxx)).resolve();cc=pathlib.Path(shutil.which(args.cc)).resolve()
        bins={name:pathlib.Path(shutil.which(name)).resolve() for name in ['as','ld','ar','nm','objdump','readelf','grub-mkrescue','qemu-system-i386']}
        state['tools']={name:{'path':str(path),'sha256':sha(path)} for name,path in dict(bins,cxx=cxx,cc=cc).items()}
        state['compiler_version']=subprocess.check_output([cxx,'--version'],text=True).splitlines()[0]
        policy=(repo/'tools/kernel-cxxflags').read_text().split();bind('tools/kernel-cxxflags')
        assert policy==['-mgeneral-regs-only','-mno-sse','-mno-mmx','-msoft-float','-fno-tree-vectorize']
        common=['-m32','-ffreestanding','-nostdlib','-nostdinc','-fno-builtin','-fno-stack-protector','-fno-pie',
            '-fno-asynchronous-unwind-tables','-ffunction-sections','-fdata-sections','-fstack-usage',
            '-Iinclude','-Iinclude/storage/littlefs-sdk','-Iinclude/storage/littlefs','-Wall','-Wextra','-Werror']
        cflags=['-x','c','-std=c99','-DLFS_DEFINES=storage/littlefs_port.h']
        cppflags=['-std=c++11','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit']
        state['common_flags']=common;state['c_flags']=cflags;state['cpp_flags']=cppflags;state['mandatory_integer_flags']=policy
        for name in ['docs/littlefs/LICENSE.md','include/storage/littlefs/lfs.h','include/storage/littlefs/lfs_util.h',
            'tests/filestore_stock.c','tests/filestore_loader.s','tests/native_process_smoke.ld','tools/audit-kernel-instructions.py']:
            bind(name)
        for opt in [0,2]:
            d=out/('O'+str(opt));(d/'kernel').mkdir(parents=True);objects=[];component=[]
            sources=['src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','src/storage/littlefs_port.cpp','src/storage/filestore.cpp',
                'src/drivers/ata.cpp','src/hardwarecommunication/port.cpp','tests/filestore_guest.cpp']
            for name in sources:
                bind(name);obj=d/'kernel'/(pathlib.Path(name).stem+'.o');objects.append(obj)
                if name.startswith('src/storage/'):component.append(obj)
                run([cxx,*common,*(cflags if name.endswith('.c') else cppflags),'-O'+str(opt),*policy,
                    '-MD','-MF',obj.with_suffix('.d'),'-c',name,'-o',obj],obj.with_suffix('.log'))
                dependencies(obj.with_suffix('.d'));state['objects']['O'+str(opt)+'-'+obj.name]=sha(obj)
            run([bins['as'],'--32','tests/filestore_loader.s','-o',d/'kernel/loader.o'],d/'loader.log');objects.append(d/'kernel/loader.o')
            run([bins['ar'],'rcsD',d/'libgtos_filestore.a',*component],d/'archive.log')
            run([bins['ld'],'-melf_i386','-r','-o',d/'closure.o',*component],d/'closure.log')
            assert not run([bins['nm'],'-u',d/'closure.o'],d/'closure-undefined.log').strip()
            # No section GC: audit every function in full upstream and all port objects.
            run([bins['ld'],'-melf_i386','-T','tests/native_process_smoke.ld','-Map',d/'kernel.map','-o',d/'kernel.bin',*objects],d/'link.log')
            assert not run([bins['nm'],'-u',d/'kernel.bin'],d/'undefined.log').strip()
            run([sys.executable,'tools/audit-kernel-instructions.py','--map',d/'kernel.map','--source-root',repo,d/'kernel.bin'],d/'integer-audit.log')
            run([bins['readelf'],'-h','-l',d/'kernel.bin'],d/'elf.log')
            run([bins['objdump'],'-d',d/'kernel.bin'],d/'disassembly.log')
            state['kernels']['O'+str(opt)]=sha(d/'kernel.bin')
            if profile and args.build_only:
                assert state['tools']['cxx']['sha256']==profile['qualified_cxx_sha256'],'Reproduction requires qualified compiler'
                assert state['kernels']['O'+str(opt)]==profile['executed_kernels']['O'+str(opt)],'Executed kernel differs'
                for key,value in profile['executed_objects'].items():
                    if key.startswith('O'+str(opt)+'-'):assert state['objects'][key]==value,'Executed object differs '+key
            if args.build_only:continue
            run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Iinclude/storage/littlefs','tests/filestore_stock.c',
                'src/storage/littlefs/lfs.c','src/storage/littlefs/lfs_util.c','-o',d/'stock-verifier'],d/'stock-build.log')
            image=d/'filesystem.img'
            image.write_bytes(b''.join(bytes([0xA5+s])*512 for s in range(8))+b'\xff'*(2048*512)+b''.join(bytes([0x5A+s])*512 for s in range(8)))
            guards(image)
            for phase in ['writer','reader']:
                before=sha(image);g=d/phase;text=boot(d/'kernel.bin',g,phase,image,33);evidence=observe(text,phase)
                state['guests'].append({'opt':opt,'phase':phase,'evidence':evidence,'disk_before':before,'disk_after':sha(image),'guest_sha256':sha(g/'guest.log')});save()
                shutil.copyfile(image,g/'post-guest.img')
                reference=run([d/'stock-verifier',image,'write' if phase=='writer' else 'read'],g/'stock-interoperability.log')
                assert 'STOCK LITTLEFS INTEROPERABILITY PASS' in reference;guards(image)
                print('PASS FILESTORE '+str((opt,phase,evidence)),flush=True)
            baseline=d/'uncut-baseline.img';shutil.copyfile(image,baseline)
            complete=d/'complete';image=d/'complete.img';shutil.copyfile(baseline,image)
            result=observe(boot(d/'kernel.bin',complete,'cut=0',image,33),'complete')
            total=result['replacement_writes'];state['O'+str(opt)+'-replacement_writes']=total
            reference=run([d/'stock-verifier',image,'recover'],complete/'stock.log')
            assert 'STOCK RECOVER CONTENT=new' in reference;guards(image)
            for cut in range(1,total+1):
                c=d/('cut-'+str(cut));c.mkdir();image=c/'filesystem.img';shutil.copyfile(baseline,image)
                observe(boot(d/'kernel.bin',c/'stop','cut='+str(cut),image,97),'stop',cut)
                evidence=observe(boot(d/'kernel.bin',c/'recover','recover',image,33),'recover')
                reference=run([d/'stock-verifier',image,'recover'],c/'stock.log')
                assert 'STOCK RECOVER CONTENT='+evidence['content'] in reference;guards(image)
                state['cuts'].append({'opt':opt,'cut':cut,'evidence':evidence,'disk_sha256':sha(image),
                    'stop_sha256':sha(c/'stop/guest.log'),'recover_sha256':sha(c/'recover/guest.log')});save()
                print('PASS FILESTORE SECTOR STOP '+str((opt,cut,total,evidence['content'])),flush=True)
        assert all(sha(path)==value for path,value in state['inputs'].items()),'Source changed during qualification'
        state.update(all_required_checks_pass=True,filesystem_backend_guest_pass=not args.build_only,
            source_before_after_identical=True,qualification_reused=args.build_only,
            actual_ata_cold_boots=0 if args.build_only else len(state['guests'])+2+2*len(state['cuts']),
            all_replacement_sector_boundaries=not args.build_only)
    except BaseException as error:
        state['failure']=str(error);(out/'exception.log').write_text(traceback.format_exc());print(state['failure'],flush=True)
    finally:
        state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    print(json.dumps({k:v for k,v in state.items() if k not in ['commands','inputs','guests','cuts']},indent=2),flush=True)
    return 0 if state['all_required_checks_pass'] else 1

if __name__=='__main__':raise SystemExit(main())
