#!/usr/bin/env python3
"""Independent Gregorian UTC vectors; freestanding i386 needs no multilib libc."""
import argparse, calendar, datetime, hashlib, json, os, pathlib, shutil, subprocess, tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', nargs='?', type=pathlib.Path)
parser.add_argument('--sanitizers', action='store_true')
parser.add_argument('--phase', choices=('all', 'build', 'run'), default='all')
parser.add_argument('--build-dir', type=pathlib.Path)
parser.add_argument('--i386-executor', type=pathlib.Path)
args = parser.parse_args()
if args.phase != 'all' and args.output is None:
    parser.error('build and run phases require an explicit fresh output')
if (args.phase == 'run') != (args.build_dir is not None):
    parser.error('--build-dir is required only for the run phase')
if args.i386_executor is not None and (not args.i386_executor.is_absolute() or
        not args.i386_executor.is_file() or not os.access(args.i386_executor, os.X_OK)):
    parser.error('--i386-executor must name an executable file by absolute path')
repo = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
expected_output = 'NATIVE REALTIME CALENDAR PASS vectors=4612 checks=27729\n'
matrix = [('O0-32',['-m32','-O0']),('O2-32',['-m32','-O2']),('O0-64',['-m64','-O0']),('O2-64',['-m64','-O2'])]
if args.sanitizers:
    matrix.append(('O1-64-sanitized',['-m64','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']))


def execute(tag, image, expected_sha, state):
    argv = [str(image)]
    if tag.endswith('-32') and args.i386_executor is not None:
        argv.insert(0, str(args.i386_executor))
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    result = subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    output = result.stdout.decode(errors='replace')
    (out / (tag + '-run.log')).write_text(output)
    state['commands'].append(dict(argv=argv, exit_code=result.returncode, output=output, sha256=expected_sha))
    if result.returncode != 0 or output != expected_output:
        print(tag + ': runtime or oracle failed', flush=True)
        return False
    print(tag + ': ' + output.strip(), flush=True)
    return True


def inputs_match(inputs):
    return all(pathlib.Path(path).is_file() and sha(path) == expected for path, expected in inputs.items())


