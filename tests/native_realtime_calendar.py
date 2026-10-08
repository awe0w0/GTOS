#!/usr/bin/env python3
"""Independent Gregorian UTC vectors; freestanding i386 needs no multilib libc."""
import argparse, calendar, datetime, hashlib, json, os, pathlib, shutil, subprocess, tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', nargs='?', type=pathlib.Path)
parser.add_argument('--sanitizers', action='store_true')
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
temporary = tempfile.TemporaryDirectory(prefix='gtos-realtime-calendar-') if args.output is None else None
out = pathlib.Path(temporary.name) if temporary else args.output.resolve()
if not temporary:
    assert not out.exists(), 'Choose a fresh evidence directory'
    out.mkdir(parents=True)
compiler = pathlib.Path(shutil.which(os.environ.get('CXX','g++'))).resolve()
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
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
paths = [pathlib.Path(__file__),repo/'tests/native_realtime_calendar.cpp',repo/'include/process/native_realtime.h',
    repo/'include/process/realtime_abi.h',repo/'include/process/clock_abi.h',repo/'include/common/types.h',vectors,shim,compiler]
inputs = {str(p):sha(p) for p in paths}
state = dict(scope='Independent Python calendar oracle, pure RTC integer decoder; Linux host is not GTOS syscall proof',
    all_required_checks_pass=False,vector_count=len(rows),inputs=inputs,commands=[])
matrix = [('O0-32',['-m32','-O0']),('O2-32',['-m32','-O2']),('O0-64',['-m64','-O0']),('O2-64',['-m64','-O2'])]
if args.sanitizers:matrix.append(('O1-64-sanitized',['-m64','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']))
for tag,flags in matrix:
    exe = out/tag
    cmd = [str(compiler),*flags,'-std=c++11','-fno-builtin','-Wall','-Wextra','-Werror','-I'+str(repo/'include'),
        '-I'+str(out),str(repo/'tests/native_realtime_calendar.cpp')]
    if tag.endswith('-32'):
        cmd += ['-nostdlib','-static','-fno-pie','-no-pie','-fno-stack-protector','-fno-exceptions','-fno-rtti',
            '-fno-asynchronous-unwind-tables',str(shim)]
    cmd += ['-o',str(exe)]
    result = subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/(tag+'-build.log')).write_bytes(result.stdout)
    state['commands'].append(dict(argv=cmd,exit_code=result.returncode))
    if result.returncode:break
    env = dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    result = subprocess.run([str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    output = result.stdout.decode(errors='replace');(out/(tag+'-run.log')).write_text(output)
    state['commands'].append(dict(argv=[str(exe)],exit_code=result.returncode,output=output,sha256=sha(exe)))
    if result.returncode:break
    assert output == 'NATIVE REALTIME CALENDAR PASS vectors=4612 checks=27729\n'
    if tag.endswith('-32'):
        assert exe.read_bytes()[:7] == b'\x7fELF\x01\x01\x01'
        assert not subprocess.check_output(['nm','-u',str(exe)],text=True).strip()
    print(tag+': '+output.strip())
state['source_before_after_identical'] = all(sha(p)==h for p,h in inputs.items())
assert state['source_before_after_identical']
state['all_required_checks_pass'] = len(state['commands'])==2*len(matrix) and all(v['exit_code']==0 for v in state['commands'])
(out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
if temporary:temporary.cleanup()
raise SystemExit(0 if state['all_required_checks_pass'] else 1)
