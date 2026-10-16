#!/usr/bin/env python3
"""Build the real C/ThreadChecker runtime consumer with accepted primitive-C RTL."""
import ctypes,hashlib,json,os,pathlib,shlex,struct,subprocess,sys,traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-directory-20261017-a';G=P/'content-graph-03';C=P/'compile-06'
OUT=P/'runtime-user-04';BUILD=OUT/'build';ROOT=OUT/'source';PROBE=P/'runtime-probe-03';R=W/'sources/GTOS'
CC=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang';LD=CC.parent/'ld.lld'
state={'complete':False,'consumer_link_pass':False,'C_headers_pass':False,'native_guest_pass':False,'browser_pass':False,
       'inputs':{},'aliases':{},'MD_aliases':{},'objects':{},'ELFs':{},'commands':[],'unbound_MD_inputs':{}}
authorized=False;exit_code=1;allowed={}
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def bind(path,digest):
    logical=pathlib.Path(path);physical=logical.resolve(strict=True);key=str(physical)
    require(W in physical.parents and physical.is_file(),'Input escaped workspace')
    require(key not in state['inputs'] or state['inputs'][key]==digest,'Input conflict')
    if key not in state['inputs']:require(sha(physical)==digest,'Changed input: '+key)
    state['inputs'][key]=digest;state['aliases'][str(logical)]=key
