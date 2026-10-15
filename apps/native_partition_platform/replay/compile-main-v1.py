#!/usr/bin/env python3
"""Compile one real V8 API consumer with the exact qualified GN flags."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
PACKET = W / 'artifacts/native-partition-consumer-20261016-a'
WORKLOAD = W / 'artifacts/native-v8-workloads-20261015-a'
HEAP = W / 'artifacts/native-large-heap-20261015-a'
OUT = PACKET / 'compile-main-01'
GN = W / 'artifacts/native-debug-shared-20261015-a/gn-01'
ROOT = OUT / 'source'
BUILD = OUT / 'build'
CLANG = W / 'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
NM = CLANG.parent / 'llvm-nm'
TOOLS = {CLANG: '656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',
         NM: '629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc'}
GN_STATUS_SHA = '386614c2f9fb5355af9cc26d387eab3d7891b2d9e057f13554ab03240e08dc97'
# Static reviewed flags. GN strings are compared, never executed.
FLAGS = shlex.split(r'''
-x c++ --target=i686-unknown-none-elf -ffreestanding -fno-stack-protector
-fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
-mno-mmx -msse2 -mfpmath=sse -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx
-femulated-tls -fstack-usage -nostdinc -nostdinc++
-DGTOS_ISOLATE_COMPILE_DIAGNOSTIC=1
-isystem ../source/tools/gtos_isolate_diagnostic/sdk
-isystem ../source/third_party/libc++/src/include
-isystem ../source/tools/gtos_isolate_diagnostic/c-sdk
-isystem /mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin/../lib/clang/24/include
-fno-exceptions -fno-rtti
-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D_FORTIFY_SOURCE=2
-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE
-D__GTOS__=1 -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0
-DV8_TYPED_ARRAY_MAX_SIZE_IN_HEAP=64 -DV8_LOGGING_LEVEL=1
-DV8_USE_METAGEN_INSTANCE_TYPES=0 -DBUILTIN_BLOCK_POSITION
-DV8_USE_HOST_CPU_ARM_FEATURES -DV8_ATOMIC_OBJECT_FIELD_WRITES
-DV8_ENABLE_LAZY_SOURCE_POSITIONS -DV8_WIN64_UNWINDING_INFO
-DV8_ENABLE_REGEXP_INTERPRETER_THREADED_DISPATCH -DV8_ENABLE_FUZZTEST
-DV8_JITLESS -DV8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
-DV8_ALLOCATION_FOLDING -DV8_ALLOCATION_SITE_TRACKING
-DV8_ADVANCED_BIGINT_ALGORITHMS -DV8_ENABLE_TDZ_HOLE -DV8_USE_ZLIB
-DV8_ENABLE_MAGLEV_GRAPH_PRINTER -DV8_ENABLE_EXTENSIBLE_RO_SNAPSHOT
-DV8_ENABLE_BLACK_ALLOCATED_PAGES -DV8_ENABLE_UNDEFINED_DOUBLE
-DV8_WASM_RANDOM_FUZZERS -DV8_ARRAY_BUFFER_INTERNAL_FIELD_COUNT=0
-DV8_ARRAY_BUFFER_VIEW_INTERNAL_FIELD_COUNT=0 -DV8_PROMISE_INTERNAL_FIELD_COUNT=0
-DV8_USE_DEFAULT_HASHER_SECRET=true -DV8_DEPRECATION_WARNINGS
-DV8_DISALLOW_JS_IN_API_INTERRUPTS_IS_CHECKED -DV8_IMMINENT_DEPRECATION_WARNINGS
-DV8_HAVE_TARGET_OS -DV8_TARGET_OS_GTOS -DCPPGC_SUPPORTS_OBJECT_NAMES
-DCPPGC_ENABLE_LARGER_CAGE -DCPPGC_SLIM_WRITE_BARRIER -DV8_TARGET_ARCH_IA32
-I../source/tools/gtos_isolate_diagnostic/runtime
-I../source/tools/gtos_isolate_diagnostic/runtime/sdk
-I../source -Igen
-Wall -Wextra -Wimplicit-fallthrough -Wextra-semi -Wunreachable-code-aggressive
-Wgnu -Wno-gnu-anonymous-struct -Wno-gnu-conditional-omitted-operand
-Wno-gnu-include-next -Wno-gnu-label-as-value -Wno-gnu-redeclared-enum
-Wno-gnu-statement-expression -Wno-gnu-zero-variadic-macro-arguments
-Wno-zero-length-array -Wthread-safety -Wno-unused-template
-Wno-missing-field-initializers -Wno-unused-parameter -Wno-psabi
-Wno-stringop-overread -Wloop-analysis -Wno-unneeded-internal-declaration
-Wno-cast-function-type -Wno-thread-safety-reference-return -Wno-nontrivial-memcall
-Wno-uninitialized-const-pointer -Wno-unused-but-set-global -Wshadow -Werror
-Werror=\#pragma-messages -fno-delete-null-pointer-checks -fno-strict-overflow
-fno-ident -fno-math-errno -fno-strict-aliasing -fcolor-diagnostics
-fdiagnostics-show-inlining-chain -fmerge-all-constants
-fcrash-diagnostics-dir=../crash -fno-lifetime-dse
-mllvm -instcombine-lower-dbg-declare=0 -mllvm -split-threshold-for-reg-with-hint=0
-ffp-contract=off -Wno-builtin-macro-redefined -D__DATE__= -D__TIME__= -D__TIMESTAMP__=
-no-canonical-prefixes -fomit-frame-pointer -g0
-fsanitize=array-bounds -fsanitize-trap=array-bounds
-fsanitize-ignore-for-ubsan-feature=array-bounds
-Wno-unsafe-buffer-usage-in-static-sized-array
-fsanitize=return -fsanitize-trap=return -fsanitize-ignore-for-ubsan-feature=return
-fsanitize=unreachable -fsanitize-trap=unreachable
-fsanitize-ignore-for-ubsan-feature=unreachable
-Wheader-hygiene -Wstring-conversion -Wtautological-overlap-compare
-Wunreachable-code -Wlifetime-safety-permissive -Wno-shadow
-Wctad-maybe-unsupported -Xclang -add-plugin -Xclang blink-gc-plugin
-Wno-invalid-offsetof -Wmissing-field-initializers -Wunnecessary-virtual-specifier
-O2 -fdata-sections -ffunction-sections -fno-unique-section-names
-Wno-invalid-offsetof
''')
# Explicit final flags; no command is obtained from a log or compdb.
FLAGS += [
    '-Wenum-compare-conditional',
    '-Wno-nullability-completeness', '-std=c++20', '-Wno-trigraphs',
    '-gsimple-template-names', '-fno-exceptions', '-fno-rtti']
SOURCES = {'main': ('../source/tools/gtos_isolate_diagnostic/main.cc',
                   '7ff57b86d0c1b9edbeeda07a543cfb2c622554d4259c6254655ae3bb0697db10')}
runtime_include = FLAGS.index('-I../source/tools/gtos_isolate_diagnostic/runtime')
codegen_begin = FLAGS.index('-fno-delete-null-pointer-checks')
codegen_end = FLAGS.index('-Wmissing-field-initializers', codegen_begin)
CPP_FLAGS = (FLAGS[:FLAGS.index('-D__STDC_CONSTANT_MACROS')]
    + FLAGS[FLAGS.index('-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE'):runtime_include]
    + ['-D_LIBCPP_BUILDING_LIBRARY', '-I../source/third_party/libc++/src/src']
    + FLAGS[runtime_include:FLAGS.index('-Wall')]
    + FLAGS[codegen_begin:codegen_end]
    + shlex.split(r'''
-O2 -fdata-sections -ffunction-sections -fno-unique-section-names
-Wno-pragma-clang-attribute -Wno-ignored-attributes -Wno-unreachable-code
-Wno-unreachable-code-aggressive -Wall -Wno-unused-variable -Wno-unused-template
-Wno-c++11-narrowing -Wno-unused-but-set-variable -Wno-missing-field-initializers
-Wno-unused-parameter -Wno-psabi -Wno-stringop-overread -Wloop-analysis
-Wno-unneeded-internal-declaration -Wno-cast-function-type -Wno-thread-safety-reference-return
-Wno-nontrivial-memcall -Wno-uninitialized-const-pointer -Wno-unused-but-set-global
-Werror -Werror=\#pragma-messages -Wno-exit-time-destructors -Wno-exit-time-destructors
-std=c++20 -Wno-trigraphs -gsimple-template-names -fno-exceptions -fno-rtti
-std=c++26
'''))
CPP_FLAGS += [FLAGS[FLAGS.index('-O2') - 3], '-Wenum-compare-conditional', '-Wno-nullability-completeness']
ENTRY_FLAGS = shlex.split(r'''
-x c++ --target=i686-unknown-none-elf -ffreestanding -fno-stack-protector
-fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
-mno-mmx -msse2 -mfpmath=sse -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx
-femulated-tls -fstack-usage -nostdinc -nostdinc++ -DGTOS_ISOLATE_COMPILE_DIAGNOSTIC=1
-isystem ../source/tools/gtos_isolate_diagnostic/sdk
-isystem ../source/third_party/libc++/src/include
-isystem ../source/tools/gtos_isolate_diagnostic/c-sdk
-isystem /mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/bin/../lib/clang/24/include
-fno-exceptions -fno-rtti -DV8_ARRAY_BUFFER_INTERNAL_FIELD_COUNT=0
-DV8_ARRAY_BUFFER_VIEW_INTERNAL_FIELD_COUNT=0 -DV8_PROMISE_INTERNAL_FIELD_COUNT=0
-DV8_USE_DEFAULT_HASHER_SECRET=true -DV8_DEPRECATION_WARNINGS
-DV8_DISALLOW_JS_IN_API_INTERRUPTS_IS_CHECKED -DV8_IMMINENT_DEPRECATION_WARNINGS
-DV8_HAVE_TARGET_OS -DV8_TARGET_OS_GTOS -DCPPGC_SUPPORTS_OBJECT_NAMES
-DCPPGC_ENABLE_LARGER_CAGE -DCPPGC_SLIM_WRITE_BARRIER -DABSL_ALLOCATOR_NOTHROW=1
-I../source/include -I../source/third_party/abseil-cpp -Igen/include
''')
FLAGS = [flag for flag in FLAGS if flag not in (
    '-I../source/tools/gtos_isolate_diagnostic/runtime',
    '-I../source/tools/gtos_isolate_diagnostic/runtime/sdk')]
FLAGS.insert(FLAGS.index('-Wall'), '-I../source/include')
OVERRIDES = {'tools/gtos_isolate_diagnostic/main.cc': (PACKET / 'patch/main.cc', SOURCES['main'][1])}
UNCHANGED = {}
state = {'all_compile_pass': False, 'guest_qualified': False, 'inputs': {}, 'input_symlinks': {}, 'objects': {}, 'commands': []}
authorized = False
known = {}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def bind(path, expected=None):
    logical = pathlib.Path(os.path.normpath(str(path)))
    path = logical.resolve(strict=True)
    require(W in path.parents, 'Input outside workspace')
    expected = state['inputs'].get(str(path), known.get(str(path))) if expected is None else expected
    digest = sha(path)
    require(expected is not None and expected == digest, 'Unbound/changed input: ' + str(path))
    previous = state['inputs'].get(str(path))
    require(previous is None or previous == digest, 'Input changed during run')
    state['inputs'][str(path)] = digest
    if logical != path:
        state['input_symlinks'][str(logical)] = str(path)
    return digest


def emit():
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


def dependencies(path):
    text = path.read_text().replace('\\\n', ' ')
    require(text.count(':') == 1, 'Unsupported dependency quoting')
    result = shlex.split(text.split(':', 1)[1])
    require(result and all('\\' not in item for item in result), 'Empty/unsupported dependencies')
    return result


def run(name, argv, cwd):
    tool = pathlib.Path(argv[0])
    require(tool in TOOLS and sha(tool) == TOOLS[tool], 'Unreviewed executable')
    with (OUT / (name + '.log')).open('xb') as stream:
        result = subprocess.run(argv, cwd=cwd, env=environment, shell=False, close_fds=True,
                                stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT, timeout=300)
    state['commands'].append({'name': name, 'argv': argv, 'cwd': str(cwd), 'returncode': result.returncode})
    emit()
    require(result.returncode == 0, 'Failed ' + name + '; inspect its retained log')
    print('Completed ' + name, flush=True)


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site, 'Isolated no-site fixed entry required')
    entries = {path.name for path in OUT.iterdir()}
    require({'guard.json', 'launcher.log'} <= entries <= {'guard.json', 'launcher.log', 'job.json'}, 'Fresh output required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__) and guard['launcher_sha256'] == sha(PACKET / 'launch-main-v1.py')
            and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
            and not guard['directory_execute_permissions'] and guard['writable_output'] == str(OUT), 'Guard mismatch')
    require(all(os.stat('/proc/self/ns/' + name).st_ino == guard['namespace_ids'][name] for name in ('user', 'net')),
            'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1, 'no_new_privs required')
    authorized = True
    bind(PACKET / 'main-before.cc', 'f0ecd9c784fcba75d642126e4a689c9ee4209127c1b975bb3f21b44d6a7dac29')
    bind(GN / 'status.json', GN_STATUS_SHA)
    gn_state = json.loads((GN / 'status.json').read_text())
    require(gn_state['all_checks_pass'] and gn_state['bound_inputs_unchanged'] and 'failure' not in gn_state,
            'GN graph is not qualified')
    bind(GN / 'compdb.log', '6fc46700cbeedad6bfda1ae9e7cefe422b4c5d239f1a03290bbd6653f61c8c5a')
    known.update(gn_state['inputs'])
    old_heap = W / 'artifacts/native-thread-heap-20261014-a/compile-01/status.json'
    bind(old_heap, '54f6b5055767770559ebd4cf3412ec96505a3643b55dcc7a5854de84dfaa5f6b')
    old_state = json.loads(old_heap.read_text())
    require(old_state['all_compile_pass'] and old_state['bound_inputs_unchanged'] and 'failure' not in old_state, 'Qualified heap required')
    known.update(old_state['inputs'])
    module = W / 'sources/GTOS/apps/native_v8_integration'
    # Publication changed only these metadata files; use their preserved,
    # digest-pinned snapshots instead of trusting updated hashes silently.
    snapshots = WORKLOAD / 'canonical-before-workloads/apps/native_v8_integration'
    bind(WORKLOAD / 'input-substitution-before-workloads.json',
         '3a04a44cf8103ad14e628def7808780ff777c76601c2b12120bb8fb76e3a7c2e')
    substitutions = json.loads((WORKLOAD / 'input-substitution-before-workloads.json').read_text())
    for name in ('payload-lock.json', 'source-lock.json'):
        record = substitutions[str(module / name)]
        require(record['snapshot'] == str(snapshots / name), 'Snapshot path differs from preserved map')
        bind(snapshots / name, record['sha256'])
    bind(snapshots / 'payload-lock.json', '3e90efe3cc20048c11c5e852e9f36e554aec4b270ae05608cbd3a330e77e06cb')
    payload = json.loads((snapshots / 'payload-lock.json').read_text())
    require(len(payload['files']) == 1306, 'Current payload changed')
    for relative, row in payload['files'].items():
        require(not pathlib.Path(relative).is_absolute() and '..' not in pathlib.Path(relative).parts, 'Payload path escaped')
        known[str(module / relative)] = row['sha256']
    bind(snapshots / 'source-lock.json', '87b2d7a4b24b859d59b85a6c1259a4628c8641726952b54230799fc94bdfa719')
    source_lock = json.loads((snapshots / 'source-lock.json').read_text())
    for relative, digest in source_lock['clang_resource_files'].items():
        require(not pathlib.Path(relative).is_absolute() and '..' not in pathlib.Path(relative).parts, 'Clang header path escaped')
        known[str((CLANG.parent.parent / 'lib/clang/24/include' / relative).resolve(strict=True))] = digest
    bind(HEAP / 'libcxx-header-provenance-01.json', '0c114841c434aae355635c2f02159c4078e18b5730701cd4d9e4f3c490bd46ef')
    provenance = json.loads((HEAP / 'libcxx-header-provenance-01.json').read_text())
    require(provenance['raw_git_blobs_verified'] and provenance['revision'] == '97b436da4c33663581d394f4ee0a5977fc38c2f4'
            and len(provenance['files']) == 71, 'Locked libc++ header provenance required')
    known.update(provenance['files'])
    bind(WORKLOAD / 'api-header-provenance-01.json', 'd7fc67b351c7ce2ca4788511d1a706c86a7db6b47d4609068306fd04db4a9188')
    api_headers = json.loads((WORKLOAD / 'api-header-provenance-01.json').read_text())
    require(api_headers['raw_git_blobs_verified'] and not api_headers['network_used']
            and api_headers['counts'] == {'97b436da4c33663581d394f4ee0a5977fc38c2f4': 511,
                                         'be042d4462bee463c9b785701b8c6ee4576ee0a3': 63}
            and len(api_headers['files']) == 574, 'Locked actual API header provenance required')
    known.update(api_headers['files'])
    # This header is the reviewed GTOS overlay, not an unmodified upstream blob.
    config_digest = payload['files']['overlay/include/v8config.h']['sha256']
    require(config_digest == '17a7c8540b117cc44659d84ac2fe1486a00ea0cc6b57c6b5b3dff7ed6a26bd6f',
            'Qualified GTOS platform header changed')
    # The graph was qualified separately; this small object stage binds every
    # actual pre-M/post-MD dependency rather than unrelated whole-graph inputs.
    rows = json.loads((GN / 'compdb.log').read_text())
    for path, digest in TOOLS.items():
        bind(path, digest)
    for name in ('home', 'tmp', 'crash', 'build', 'source'):
        (OUT / name).mkdir()
    environment = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC',
                   'HOME': str(OUT / 'home'), 'TMPDIR': str(OUT / 'tmp')}
    for relative, (replacement, digest) in OVERRIDES.items():
        bind(replacement, digest)
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(replacement, target)
        bind(target, digest)
    for name, (source, expected_sha) in SOURCES.items():
        base_flags = FLAGS
        output = 'obj/tools/gtos_isolate_diagnostic/consumer.main.o'
        selected = [row for row in rows if row['file'] == source and row['output'] ==
                    output]
        require(len(selected) == 1, 'Exactly one GN source row required')
        comparable = ['-fcrash-diagnostics-dir=../source/out/clang-crashreports'
                      if flag == '-fcrash-diagnostics-dir=../crash' else flag for flag in base_flags]
        expected = [str(CLANG)] + comparable + ['-MMD', '-MF', output + '.d', '-c', source, '-o', output]
        require(shlex.split(selected[0]['command']) == expected, 'Static flags differ from actual GN')
        revised_source = ROOT / source[len('../source/'):]
        require(sha(revised_source) == expected_sha, 'Qualified heap source changed')
        prepass = OUT / (name + '.prepass.d')
        prepass_flags = ['-fcrash-diagnostics-dir=' + str(OUT / 'crash')
                         if flag == '-fcrash-diagnostics-dir=../crash' else flag for flag in base_flags]
        run('dependencies-' + name, [str(CLANG)] + prepass_flags + ['-M', '-MF', str(prepass), '-MT', 'dependency-row', str(revised_source)], GN / 'build')
        expected_dependencies = set()
        for dependency_index, item in enumerate(dependencies(prepass)):
            original = GN / 'build' / item
            normalized = os.path.normpath(item)
            expected_digest = None
            if normalized.startswith('../source/'):
                relative = normalized[len('../source/'):]
                require('..' not in pathlib.PurePosixPath(relative).parts, 'Dependency escaped source')
                sdk_prefix = 'tools/gtos_isolate_diagnostic/'
                if relative.startswith((sdk_prefix + 'sdk/', sdk_prefix + 'c-sdk/')):
                    expected_digest = payload['files'][relative[len(sdk_prefix):]]['sha256']
                elif 'overlay/' + relative in payload['files']:
                    expected_digest = payload['files']['overlay/' + relative]['sha256']
                elif relative.startswith(('include/', 'third_party/libc++/src/include/')):
                    expected_digest = api_headers['files'].get(str(W / 'sources/v8-reference/v8' / relative))
                    require(expected_digest is not None, 'Unqualified upstream logical header: ' + relative)
            # Bind the actual resolved bytes against the qualified logical origin.
            # The GN view may contain either a symlink or a retained byte copy.
            digest = bind(original, expected_digest)
            override = [relative for relative, (path, _) in OVERRIDES.items() if pathlib.Path(item).is_absolute() and path == pathlib.Path(item)]
            if pathlib.Path(item).is_absolute() and ROOT in pathlib.Path(item).parents:
                target = pathlib.Path(item)
            elif override:
                require(len(override) == 1, 'Ambiguous private replacement')
                target = ROOT / override[0]
            elif normalized.startswith('../source/'):
                target = ROOT / normalized[len('../source/'):]
                require(ROOT in target.parents and '..' not in pathlib.PurePosixPath(normalized[len('../source/'):]).parts,
                        'Source copy escaped')
            elif normalized.startswith('gen/'):
                target = BUILD / normalized
                require(BUILD in target.parents and '..' not in pathlib.PurePosixPath(normalized).parts, 'Generated copy escaped')
            else:
                require(pathlib.Path(item).is_absolute() and str(pathlib.Path(item).resolve()).startswith(str(CLANG.parent.parent / 'lib/clang/24/include') + '/'),
                        'Unexpected dependency path')
                expected_dependencies.add(str(original.resolve(strict=True)))
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            require(ROOT in target.parent.resolve().parents or BUILD in target.parent.resolve().parents,
                    'Resolved destination escaped fresh output')
            require(not target.is_symlink(), 'Unexpected destination symlink')
            if not target.exists():
                shutil.copyfile(original, target)
            bind(target, digest)
            expected_dependencies.add(str(target.resolve(strict=True)))
            if dependency_index % 64 == 0:
                print('Bound/materialized dependency ' + str(dependency_index + 1), flush=True)
        require(all(sha(path) == digest for path, digest in state['inputs'].items()), 'Input changed before compile')
        (BUILD / output).parent.mkdir(parents=True, exist_ok=True)
        run('compile-' + name, [str(CLANG)] + base_flags + ['-MD', '-MF', output + '.d', '-c', source, '-o', output], BUILD)
        actual_dependencies = set()
        for item in dependencies(BUILD / (output + '.d')):
            path = (BUILD / item).resolve(strict=True)
            actual_dependencies.add(str(path))
            require(str(path) in state['inputs'], 'Dependency not bound before compile: ' + str(path))
            bind(path, state['inputs'][str(path)])
        require(actual_dependencies == expected_dependencies, 'Full M/MD closure changed')
        run('symbols-' + name, [str(NM), '-C', '--defined-only', output], BUILD)
        run('undefined-' + name, [str(NM), '-C', '--undefined-only', output], BUILD)
        defined = (OUT / ('symbols-' + name + '.log')).read_text().splitlines()
        undefined = {line.split()[-1] for line in (OUT / ('undefined-' + name + '.log')).read_text().splitlines() if line.split()}
        exported = {line.split(' T ', 1)[1] for line in defined if ' T ' in line}
        if name == 'heap':
            require({'malloc', 'calloc', 'free', 'realloc', 'posix_memalign', 'gtos_native_heap_query',
                     'gtos_native_heap_panic', 'gtos_native_heap_enable_large_allocations'} <= exported
                     and '__llvm_libc_errno' in undefined, 'Missing real heap API or errno provider')
        elif name == 'entry':
            require('NativeEntry' in exported and 'gtos_native_heap_enable_large_allocations' in undefined,
                    'Whole V8 entry must explicitly enable fallback')
        if name in UNCHANGED:
            require(sha(BUILD / output) == UNCHANGED[name], 'Header-dependent unchanged object bytes differ')
        require('NativeV8IsolateMain' in exported and any('native_v8_workload_mask' in line for line in defined)
                and any('native_v8_gc_callbacks' in line for line in defined), 'Missing actual workload entry/state')
        state['objects'][name] = {'path': str(BUILD / output), 'sha256': sha(BUILD / output), 'gn_output': output}
        emit()
    require(len(state['objects']) == 1 and all(sha(path) == digest for path, digest in state['inputs'].items()),
            'Incomplete compile or changed input')
    require(all(str(pathlib.Path(path).resolve(strict=True)) == actual for path, actual in state['input_symlinks'].items()),
            'Input alias changed')
    state.update(all_compile_pass=True, bound_inputs_unchanged=True, gn_qualified=True,
                 input_symlinks_unchanged=True, full_M_MD=True,
                 full_system_header_dependencies=True, shallow_materialized_source_view=True,
                 host_only_flag_change='fresh crash-report directory', dependency_flag_change='MMD to MD',
                 gn_all_input_snapshot_rehashed=False, input_scope='qualified GN/profile, tools and actual complete pre/post MD closure')
except BaseException as error:
    state['failure'] = str(error)
    if authorized:
        (OUT / 'exception.log').write_text(traceback.format_exc())
finally:
    if authorized:
        emit()
    print(json.dumps({key: value for key, value in state.items() if key not in ('inputs', 'commands')}, indent=2), flush=True)
sys.exit(0 if state['all_compile_pass'] and state.get('bound_inputs_unchanged') and 'failure' not in state else 1)
