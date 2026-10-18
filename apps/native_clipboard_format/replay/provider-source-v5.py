#!/usr/bin/env python3
"""Two new full-M checks over preserved source copies; unchanged provider objects."""
import ctypes, hashlib, json, os, pathlib, re, shlex, stat, subprocess, sys, time, traceback
W=pathlib.Path('/mnt/f/GTOS-Chromium');Q=W/'artifacts/native-clipboard-format-20261018-a';OUT=Q/'provider-source-05'
H=W/'artifacts/native-large-heap-20261015-a/compile-05';HG=W/'artifacts/native-debug-shared-20261015-a/gn-01'
X=W/'artifacts/native-iostream-20261014-a/compile-thread-03';XG=W/'artifacts/native-iostream-20261014-a/gn-05'
CC=W/'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang';CC_SHA='656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7'
HO='obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.heap.o';XO='obj/tools/gtos_isolate_diagnostic/runtime/native_exit_libc.process-exit.o'
HS='e07e54f37c9fd28ba8c2e485dbbd2c17b84da6a6a372506cb0a3654d008b32f4';XS='bdedc2dbe50b97067ccb8493d4a74618af62b5784560826bc592912d029c6693'
PAYLOAD=W/'sources/GTOS/apps/native_v8_integration/payload-lock.json'
RESOURCE=W/'toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include'
DUPLICATE_KEYS={
    'source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-types/FILE.h':{'sha256':'6b26b98cba34a1536316479522bede9df90954f13060c1a42b7c4f6e9d293f77','entries':[
        {'index':71,'raw':'../source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-macros/../llvm-libc-types/FILE.h'},
        {'index':72,'raw':'../source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-types/FILE.h'}]},
    'source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-types/wint_t.h':{'sha256':'ee322165336ea6a08ababd87446230b89cf7ed38932267a129c44898593e5e6d','entries':[
        {'index':89,'raw':'../source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-macros/../llvm-libc-types/wint_t.h'},
        {'index':93,'raw':'../source/tools/gtos_isolate_diagnostic/c-sdk/llvm-libc-types/wint_t.h'}]}}