def emit():
    (OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def read_receipt(path,digest,checks):
    bind(path,digest);value=json.loads(path.read_text());require(all(value[key] for key in checks) and 'failure' not in value,'Prior qualification missing')
    for key,expected in value['inputs'].items():
        require(key not in allowed or allowed[key]==expected,'Inherited source conflict');allowed[key]=expected
    return value
def run(argv,name,negative=False):
    argv=[str(v) for v in argv];path=OUT/(name+'.log')
    with path.open('xb') as stream:
        result=subprocess.run(argv,cwd=BUILD,env=environment,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
    require(path.stat().st_size<8*1024*1024,'Unbounded tool log');state['commands'].append({'argv':argv,'returncode':result.returncode,'log':str(path),'sha256':sha(path)});emit()
    require(result.returncode!=0 if negative else result.returncode==0,'Unexpected tool result: '+name);return path.read_text(errors='replace')
def deps(path,obj):
    text=path.read_text().replace(chr(92)+chr(10),' ');require(text.startswith(str(obj)+': ') and chr(92) not in text and '$' not in text,'Unexpected MD syntax')
    names=text.split(':',1)[1].split();require(0<len(names)<10000 and len(names)==len(set(names)),'Unexpected MD count');aliases={};unknown=[]
    for name in names:
        logical=BUILD/name;physical=logical.resolve(strict=True);key=str(physical);require(W in physical.parents,'MD escaped workspace')
        if key not in allowed:
            state['unbound_MD_inputs'][key]={'logical':str(logical),'sha256':sha(physical)};unknown.append(key)
        else:bind(logical,allowed[key])
        aliases[str(logical)]=key
    state['MD_aliases'].update(aliases);emit();require(not unknown,'New source requires independent provenance before CXX');return aliases
try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed no-site entry')
    guard=json.loads((OUT/'guard.json').read_text())
    require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-runtime-user-v4.py') and guard['landlock_applied']
        and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
    require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')),'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required');authorized=True
    bind(CC,'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7');bind(LD,'99ef321b93b2f3c8214bb592b633b3736d2df70f51e5454920131631f08e3412')
    graph=read_receipt(G/'status.json','27c52ea21a1661cf10ef6a5576509044fb7e48cb0e7a047a4c19362c319f205c',('diagnostic_complete','gn_generation_pass','bound_inputs_unchanged','input_symlinks_unchanged','actual_final_compiled_source_view'))
    compiled=read_receipt(C/'status.json','2b26e0319fe8b88dd8fea6ee25cdfa329307ba5548ab9f3c72e43efafcfd794d',('diagnostic_complete','objects_qualified','bound_inputs_unchanged','aliases_unchanged','MD_aliases_unchanged'))
    read_receipt(W/'artifacts/native-dup-20261016T213527Z-569/status.json','230e5df0382cae1cc61177440cf8a023b44f9822f02305aabf3fef3a5de0663f',('complete','native_DUP_CPL3_guest_pass','bound_inputs_unchanged','aliases_unchanged'))
    bind(P/'runtime-support-03.json','20a9edb4bcbe6f6def75a6d6632e6fa096392aaa0a95a18441292c99f213ff83');support=json.loads((P/'runtime-support-03.json').read_text())
    bind(W/support['prior_consumer'],support['prior_consumer_sha256']);bind(W/support['published_acceptance'],support['published_acceptance_sha256']);bind(W/support['prior_guest'],support['prior_guest_sha256'])
    require(len(support['support_inputs'])==133 and support['previous_goal_turn']=='progress','Exact real primitive-C runtime required')
    retained=[]
    for item in support['support_inputs']:
        path=pathlib.Path(item['path']);require(path.is_absolute() and path.suffix in ('.o','.a'),'Only typed linker input data allowed');bind(path,item['sha256']);retained.append(path)
    for name,digest in support['probe_sources'].items():bind(PROBE/name,digest);allowed[str(PROBE/name)]=digest
    bind(PROBE/'headers.c','3d95bd0a9e8da2db08dc660dc656c14291cb7881a00e355fe225637dbc6a6467');allowed[str(PROBE/'headers.c')]='3d95bd0a9e8da2db08dc660dc656c14291cb7881a00e355fe225637dbc6a6467'
    bind(P/'thread-checker-origins-01.json','60a9896c359a16bb694b46c2df68dadd6dca0ca90ead1d2988a5f73122eb3c47');owner=json.loads((P/'thread-checker-origins-01.json').read_text())['owner']
    bind(G/'build'/owner['relative'],owner['sha256']);content=(G/'build'/owner['relative']).read_text().splitlines();fields=[]
    require(len(owner['row'])==1 and content.count(owner['row'][0])==1,'Actual original base owner member changed')
    for key in ('defines','include_dirs','cflags','cflags_cc'):
        value=owner['fields'][key];require('$' not in value and content.count(key+' = '+value)==1,'Actual field mismatch');fields.extend(shlex.split(value))
    require(all(flag in fields for flag in ('-D__GTOS__=1','-O2','-std=c++23','-DDCHECK_ALWAYS_ON=1','-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE')),'Current Content namespace/features required')
    require(not any(flag.startswith(('@','--sysroot','--config=','-fplugin=','-fpass-plugin=','-ivfsoverlay','-D__linux__','-D__unix__')) for flag in fields) and '-load' not in fields and '-wrapper' not in fields,'No tool/plugin override')
    for index,flag in enumerate(fields):
        if flag=='-add-plugin':require(fields[index-1:index+3] in [['-Xclang','-add-plugin','-Xclang',v] for v in ('find-bad-constructs','raw-ptr-plugin','unsafe-buffers')],'Only original static plugins')
    BUILD.mkdir();ROOT.symlink_to((G/'source').resolve(strict=True),target_is_directory=True);state['aliases'][str(ROOT)]=str((G/'source').resolve(strict=True))
    generated=(W/'artifacts/native-perfetto-time-20261016-a/buffers-02/build/gen').resolve(strict=True)
    require(W/'artifacts' in generated.parents,'Generated view escaped workspace');(BUILD/'gen').symlink_to(generated,target_is_directory=True);state['aliases'][str(BUILD/'gen')]=str(generated)
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    environment={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    prefix=[CC,'-x','c++','--target=i686-unknown-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
        '-mno-mmx','-msse2','-mfpmath=sse','-mno-sse3','-mno-ssse3','-mno-sse4','-mno-avx','-femulated-tls','-fstack-usage','-nostdinc','-nostdinc++','-DGTOS_NATIVE_SDK=1',
        '-isystem','../source/tools/gtos_native_sdk/sdk','-isystem','../source/third_party/libc++/src/include','-isystem','../source/tools/gtos_native_sdk/c-sdk',
        '-isystem','../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include','-fno-exceptions','-fno-rtti']
    for relative in ('build/config/unsafe_buffers_paths.txt','build/config/raw_ptr_plugin_config.yaml','build/config/warning_suppression.txt'):
        physical=(ROOT/relative).resolve(strict=True);require(str(physical) in allowed,'Original compiler policy not qualified');bind(ROOT/relative,allowed[str(physical)])
    crash='-fcrash-diagnostics-dir=../source/out/clang-crashreports';require(fields.count(crash)==1,'Exact crash relocation only');fields[fields.index(crash)]='-fcrash-diagnostics-dir='+str(OUT/'crash')
    imported=[]
    for name in ('directory','thread_checker'):
        item=compiled['objects'][name];bind(item['path'],item['sha256']);bind(item['MD'],item['MD_sha256']);imported.append(item['path'])
    for opt in ('0','2'):
        own=[flag for flag in fields if flag!='-O2']+['-O'+opt,'-I'+str(R/'include'),'-I'+str(PROBE)]
        obj=OUT/('user-O'+opt+'.o');pre=OUT/('user-O'+opt+'.pre.d');dep=OUT/('user-O'+opt+'.d');state['stage']='user_M_O'+opt;emit()
        run(prefix+own+['-M','-MF',pre,'-MT',obj,PROBE/'user.cpp'],'user-O'+opt+'-M');expected=deps(pre,obj)
        run(prefix+own+['-MD','-MF',dep,'-c',PROBE/'user.cpp','-o',obj],'user-O'+opt+'-CXX');require(deps(dep,obj)==expected,'Fixture full MD changed')
        state['objects']['user-O'+opt]={'path':str(obj),'sha256':sha(obj),'MD':str(dep),'MD_sha256':sha(dep),'MD_unique':len(set(expected.values()))};emit()
        elf=OUT/('user-O'+opt+'.elf');run([LD,'-m','elf_i386','--gc-sections','-T',PROBE/'user.ld','-Map='+str(OUT/('user-O'+opt+'.map')),'-o',elf,obj,*imported,*retained],'user-O'+opt+'-link')
        data=elf.read_bytes();require(data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,3),'Native ELF32 executable required')
        state['ELFs']['O'+opt]={'path':str(elf),'sha256':sha(elf),'bytes':len(data)};emit()
    cflags=['-x','c','--target=i686-unknown-none-elf','-std=c11','-ffreestanding','-nostdinc','-fno-builtin','-D__GTOS__=1','-Wall','-Wextra','-Werror','-O2','-isystem','../source/tools/gtos_native_sdk/c-sdk']
    obj=OUT/'headers.o';pre=OUT/'headers.pre.d';dep=OUT/'headers.d';run([CC]+cflags+['-M','-MF',pre,'-MT',obj,PROBE/'headers.c'],'headers-M');expected=deps(pre,obj)
    run([CC]+cflags+['-MD','-MF',dep,'-c',PROBE/'headers.c','-o',obj],'headers-C11');require(deps(dep,obj)==expected,'C11 MD changed')
    for target in ('x86_64-unknown-none-elf','i686-unknown-linux-gnu'):
        negative=[('--target='+target) if flag.startswith('--target=') else flag for flag in cflags]
        text=run([CC]+negative+['-E',PROBE/'headers.c','-o',OUT/('rejected-'+target+'.i')],'headers-reject-'+target,True)
        require('GTOS directory declarations require the native IA32 ABI' in text and 'GTOS descriptor declarations require the native IA32 ABI' in text,'Both explicit native header guards must reject')
    state['stage']='final_input_alias_checks';emit()
    require(all(sha(path)==digest for path,digest in state['inputs'].items()),'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True))==physical for group in (state['aliases'],state['MD_aliases']) for path,physical in group.items()),'Bound alias changed')
    state.update(stage='completed',complete=True,consumer_link_pass=True,C_headers_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,MD_aliases_unchanged=True,
        scope='New O0/O2 fixture links the actual GN-owned native directory and ThreadChecker objects to133 previously accepted real primitive-C runtime objects/regular archive in retained order. Current Content STL namespace used only for current ThreadChecker; no old private STL objects cross interfaces. Real malloc/errno/FILE/TLS/entry reused, no heap/lock/thread stubs. C11 and native-only guards tested. No guest/runtime/Content/browser execution or cold provenance of legacy runtime newly certified.')
    emit();print(json.dumps({'consumer_link_pass':True,'C_headers_pass':True,'ELFs':len(state['ELFs'])}),flush=True);exit_code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc());emit()
    traceback.print_exc()
sys.exit(exit_code)
