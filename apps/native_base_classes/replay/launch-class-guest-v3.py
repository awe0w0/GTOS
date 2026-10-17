#!/usr/bin/env python3
"""Offline separate native class semantics; exact tools and fresh output only."""
import atexit,ctypes,hashlib,json,os,pathlib,subprocess,sys,time
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-pma-20261017-a';OUT=P/'class-guest-03';CHILD=P/'class-guest-v3.py'
CHILD_SHA='7528e35c5db0dd8709d82d2df54b97b9f1b5583cfcfc246654d6cc204b2741ca'
PYTHON=pathlib.Path('/usr/bin/python3');PYTHON_SHA='298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
EXECUTABLES={PYTHON:PYTHON_SHA,W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang':'656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',pathlib.Path('/usr/bin/x86_64-linux-gnu-ld.bfd'):'476b24d5cc1fef54f80412d06e7430b2dcf4e2b6f1525ee17e6eaaf16b28a00f',pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump'):'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d',W/'toolchains/gtos-qemu8-native-files-a/root/usr/bin/qemu-system-i386':'28fa14f1c45fca7422e3ec5737768b33b705de2f31014a76e658d4263464a6a5',W/'toolchains/gtos-qemu8-native-files-a/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2':'c20a2dc8917c755f02b94049356320fe1f62ac7d9f8994731f807d9df39302da'}
ENV={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC'};HANDLES=[];DOC=W/'sources/GTOS/docs/native-command-execution.md'

def close_handles():
    while HANDLES:os.close(HANDLES.pop())
atexit.register(close_handles)

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def emit(path,data):path.write_text(json.dumps(data,indent=2)+'\n')
class Ruleset(ctypes.Structure):_fields_=[('fs',ctypes.c_uint64),('net',ctypes.c_uint64),('scoped',ctypes.c_uint64)]
class Beneath(ctypes.Structure):
    _pack_=1
    _fields_=[('rights',ctypes.c_uint64),('fd',ctypes.c_int32)]

def landlock():
    require(os.uname().machine=='x86_64','Reviewed syscall ABI required');libc=ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6',use_errno=True);libc.syscall.restype=ctypes.c_long
    abi=libc.syscall(ctypes.c_long(444),ctypes.c_void_p(),ctypes.c_size_t(0),ctypes.c_uint(1));require(abi>=6,'Landlock ABI >=6 required; no fallback')
    rule=Ruleset(65535,3,3);descriptor=libc.syscall(ctypes.c_long(444),ctypes.byref(rule),ctypes.sizeof(rule),ctypes.c_uint(0));require(descriptor>=0,'Create Landlock ruleset')
    read=(1<<2)|(1<<3);write=read|(1<<1)|(1<<7)|(1<<8)|(1<<14)
    paths=[(W,read),(pathlib.Path('/usr/bin'),read),(pathlib.Path('/usr/lib'),read),(OUT,write),(pathlib.Path('/dev/null'),6),(pathlib.Path('/etc/ld.so.cache'),4),(pathlib.Path('/dev/urandom'),4),(pathlib.Path('/dev/random'),4),(pathlib.Path('/dev/zero'),4)]
    paths.extend((pathlib.Path(p).resolve(strict=True),read) for p in ('/bin','/lib','/lib64'))
    paths.extend((p.resolve(strict=True),5) for p in EXECUTABLES)
    paths.append((pathlib.Path('/lib64/ld-linux-x86-64.so.2').resolve(strict=True),5))
    try:
        for path,rights in paths:
            fd=os.open(str(path),os.O_PATH|os.O_CLOEXEC);HANDLES.append(fd);item=Beneath(rights,fd)
            require(libc.syscall(ctypes.c_long(445),ctypes.c_int(descriptor),ctypes.c_int(1),ctypes.byref(item),ctypes.c_uint(0))==0,'Add Landlock path rule')
        require(libc.prctl(ctypes.c_int(38),ctypes.c_ulong(1),ctypes.c_ulong(0),ctypes.c_ulong(0),ctypes.c_ulong(0))==0,'Set no_new_privs')
        require(libc.syscall(ctypes.c_long(446),ctypes.c_int(descriptor),ctypes.c_uint(0))==0,'Apply Landlock restriction')
    finally:os.close(descriptor)
    return abi
require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed Python -I -B -S entry')
require(pathlib.Path(sys.executable).resolve()==pathlib.Path('/usr/bin/python3.8') and sha(PYTHON)==PYTHON_SHA,'Qualified Python')
require(set(os.environ)<={'PATH','LANG','TZ','LC_CTYPE'} and all(os.environ.get(k)==v for k,v in ENV.items()),'Minimum environment')
interfaces=[line.split(':',1)[0].strip() for line in pathlib.Path('/proc/self/net/dev').read_text().splitlines()[2:] if ':' in line]
require(interfaces==['lo'] and not pathlib.Path('/proc/self/net/route').read_text().splitlines()[1:],'No external network')
mapping=pathlib.Path('/proc/self/uid_map').read_text().split();require(len(mapping)==3 and mapping[0]=='0' and mapping[2]=='1' and os.getuid()==0,'Fresh mapped user namespace')
require(P.resolve()==P and pathlib.Path(__file__).resolve()==P/'launch-class-guest-v3.py' and CHILD.resolve()==CHILD and not CHILD.is_symlink() and sha(CHILD)==CHILD_SHA,'Fixed reviewed paths and child')
for path,digest in EXECUTABLES.items():require(path.is_file() and sha(path)==digest,'Exact executable changed')
require(not OUT.exists() and not OUT.is_symlink(),'Fresh output only; preserve previous results');before=sha(DOC);ns={n:os.stat('/proc/self/ns/'+n).st_ino for n in ('user','net')};OUT.mkdir();abi=landlock()
try:fd=os.open(str(DOC),os.O_WRONLY)
except PermissionError:denied=True
else:os.close(fd);raise RuntimeError('Source write restriction not enforced')
require(sha(DOC)==before,'Source unchanged after write-refusal check')
guard={'launcher_sha256':sha(__file__),'child_sha256':CHILD_SHA,'landlock_applied':True,'landlock_abi':abi,'landlock_handled_access_fs':65535,'landlock_handled_access_net':3,'landlock_scoped':3,'network_disabled':True,'source_write_denied':denied,'source_document_sha256':before,'directory_execute_permissions':False,'writable_output':str(OUT),'namespace_ids':ns,'exact_executable_files':{str(p.resolve(strict=True)):d for p,d in EXECUTABLES.items()},'workspace_read_only':True,'output_execution_denied':True,'output_symlink_creation_allowed':False,'output_delete_and_rename_allowed':False,'no_new_privs':True,'outer_user_namespace_uid_map':mapping,'rule_path_handles_retained':True}
emit(OUT/'guard.json',guard);argv=[str(PYTHON),'-I','-B','-S',str(CHILD)];began=time.monotonic()
with (OUT/'launcher.log').open('xb') as stream:
    process=subprocess.Popen(argv,cwd=W,env=ENV,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT)
    emit(OUT/'job.json',{'state':'running','child_pid':process.pid,'launcher_pid':os.getpid(),'child_sha256':CHILD_SHA,'namespace_ids':ns})
    try:result=process.wait(timeout=3600)
    except subprocess.TimeoutExpired:process.kill();process.wait();result=124
    emit(OUT/'job.json',{'state':'completed','child_pid':process.pid,'launcher_pid':os.getpid(),'child_sha256':CHILD_SHA,'namespace_ids':ns,'returncode':result})
emit(OUT/'launcher-result.json',{'argv':argv,'shell':False,'returncode':result,'seconds':time.monotonic()-began,'child_sha256':sha(CHILD),'guard_sha256':sha(OUT/'guard.json'),'launcher_log_sha256':sha(OUT/'launcher.log')});print(json.dumps({'returncode':result,'output':str(OUT),'landlock_abi':abi}),flush=True);close_handles();sys.exit(result)