# Independently authored fixed runtime prefix and exact old GN field values.
# GN, compdb, and old successful argv are compared as DATA, never dispatched.
PREFIX=shlex.split('''-x c++ --target=i686-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-mmx -msse2 -mfpmath=sse -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx -femulated-tls -fstack-usage -nostdinc -nostdinc++ -DGTOS_ISOLATE_COMPILE_DIAGNOSTIC=1 -isystem ../source/tools/gtos_isolate_diagnostic/sdk -isystem ../source/third_party/libc++/src/include -isystem ../source/tools/gtos_isolate_diagnostic/c-sdk -isystem /mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin/../lib/clang/24/include -fno-exceptions -fno-rtti''')
FIELDS={
'defines':'''-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FORTIFY_SOURCE=2 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE -D__GTOS__=1 -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0 -DV8_TYPED_ARRAY_MAX_SIZE_IN_HEAP=64 -DV8_LOGGING_LEVEL=1 -DV8_USE_METAGEN_INSTANCE_TYPES=0 -DBUILTIN_BLOCK_POSITION -DV8_USE_HOST_CPU_ARM_FEATURES -DV8_ATOMIC_OBJECT_FIELD_WRITES -DV8_ENABLE_LAZY_SOURCE_POSITIONS -DV8_WIN64_UNWINDING_INFO -DV8_ENABLE_REGEXP_INTERPRETER_THREADED_DISPATCH -DV8_ENABLE_FUZZTEST -DV8_JITLESS -DV8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA -DV8_ALLOCATION_FOLDING -DV8_ALLOCATION_SITE_TRACKING -DV8_ADVANCED_BIGINT_ALGORITHMS -DV8_ENABLE_TDZ_HOLE -DV8_USE_ZLIB -DV8_ENABLE_MAGLEV_GRAPH_PRINTER -DV8_ENABLE_EXTENSIBLE_RO_SNAPSHOT -DV8_ENABLE_BLACK_ALLOCATED_PAGES -DV8_ENABLE_UNDEFINED_DOUBLE -DV8_WASM_RANDOM_FUZZERS -DV8_ARRAY_BUFFER_INTERNAL_FIELD_COUNT=0 -DV8_ARRAY_BUFFER_VIEW_INTERNAL_FIELD_COUNT=0 -DV8_PROMISE_INTERNAL_FIELD_COUNT=0 -DV8_USE_DEFAULT_HASHER_SECRET=true -DV8_DEPRECATION_WARNINGS -DV8_DISALLOW_JS_IN_API_INTERRUPTS_IS_CHECKED -DV8_IMMINENT_DEPRECATION_WARNINGS -DV8_HAVE_TARGET_OS -DV8_TARGET_OS_GTOS -DCPPGC_SUPPORTS_OBJECT_NAMES -DCPPGC_ENABLE_LARGER_CAGE -DCPPGC_SLIM_WRITE_BARRIER -DV8_TARGET_ARCH_IA32''',
'include_dirs':'''-I../source/tools/gtos_isolate_diagnostic/runtime -I../source/tools/gtos_isolate_diagnostic/runtime/sdk -I../source -Igen''',
'cflags':(r'''-Wall -Wextra -Wimplicit-fallthrough -Wextra-semi -Wunreachable-code-aggressive -Wgnu -Wno-gnu-anonymous-struct -Wno-gnu-conditional-omitted-operand -Wno-gnu-include-next -Wno-gnu-label-as-value -Wno-gnu-redeclared-enum -Wno-gnu-statement-expression -Wno-gnu-zero-variadic-macro-arguments -Wno-zero-length-array -Wthread-safety -Wno-unused-template -Wno-missing-field-initializers -Wno-unused-parameter -Wno-psabi -Wno-stringop-overread -Wloop-analysis -Wno-unneeded-internal-declaration -Wno-cast-function-type -Wno-thread-safety-reference-return -Wno-nontrivial-memcall -Wno-uninitialized-const-pointer -Wno-unused-but-set-global -Wshadow -Werror -Werror=\#pragma-messages -fno-delete-null-pointer-checks -fno-strict-overflow -fno-ident -fno-math-errno -fno-strict-aliasing -fcolor-diagnostics -fdiagnostics-show-inlining-chain -fmerge-all-constants -fcrash-diagnostics-dir=../source/out/clang-crashreports -fno-lifetime-dse -mllvm -instcombine-lower-dbg-declare=0 -mllvm -split-threshold-for-reg-with-hint=0 -ffp-contract=off -Wno-builtin-macro-redefined -D__DATE__= -D__TIME__= -D__TIMESTAMP__= -no-canonical-prefixes -fomit-frame-pointer -g0 -fsanitize=array-bounds -fsanitize-trap=array-bounds -fsanitize-ignore-for-ubsan-feature=array-bounds -Wno-unsafe-buffer-usage-in-static-sized-array -fsanitize=return -fsanitize-trap=return -fsanitize-ignore-for-ubsan-feature=return -fsanitize=unreachable -fsanitize-trap=unreachable -fsanitize-ignore-for-ubsan-feature=unreachable -Wheader-hygiene -Wstring-conversion -Wtautological-overlap-compare -Wunreachable-code -Wlifetime-safety-permissive -Wno-shadow -Wctad-maybe-unsupported -Xclang -add-plugin -Xclang blink-gc-plugin '''
           '-Wno-invalid-' 'offsetof '
           '''-Wmissing-field-initializers -Wunnecessary-virtual-specifier -O2 -fdata-sections -ffunction-sections -fno-unique-section-names'''),
'cflags_cc':('-Wno-invalid-' 'offsetof '
             '''-Wenum-compare-conditional -Wno-nullability-completeness -std=c++20 -Wno-trigraphs -gsimple-template-names -fno-exceptions -fno-rtti''')}
EXIT_FIELDS=dict(FIELDS)
EXIT_FIELDS['defines']='-DLIBC_NAMESPACE=__llvm_libc_cr -DLIBC_COPT_PUBLIC_PACKAGING=1 -DLIBC_ERRNO_MODE=3 -DLIBC_THREAD_MODE=LIBC_THREAD_MODE_PLATFORM '+FIELDS['defines']
EXIT_FIELDS['include_dirs']='-I../source/tools/gtos_isolate_diagnostic/native-exit/llvm-libc -I../source/tools/gtos_isolate_diagnostic/native-c-file/llvm-libc -I../source/third_party/llvm-libc/src '+FIELDS['include_dirs']
state={'complete':False,'provider_source_qualification_pass':False,'native_pass':False,'inputs':{},'aliases':{},'symlink_nodes':{},'exit_physical_source_nodes':{},'outputs':{},'commands':[],'provider_objects':{}}
metadata={};authorized=False;code=1;active=None

def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()
def info(path):
    row=path.stat();return(row.st_size,row.st_mtime_ns,row.st_ctime_ns,row.st_mode,row.st_dev,row.st_ino)
