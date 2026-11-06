#!/usr/bin/env python3
"""Build real C-toolchain/Rust floating ABI images; run them in another stage."""
import argparse
import hashlib
import json
import shlex
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
for option in ['view','clang-bin','core-root','out']:
    parser.add_argument('--'+option,required=True,type=Path)
for option in ['gn','ninja']:
    parser.add_argument('--'+option,required=True)
args = parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
repo = Path(__file__).resolve().parents[1]
rust = repo / 'tests/gtos_c_rust_float.rs'
c = repo / 'tests/gtos_c_rust_float.c'
spec = repo / 'apps/native_rust_target/i686-unknown-gtos.json'
toolchain = args.view / 'build/toolchain/gtos/BUILD.gn'
inputs = [rust,c,spec,toolchain,args.core_root/'libcore_core.rlib',
          args.core_root/'libcompiler_builtins_compiler_builtins.rlib']
before = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}


def run(root,name,command):
    result = subprocess.run(list(map(str,command)),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (root/(name+'.log')).write_text(result.stdout)
    if result.returncode:
        print(result.stdout,end='')
        raise SystemExit(result.returncode)
    return result.stdout


images = []
cases = [(mode,opt,False) for mode in ['sse2','x87'] for opt in [0,2]] + [('sse2',2,True)]
for mode,opt,bad in cases:
    name = mode+'-O'+str(opt)+('-bad-expectation' if bad else '')
    root = args.out/name
    root.mkdir()

    def write(rel,text):
        path = root/rel
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text)

    obj = root/'rust.o'
    run(root,'rust',[args.view/'third_party/rust-toolchain/bin/rustc','--edition=2024',
        '-Zunstable-options','--target',spec,'--crate-type=rlib','--emit=obj','-Ccodegen-units=1',
        '-Copt-level='+str(opt),'--extern','core='+str(args.core_root/'libcore_core.rlib'),
        '--extern','compiler_builtins='+str(args.core_root/'libcompiler_builtins_compiler_builtins.rlib'),
        '-L','dependency='+str(args.core_root),'-Dwarnings',rust,'-o',obj])
    write('.gn','buildconfig = "//config.gn"\n')
    write('config.gn','set_default_toolchain("//build/toolchain/gtos:clang_x86")\n')
    write('build/config/rust.gni','toolchain_has_rust = false\n')
    write('build/toolchain/gtos/BUILD.gn',toolchain.read_text())
    write('probe.c',c.read_text())
    write('memory.c','''/* Reporting fixture only: no host libc is linked. */
void *memcpy(void *to,const void *from,unsigned bytes) {
    volatile unsigned char *out=to;
    const volatile unsigned char *in=from;
    for(unsigned i=0;i<bytes;++i)out[i]=in[i];
    return to;
}
void *memset(void *to,int value,unsigned bytes) {
    volatile unsigned char *out=to;
    for(unsigned i=0;i<bytes;++i)out[i]=(unsigned char)value;
    return to;
}
''')
    write('entry.s','''.section .text._start,"ax",@progbits
.global _start
_start:
  fninit
  ldmxcsr initial_mxcsr
  andl $-16,%esp
  call TestMain
  movl %eax,%ebx
  movl $1,%eax
  int $0x80
  ud2
.section .rodata.cpu,"a",@progbits
.balign 4
initial_mxcsr: .long 0x1f80
.section .note.GNU-stack,"",@progbits
''')
    write('link.ld','''ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x40000000;
 .text : { KEEP(*(.text._start)) *(.text .text.*) } :text
 .rodata : { *(.rodata .rodata.*) } :text
 . = ALIGN(4096);
 .data : { *(.data .data.*) } :data
 .bss : { *(.bss .bss.* COMMON) } :data
 /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
''')
    flags = ['-O'+str(opt),'-Wall','-Wextra','-Werror']
    if mode=='x87':
        flags += ['-mno-sse','-mno-sse2','-mfpmath=387']
    if bad:
        flags += ['-DGTOS_FLOAT_BAD_EXPECTATION=1']
    write('BUILD.gn','''executable("float_probe") {
  sources = [ "probe.c", "memory.c", "entry.s" ]
  cflags = '''+json.dumps(flags)+'''
  inputs = [ '''+json.dumps(str(obj))+''' ]
  ldflags = [ "-T" + rebase_path("//link.ld",root_build_dir), '''+json.dumps(str(obj))+''' ]
}
group("default") { deps = [ ":float_probe" ] }
''')
    run(root,'generate',[args.gn,'gen',root/'out','--root='+str(root),
        '--args=gtos_clang_bin="'+str(args.clang_bin)+'"'])
    run(root,'build',[args.ninja,'-C',root/'out','-j2','-d','keepdepfile','float_probe.elf'])
    commands = run(root,'commands',[args.ninja,'-C',root/'out','-t','commands','float_probe.elf'])
    tokens = shlex.split(commands.splitlines()[0])
    assert '-msse2' in tokens and '-mfpmath=sse' in tokens
    assert ('-mfpmath=387' in tokens)==(mode=='x87')
    image = root/'out/float_probe.elf'
    data = image.read_bytes()
    assert data[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<HH',data,16)==(2,3)
    image.chmod(0o555)
    images.append({'path':str(image),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
        'mode':mode,'optimization':opt,'expected_return':1 if bad else 0,
        'expected_output':'C_RUST_FLOAT_ABI_FAIL\n' if bad else 'C_RUST_FLOAT_ABI_PASS 22\n'})
    print('FLOAT_ABI_BUILT',name,len(data),flush=True)
assert before == {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
with (args.out/'manifest.json').open('x') as handle:
    json.dump({'sources':before,'images':images,
        'scope':'Compiler C ABI in a user-mode emulator, not GTOS loader/FP context acceptance'},handle,indent=2)