def status(state):
    (out / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


temporary = tempfile.TemporaryDirectory(prefix='gtos-realtime-calendar-') if args.output is None else None
out = pathlib.Path(temporary.name) if temporary else args.output.resolve()
if not temporary:
    assert not out.exists(), 'Choose a fresh evidence directory'
    out.mkdir(parents=True)
if args.phase == 'run':
    build = args.build_dir.resolve(strict=True)
    if out == build or out in build.parents or build in out.parents:
        parser.error('the run output and build publication must be disjoint')
    receipt = build / 'publication.json'
    if receipt.is_symlink() or not receipt.is_file():
        parser.error('missing build publication')
    raw = receipt.read_bytes()
    published = json.loads(raw)
    tags = [tag for tag, _ in matrix]
    if [item['name'] for item in published['images']] != tags:
        parser.error('build publication does not match the selected matrix')
    def verify():
        if receipt.read_bytes() != raw or not inputs_match(published['inputs']):
            parser.error('build publication inputs changed')
        for image in published['images']:
            path = build / image['name']
            if (str(path) != image['path'] or path.is_symlink() or not path.is_file() or
                path.stat().st_mode & 0o222 or path.stat().st_size != image['bytes'] or sha(path) != image['sha256']):
                parser.error('published calendar image identity drift: ' + image['name'])
    verify()
    state = dict(scope='Published calendar consumer, Linux host oracle', phase='run', commands=[],
                 publication_sha256=hashlib.sha256(raw).hexdigest())
    for image in published['images']:
        if not execute(image['name'], build / image['name'], image['sha256'], state):
            break
    verify()
    state['source_before_after_identical'] = True
    state['all_required_checks_pass'] = len(state['commands']) == len(tags) and all(
        command['exit_code'] == 0 and command['output'] == expected_output for command in state['commands'])
    status(state)
    raise SystemExit(0 if state['all_required_checks_pass'] else 1)

compiler = pathlib.Path(shutil.which(os.environ.get('CXX','g++'))).resolve()
dates = {(1970,1,1,0,0,0)}
for year in (1970,1972,1999,2000,2001,2037,2038,2096,2099,2100,2400,9999):
    for month in range(1,13):
        for day in (1,calendar.monthrange(year,month)[1]):
            for hour in (0,11,12,23): dates.add((year,month,day,hour,59,59))
def byte(n,binary): return n if binary else (n//10)*16+n%10
rows = []
for fields in sorted(dates):
    year,month,day,hour,minute,second = fields
    expected = calendar.timegm(datetime.datetime(*fields).timetuple())*1000000
    for binary in (False,True):
        for full in (False,True):
            h = hour if full else hour%12 or 12
            raw = [byte(second,binary),byte(minute,binary),byte(h,binary)|(0x80 if not full and hour>=12 else 0),
                byte(day,binary),byte(month,binary),byte(year%100,binary),byte(year//100,binary),
                (4 if binary else 0)|(2 if full else 0),0x26,0x80]
            rows.append('    {{'+','.join(str(v) for v in raw)+'},'+str(expected)+'ULL}')
vectors = out/'calendar-vectors.inc'
vectors.write_text('static const Vector vectors[] = {\n'+',\n'.join(rows)+'\n};\n')
shim = out/'linux-i386-start.cc'
shim.write_text(r'''
extern "C" int main();
extern "C" int printf(const char* format, ...) {
    char buffer[160]; unsigned used=0;
    __builtin_va_list args; __builtin_va_start(args,format);
    while (*format && used<sizeof(buffer)-11) {
        if (format[0]=='%' && format[1]=='u') {
            unsigned value=__builtin_va_arg(args,unsigned); char digits[10]; unsigned n=0;
            do { digits[n++]=(char)('0'+value%10); value/=10; } while (value);
            while(n) buffer[used++]=digits[--n];
            format+=2;
        } else buffer[used++]=*format++;
    }
    __builtin_va_end(args);
    unsigned call=4,fd=1,length=used; const char* data=buffer;
    asm volatile("int $0x80":"+a"(call):"b"(fd),"c"(data),"d"(length):"memory","cc");
    return used;
}
asm(".global _start\n_start:\nxorl %ebp,%ebp\nandl $-16,%esp\ncall main\nmovl %eax,%ebx\nmovl $1,%eax\nint $0x80\nud2\n");
''')
# File-backed data avoids a BSS-only PT_LOAD in the IA32 emulator.
layout = out / 'linux-i386.ld'
layout.write_text('''ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); constants PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x08048000;
    .text : { *(.text*) } :code
    . = ALIGN(4096);
    .rodata : { *(.rodata*) } :constants
    . = ALIGN(4096);
    .data : { BYTE(0); *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
paths = [pathlib.Path(__file__),repo/'tests/native_realtime_calendar.cpp',repo/'include/process/native_realtime.h',
    repo/'include/process/realtime_abi.h',repo/'include/process/clock_abi.h',repo/'include/common/types.h',vectors,shim,layout,compiler]
inputs = {str(p):sha(p) for p in paths}
state = dict(scope='Independent Python calendar oracle, pure RTC integer decoder; Linux host is not GTOS syscall proof',
    all_required_checks_pass=False,phase=args.phase,vector_count=len(rows),inputs=inputs,commands=[])
images = []
for tag,flags in matrix:
    exe = out/tag
    cmd = [str(compiler),*flags,'-std=c++11','-fno-builtin','-Wall','-Wextra','-Werror','-I'+str(repo/'include'),
        '-I'+str(out),str(repo/'tests/native_realtime_calendar.cpp')]
    if tag.endswith('-32'):
        cmd += ['-nostdlib','-static','-fno-pie','-no-pie','-fno-stack-protector','-fno-exceptions','-fno-rtti',
            '-fno-asynchronous-unwind-tables','-Wl,-T,'+str(layout),str(shim)]
    cmd += ['-o',str(exe)]
    result = subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/(tag+'-build.log')).write_bytes(result.stdout)
    state['commands'].append(dict(argv=cmd,exit_code=result.returncode))
    if result.returncode:break
    if tag.endswith('-32'):
        assert exe.read_bytes()[:7] == b'\x7fELF\x01\x01\x01'
        assert not subprocess.check_output(['nm','-u',str(exe)],text=True).strip()
    if args.phase == 'build':
        exe.chmod(0o555)
    images.append(dict(name=tag, path=str(exe), bytes=exe.stat().st_size, sha256=sha(exe)))
    if args.phase == 'all' and not execute(tag, exe, images[-1]['sha256'], state):
        break
state['source_before_after_identical'] = inputs_match(inputs)
assert state['source_before_after_identical']
commands_per_case = 1 if args.phase == 'build' else 2
state['all_required_checks_pass'] = len(state['commands']) == commands_per_case * len(matrix) and all(
    command['exit_code'] == 0 and command.get('output', expected_output) == expected_output
    for command in state['commands'])
if args.phase == 'build' and state['all_required_checks_pass']:
    receipt = out / 'publication.json'
    with receipt.open('x') as stream:
        json.dump({'images': images, 'inputs': inputs}, stream, indent=2)
    receipt.chmod(0o444)
status(state)
if temporary:temporary.cleanup()
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