def bind(path,digest):
    logical=pathlib.Path(os.path.normpath(str(path)));actual=logical.resolve(strict=True)
    require(W in actual.parents and actual.is_file() and re.fullmatch(r'[0-9a-f]{64}',digest) and sha(actual)==digest,'Unbound/changed provider evidence: '+str(logical))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)]==digest,'Digest conflict')
    state['inputs'][str(actual)]=digest;state['aliases'][str(logical)]=str(actual)
    if OUT not in actual.parents and actual.suffix in ('.o','.a','.d','.log') and str(actual) not in metadata:metadata[str(actual)]=info(actual)
def read(path,digest):
    bind(path,digest);value=json.loads(path.read_text());require('failure' not in value,'Failed source evidence');return value
def emit():(OUT/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def deps(path):
    text=path.read_text().replace('\\\n',' ');require(text.count(':')==1 and '\\' not in text and '$' not in text,'Plain complete dependency syntax')
    result=shlex.split(text.split(':',1)[1]);require(0<len(result)<10000 and len(result)==len(set(result)),'Nonempty unique full dependency list');return result
def command(record,name):
    rows=[item for item in record['commands'] if item['name']==name];require(len(rows)==1 and rows[0]['returncode']==0,'Successful exact unit command');return rows[0]
def heap_dep(logical,record):
    logical=pathlib.Path(os.path.normpath(str(logical)));actual=logical.resolve(strict=True)
    require(str(actual) in record['inputs'],'Actual heap dependency absent from old source lock')
    require(record['input_symlinks'].get(str(logical),str(logical))==str(actual),'Heap logical-to-resolved source alias changed')
    bind(logical,record['inputs'][str(actual)]);return str(actual)
def exit_data_key(path):
    # Pure lexical history mapping. Never resolve or open the old GN source view.
    for root in (XG/'source',X/'source'):
        if root in path.parents:return 'source/'+str(path.relative_to(root))
    require(RESOURCE in path.parents,'Exit dependency outside preserved source copy and fixed resource headers')
    return 'physical/'+str(path)
def exit_physical_bind(path,digest):
    path=pathlib.Path(os.path.normpath(str(path)))
    require((X/'source') in path.parents or RESOURCE in path.parents,'Only exact materialized source/resource roots')
    current=W;parts=path.relative_to(W).parts
    require(len(parts)<128,'Excessive physical source depth')
    for index,part in enumerate(('',)+parts):
        if part:current=current/part
        row=current.lstat();kind='file' if index==len(parts) else 'directory'
        require(not stat.S_ISLNK(row.st_mode) and (stat.S_ISREG(row.st_mode) if kind=='file' else stat.S_ISDIR(row.st_mode)),'Materialized source ancestor/leaf is not an ordinary physical node: '+str(current))
        value={'kind':kind,'device':row.st_dev,'inode':row.st_ino}
        require(str(current) not in state['exit_physical_source_nodes'] or state['exit_physical_source_nodes'][str(current)]==value,'Physical source node changed during qualification')
        state['exit_physical_source_nodes'][str(current)]=value
    require(path.resolve(strict=True)==path,'Physical source path acquired an alias');bind(path,digest)
def fixed_lookup_directory(path):
    require(path in (CC.parent,X/'build',H/'build'),'Only the three fixed raw relative-lookup directory anchors')
    current=W;parts=path.relative_to(W).parts;require(len(parts)<128,'Excessive fixed directory depth')
    for part in ('',)+parts:
        if part:current=current/part
        row=current.lstat();require(not stat.S_ISLNK(row.st_mode) and stat.S_ISDIR(row.st_mode),'Fixed relative-lookup ancestor/leaf is not a physical directory: '+str(current))
        value={'kind':'directory','device':row.st_dev,'inode':row.st_ino}
        require(str(current) not in state['exit_physical_source_nodes'] or state['exit_physical_source_nodes'][str(current)]==value,'Fixed relative-lookup directory identity changed')
        state['exit_physical_source_nodes'][str(current)]=value
    require(path.resolve(strict=True)==path,'Fixed relative-lookup directory acquired an alias')
def exit_sequence(path,base,x,x5):
    sequence=[];closure={};multiset={};multiplicity={};physical=set()
    for item in deps(path):
        logical=pathlib.Path(os.path.normpath(str(base/item)));key=exit_data_key(logical)
        source=X/key if key.startswith('source/') else pathlib.Path(key[len('physical/'):]);actual=str(source)
        require(actual in x['inputs'] and actual in x5['inputs'] and x['inputs'][actual]==x5['inputs'][actual] and source!=PAYLOAD,'Each raw exit dependency has exact identical original/post-cache source pins')
        digest=x['inputs'][actual];exit_physical_bind(source,digest);physical.add(actual)
        require(key not in closure or closure[key]==digest,'Repeated canonical exit dependency key has conflicting source SHA')
        closure[key]=digest;sequence.append({'raw':item,'index':len(sequence)+1,'key':key,'sha256':digest,'physical_source':actual})
        pair=(key,digest);multiset[pair]=multiset.get(pair,0)+1;multiplicity[key]=multiplicity.get(key,0)+1
    require(len(sequence)==113 and len(closure)==len(physical)==111 and {key:count for key,count in multiplicity.items() if count>1}=={key:2 for key in DUPLICATE_KEYS},'Exact raw113/physical111 closure and only two fixed lexical duplicate keys')
    for key,expected in DUPLICATE_KEYS.items():
        require(closure[key]==expected['sha256'] and [{'index':row['index'],'raw':row['raw']} for row in sequence if row['key']==key]==expected['entries'],'Fixed duplicate source SHA, positions and original lexical spellings')
    return sequence,closure,multiset,multiplicity,physical

try:
    require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated no-site entry')
    require(set(os.environ)<={'PATH','LANG','TZ','LC_CTYPE'} and all(os.environ.get(key)==value for key,value in {'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC'}.items()),'Minimum inherited environment')
    guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(Q/'launch-provider-source-v5.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Exact inherited guard')
    require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs');authorized=True
    bind(__file__,guard['child_sha256']);bind(Q/'launch-provider-source-v5.py',guard['launcher_sha256']);bind(CC,CC_SHA)
    for directory in (CC.parent,X/'build',H/'build'):fixed_lookup_directory(directory)
    state['fixed_relative_lookup_directories']=[str(directory) for directory in (CC.parent,X/'build',H/'build')]
    h=read(H/'status.json','04aa857682f7d1db50c0984f8652252ecf11c559f3a7a267d07e30aa55c0092f');require(all(h[key] for key in ('all_compile_pass','gn_qualified','full_M_MD','bound_inputs_unchanged','input_symlinks_unchanged')) and h['objects']['heap']['path']==str(H/'build'/HO) and h['objects']['heap']['sha256']==HS,'Original heap unit and aggregate source data')
    bind(H.parent/'compile-v5.py','da71b523a434437c24df957ede5bff517031713400ade00e305f0fe128c5d137')
    hg=read(H/'guard.json','9e0a0f59c2a2d4ad1c19181ba00aa83d6463d95e072013ce12561f6c373e2f8c');hl=read(H/'launcher-result.json','5d8eec2eae84916eca829af5d2505c8fe524f26f7399a52cf4611efa61178874');require(hg['child_sha256']==state['inputs'][str(H.parent/'compile-v5.py')] and hl['returncode']==0 and hl['guard_sha256']==state['inputs'][str(H/'guard.json')],'Original successful heap script identity')
    gn=read(HG/'status.json','386614c2f9fb5355af9cc26d387eab3d7891b2d9e057f13554ab03240e08dc97');require(gn['all_checks_pass'] and gn['bound_inputs_unchanged'] and gn['input_symlinks_unchanged'] and gn['prior_object_commands_preserved'],'Accepted old GN configuration evidence')
    owner=HG/'build/obj/tools/gtos_isolate_diagnostic/native_runtime.ninja';bind(owner,'2c53ec53ed68926a93cd609e2c19594bf8f49da51ec02964266edf3729b6e72a')
    lines=owner.read_text().splitlines();require(lines.count('build '+HO+': cxx ../source/tools/gtos_isolate_diagnostic/runtime/heap.cc')==1,'Exact actual heap GN owner member')
    for key,value in FIELDS.items():require(lines.count(key+' = '+value)==1,'Independent typed GN field mismatch: '+key)
    graph_flags=PREFIX+[token for key in ('defines','include_dirs','cflags','cflags_cc') for token in shlex.split(FIELDS[key])]
    comparable=[('-fcrash-diagnostics-dir=../crash' if flag=='-fcrash-diagnostics-dir=../source/out/clang-crashreports' else flag) for flag in graph_flags]
    compdb=HG/'compdb.log';bind(compdb,'6fc46700cbeedad6bfda1ae9e7cefe422b4c5d239f1a03290bbd6653f61c8c5a');rows=[row for row in json.loads(compdb.read_text()) if row['file']=='../source/tools/gtos_isolate_diagnostic/runtime/heap.cc' and row['output']==HO]
    require(len(rows)==1 and rows[0]['directory']==str(HG/'build') and shlex.split(rows[0]['command'])==[str(CC)]+graph_flags+['-MMD','-MF',HO+'.d','-c','../source/tools/gtos_isolate_diagnostic/runtime/heap.cc','-o',HO],'Typed actual old GN/compdb profile')
    old_compile=command(h,'compile-heap');require(old_compile['cwd']==str(H/'build') and old_compile['argv']==[str(CC)]+comparable+['-MD','-MF',HO+'.d','-c','../source/tools/gtos_isolate_diagnostic/runtime/heap.cc','-o',HO],'Original successful heap compile uses identical typed flags')
    require(not any(flag.startswith(('@','--config','--sysroot','-ivfsoverlay','-fplugin','-fpass-plugin')) for flag in comparable),'No response/config/plugin side inputs')
    old_pre=H/'heap.prepass.d';old_md=H/'build'/(HO+'.d')
    # These two hashes are explicitly CURRENT observations, never historical pins.
    bind(old_pre,'cd2f4fb6e8dd247b2de0d18d92c6fe3f0a49b57d563a6c92d36fc048e08a788f');bind(old_md,'777146efac88d4a61675c90eb95a345f3892af19f545d48cd1e25e03eed23100')
    require(str(old_pre) not in h['inputs'] and str(old_md) not in h['inputs'],'Heap old depfiles are not historically pinned')
    old_md_items=deps(old_md);old_actual={heap_dep(H/'build'/item,h) for item in old_md_items};require(str(PAYLOAD) not in old_actual and all('payload-lock.json' not in item for item in deps(old_pre)),'Aggregate payload was not a consumed provider dependency')
    bind(H/'build'/HO,HS)
    empty='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
    for name,digest in (('dependencies-heap',empty),('compile-heap',empty),('symbols-heap','2ef588c5ae7a6255299f553f4199d20a51efe735b31c9fe1b50baedd360d5532'),('undefined-heap','58d42556b59c9968e578fcac041c631aedb3c0aa1d1798e771eddf304704154a')):command(h,name);bind(H/(name+'.log'),digest)
    state['stage']='exit_readonly_preflight_before_any_Clang';emit()
    x=read(X/'status.json','c343334d8af5e16e91b1f5d83053b407032217fdfb1e08936d37f224b2faaa57');x5=read(X.parent/'compile-thread-05/status.json','677b09eaffff215d9c9a233d82a85db6b12991b963fbba8dcf4b550f67b083f4')
    require(x['all_compile_pass'] is False and len(x['objects'])==37 and len(x['commands'])==150 and 'failure' not in x,'Exact original partial exit aggregate identity')
    require(x['objects']['exit-native_exit_libc.process-exit']['path']==str(X/'build'/XO) and x['objects']['exit-native_exit_libc.process-exit']['sha256']==XS and x['objects']['exit-native_exit_libc.process-exit']['gn_output']==XO,'Exact original successful exit unit object')
    for name in ('dependencies-exit-native_exit_libc.process-exit','compile-exit-native_exit_libc.process-exit','symbols-exit-native_exit_libc.process-exit','undefined-exit-native_exit_libc.process-exit'):command(x,name)
    require(all(x5[key] for key in ('all_compile_pass','bound_inputs_unchanged','gn_qualified','existing_gn_flags_matched','input_symlinks_unchanged','full_system_header_dependencies','preserved_timeout_cache_requalified')),'Accepted later successful exit cache requalification')
    require(h['inputs'].get(str(CC))==x['inputs'].get(str(CC))==x5['inputs'].get(str(CC))==CC_SHA,'Both original provider compilations and accepted exit cache pin the exact current Clang bytes')
    require('input_symlinks' not in x,'Original partial exit aggregate did not record a symlink-node table')
    state.update(original_exit_aggregate_complete=False,original_exit_aggregate_symlink_table_present=False)
    require(x['inputs'].get(str(XG/'status.json'))=='c1e302ea0d02b17374fbad2d89d6c1f18f8850ca8fd43df0f0ca0d6001be2299' and x['inputs'].get(str(XG/'compdb.log'))=='15d05ed5b6ec1dfd81a98af35978cd8abb0174e9aae6f775f61ee4d312ca3706','Exit GN status and compdb are historically pinned provenance data')
    xgn=read(XG/'status.json','c1e302ea0d02b17374fbad2d89d6c1f18f8850ca8fd43df0f0ca0d6001be2299');require(xgn['all_checks_pass'] and xgn['bound_inputs_unchanged'] and xgn['engine_args_byte_identical'] and xgn['new_object_flags_match_reviewed_runtime_config'],'Accepted actual exit GN graph provenance')
    exit_owner=XG/'build/obj/tools/gtos_isolate_diagnostic/native_exit_libc.ninja';bind(exit_owner,'1f559aeb3cfad35fd57fab7938b87a3916b6c17855ce577206b4469dde63b541')
    exit_lines=exit_owner.read_text().splitlines();require(exit_lines.count('build '+XO+': cxx ../source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc')==1,'Exact current observed exit GN owner member')
    for key,value in EXIT_FIELDS.items():require(exit_lines.count(key+' = '+value)==1,'Independent typed exit GN field mismatch: '+key)
    exit_graph_flags=PREFIX+[token for key in ('defines','include_dirs','cflags','cflags_cc') for token in shlex.split(EXIT_FIELDS[key])]
    exit_flags=[('-fcrash-diagnostics-dir=../crash' if flag=='-fcrash-diagnostics-dir=../source/out/clang-crashreports' else flag) for flag in exit_graph_flags]
    exit_compdb=XG/'compdb.log';bind(exit_compdb,'15d05ed5b6ec1dfd81a98af35978cd8abb0174e9aae6f775f61ee4d312ca3706');exit_rows=[row for row in json.loads(exit_compdb.read_text()) if row['file']=='../source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc' and row['output']==XO]
    require(len(exit_rows)==1 and exit_rows[0]['directory']==str(XG/'build') and shlex.split(exit_rows[0]['command'])==[str(CC)]+exit_graph_flags+['-MMD','-MF',XO+'.d','-c','../source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc','-o',XO],'Typed actual exit GN/locked compdb profile')
    exit_compile=command(x,'compile-exit-native_exit_libc.process-exit');require(exit_compile['cwd']==str(X/'build') and exit_compile['argv']==[str(CC)]+exit_flags+['-MD','-MF',XO+'.d','-c','../source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc','-o',XO],'Original successful exit unit uses the complete independent typed GN profile')
    require(not any(flag.startswith(('@','--config','--sysroot','-ivfsoverlay','-fplugin','-fpass-plugin')) for flag in exit_flags),'No exit response/config/plugin side inputs')
    state.update(exit_GN_owner_sha256_current_observed=state['inputs'][str(exit_owner)],exit_GN_owner_historical_pin=False,exit_typed_GN_compdb_successful_compile_equal=True)
    cached=[row for row in x5['requalified_cache'] if row['name']=='exit-native_exit_libc.process-exit'];require(len(cached)==1 and cached[0]['path']==str(X/'build'/XO) and cached[0]['sha256']==XS and cached[0]['full_M_MD_closure_equal'],'Exact original exit object cache evidence')
    xp=X/'exit-native_exit_libc.process-exit.prepass.d';xd=X/'build'/(XO+'.d')
    require(x5['inputs'].get(str(xp))=='58adac6d3663a7ece3cb00167b3ba0c2b4097dee88d9283bade28c54964dfcc8' and x5['inputs'].get(str(xd))=='a794fba4c0998e4a2b8778d6d7bbb5d754d4285c84c41a6582981d257851b05b','Historically locked exact exit M and MD')
    bind(xp,x5['inputs'][str(xp)]);bind(xd,x5['inputs'][str(xd)]);bind(X/'build'/XO,XS)
    before_rows,before,before_multiset,before_multiplicity,before_physical=exit_sequence(xp,XG/'build',x,x5)
    after_rows,after,after_multiset,after_multiplicity,after_physical=exit_sequence(xd,X/'build',x,x5)
    require(before==after and before_physical==after_physical and [(row['key'],row['sha256']) for row in before_rows]==[(row['key'],row['sha256']) for row in after_rows] and before_multiset==after_multiset and before_multiplicity==after_multiplicity,'Locked historical M/MD text maps to identical ordered raw113/unique111 materialized source hashes')
    state['exit_locked_dependency_hashes']=before
    state.update(exit_locked_dependency_sequences={'M':before_rows,'MD':after_rows},exit_ordered_normalized_key_SHA_equal=True,exit_normalized_key_SHA_multiset_equal=True,exit_dependency_multiplicity=after_multiplicity,exit_lexical_alias_duplicates=DUPLICATE_KEYS)
    for name,digest in (('dependencies-exit-native_exit_libc.process-exit',empty),('compile-exit-native_exit_libc.process-exit',empty),('symbols-exit-native_exit_libc.process-exit','96c10316b094b04bfd182945e4bf37dbf427eed02e3edfd070a9e84cd25bcd30'),('undefined-exit-native_exit_libc.process-exit','2d0a5cede03c3e3f3c85e9414d865d659b5835c75ad772ea126def6b623bcd93')):command(x,name);bind(X/(name+'.log'),digest)
    bind(X.parent/'compile-thread-v3.py','954649a388e0953177563aef0c648308fc165643e28b679d417017502b3b8dd8');bind(X.parent/'compile-thread-v5.py','6fd2b4ebef1ac9b28c5fabbdccc86712ce6a1db636d53798a911faf98519db90')
    state.update(exit_locked_M_MD_requalified=True,exit_locked_M_MD_count=len(before_rows),exit_locked_M_MD_count_basis='raw entries in each historically locked complete M/MD depfile',exit_unique_dependency_count=len(before),provider_objects={'heap':{'path':str(H/'build'/HO),'sha256':HS},'exit':{'path':str(X/'build'/XO),'sha256':XS}},excluded_aggregate_payload={'path':str(PAYLOAD),'reason':'Not in either actual provider source dependency closure; aggregate source record retained only as immutable provenance data. No current replacement hash or aggregate unchanged claim.'},old_log_hashes_observed_now_not_claimed_as_historical_receipt_fields=True)
    require(not state['commands'] and not state['symlink_nodes'] and state['inputs'].get(str(X/'source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc'))=='7848755069a7d45ab00fd9896d883a9de78fc671c1f1e54cf946ef29a79c4791','Physical successful exit source identity, no legacy source nodes or compiler execution before preflight')
    require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Read-only materialized source preflight bytes changed')
    state.update(exit_readonly_preflight_before_Clang=True,exit_materialized_physical_sources_only=True,exit_legacy_source_view_consumed=False,exit_legacy_source_view_current_unchanged_claim=False);emit()
    for name in ('home','tmp','crash'):(OUT/name).mkdir()
    env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8','TZ':'UTC','HOME':str(OUT/'home'),'TMPDIR':str(OUT/'tmp')}
    new_pre=OUT/'heap-new-M.d';flags=[('-fcrash-diagnostics-dir='+str(OUT/'crash') if flag=='-fcrash-diagnostics-dir=../crash' else flag) for flag in comparable]
    argv=[str(CC)]+flags+['-M','-MF',str(new_pre),'-MT','provider-heap','../source/tools/gtos_isolate_diagnostic/runtime/heap.cc']
    require('-c' not in argv and '-o' not in argv and '-MD' not in argv and '-MMD' not in argv,'Exactly one preprocessing-only action; no object compilation')
    state['stage']='heap_new_independent_complete_M';emit();entry={'name':'heap-new-M','argv':argv,'cwd':str(H/'build'),'shell':False,'state':'starting'};state['commands'].append(entry)
    log=OUT/'heap-new-M.log'
    with log.open('xb') as stream:
        active=subprocess.Popen(argv,cwd=H/'build',env=env,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT);entry.update(state='running',pid=active.pid);emit()
        try:result=active.wait(timeout=300)
        except subprocess.TimeoutExpired:active.kill();active.wait();result=124
        active=None;entry.update(state='completed',returncode=result)
    require(result==0 and log.stat().st_size<1024*1024 and new_pre.is_file(),'Independent heap full-M failed')
    new_items=deps(new_pre);new_actual={heap_dep(H/'build'/item,h) for item in new_items}
    require(new_actual==old_actual and len(new_items)==len(old_md_items)==len(new_actual),'Independent complete M strictly matches old postMD actual physical closure')
    state['outputs'][str(new_pre)]=sha(new_pre);state['outputs'][str(log)]=sha(log);entry['log_sha256']=sha(log)
    state.update(heap_new_M_qualified=True,new_full_M_heap_count=len(new_actual),heap_old_depfile_hashes_historical=False,heap_old_depfile_hashes_current_observations={str(old_pre):sha(old_pre),str(old_md):sha(old_md)},heap_typed_GN_compdb_successful_compile_equal=True)
    new_exit=OUT/'exit-new-M.d';new_exit_flags=[('-fcrash-diagnostics-dir='+str(OUT/'crash') if flag=='-fcrash-diagnostics-dir=../crash' else flag) for flag in exit_flags]
    argv=[str(CC)]+new_exit_flags+['-M','-MF',str(new_exit),'-MT','provider-exit','../source/tools/gtos_isolate_diagnostic/runtime/process-exit.cc']
    require('-c' not in argv and '-o' not in argv and '-MD' not in argv and '-MMD' not in argv,'Exit action is a fixed complete preprocessing-only M; no object compilation')
    state['stage']='exit_new_independent_complete_M_from_physical_copy';emit();entry={'name':'exit-new-M','argv':argv,'cwd':str(X/'build'),'shell':False,'state':'starting'};state['commands'].append(entry)
    exit_log=OUT/'exit-new-M.log'
    with exit_log.open('xb') as stream:
        active=subprocess.Popen(argv,cwd=X/'build',env=env,shell=False,close_fds=True,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT);entry.update(state='running',pid=active.pid);emit()
        try:result=active.wait(timeout=300)
        except subprocess.TimeoutExpired:active.kill();active.wait();result=124
        active=None;entry.update(state='completed',returncode=result)
    require(result==0 and exit_log.stat().st_size<1024*1024 and new_exit.is_file(),'Independent exit full-M failed')
    exit_new_rows,exit_new_map,exit_new_multiset,exit_new_multiplicity,exit_new_physical=exit_sequence(new_exit,X/'build',x,x5)
    require(exit_new_map==after and exit_new_physical==after_physical and [(row['key'],row['sha256']) for row in exit_new_rows]==[(row['key'],row['sha256']) for row in after_rows] and exit_new_multiset==after_multiset and exit_new_multiplicity==after_multiplicity,'New exit complete-M exactly matches historically locked postMD ordered113/unique111 physical source closure, SHA and multiplicity')
    state['outputs'][str(new_exit)]=sha(new_exit);state['outputs'][str(exit_log)]=sha(exit_log);entry['log_sha256']=sha(exit_log)
    state.update(exit_new_M_qualified=True,new_full_M_exit_count=len(exit_new_physical),new_full_M_exit_raw_count=len(exit_new_rows),exit_new_dependency_sequence=exit_new_rows,exit_new_M_matches_locked_postMD=True)
    require(all(sha(path)==digest for path,digest in state['inputs'].items()) and all(str(pathlib.Path(path).resolve(strict=True))==actual for path,actual in state['aliases'].items()),'Provider inputs/aliases changed')
    for path,value in state['exit_physical_source_nodes'].items():
        row=pathlib.Path(path).lstat();require(not stat.S_ISLNK(row.st_mode) and (stat.S_ISREG(row.st_mode) if value['kind']=='file' else stat.S_ISDIR(row.st_mode)) and row.st_dev==value['device'] and row.st_ino==value['inode'],'Physical source ancestor/leaf type or identity changed')
    require(not state['symlink_nodes'] and len(state['commands'])==2 and all(row['state']=='completed' and row['returncode']==0 for row in state['commands']) and len(new_actual)==75 and len(exit_new_physical)==111 and len(exit_new_rows)==113,'Only two successful M commands, unchanged object sources and exact complete closures')
    require(all(info(pathlib.Path(path))==row for path,row in metadata.items()) and all(sha(path)==digest for path,digest in state['outputs'].items()),'Old cache metadata or new evidence changed')
    state.update(complete=True,provider_source_qualification_pass=True,bound_inputs_unchanged=True,aliases_unchanged=True,old_cache_metadata_unchanged=True,outputs_unchanged=True,recompiled_provider_objects=False,new_Clang_M_commands=2,stage='completed',scope='Provider-specific source qualification only. Heap gets independent full-M checked against old postMD/source hashes; its old depfile hashes are current observations, not historical attestations. Exit gets independent full-M using only preserved physical post-compile source copies and fixed compiler resource headers, all ancestor/leaf types and dual original/cache source hashes checked. Old exit M/MD text is historically locked mapping data; old GN source links are not consumed or claimed currently unchanged. Current observed exit owner fields equal independent typed fields, historically pinned compdb and original successful argv. Exact original object bytes unchanged. No aggregate payload hash substitution, no claim all old aggregate inputs remain unchanged, no CXX/object recompilation, link, native or browser acceptance.');code=0
except BaseException as error:
    if authorized:state.update(failure=str(error),traceback=traceback.format_exc())
    traceback.print_exc()
finally:
    if active is not None:
        if active.poll() is None:active.kill()
        result=active.wait();entry.update(state='completed',returncode=result,aborted=True)
    if authorized:state['returncode']=code;emit()
sys.exit(code)
