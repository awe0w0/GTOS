#!/usr/bin/env python3
"""Independent high-precision test data only; no subprocesses or target calls."""
import ctypes
import decimal
import hashlib
import json
import os
import pathlib
import struct
import sys
from fractions import Fraction

W=pathlib.Path('/mnt/f/GTOS-Chromium')
PACKET=W/'artifacts/native-exp2-20261014-a'
OUT=PACKET/'reference-02'

def require(value,message):
    if not value: raise RuntimeError(message)

def sha(path):
    digest=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()

def encode(value): return struct.unpack('>Q',struct.pack('>d',value))[0]
def decode(bits): return struct.unpack('>d',struct.pack('>Q',bits))[0]
def power(exponent): return Fraction(1<<exponent,1) if exponent>=0 else Fraction(1,1<<-exponent)

def directed(y):
    # Exact rational conversion of the independent Decimal approximation.
    # For positive values: [nearest-even,down,up,toward-zero].
    if y>=power(1024): return [0x7ff0000000000000,0x7fefffffffffffff,0x7ff0000000000000,0x7fefffffffffffff]
    exponent=y.numerator.bit_length()-y.denominator.bit_length()
    if y<power(exponent): exponent-=1
    scaled=y/power(max(exponent-52,-1074))
    q,r=divmod(scaled.numerator,scaled.denominator)
    lo=q if exponent<-1022 else ((exponent+1023)<<52)+(q-(1<<52))
    hi=lo+(r!=0)
    nearest=lo if 2*r<scaled.denominator else hi if 2*r>scaled.denominator else lo+(lo&1)
    return [nearest,lo,hi,lo]

def finite_reference(value,precision):
    if value.is_integer(): return directed(power(int(value)))
    with decimal.localcontext() as context:
        context.prec=precision
        x=decimal.Decimal.from_float(value)
        y=(x*decimal.Decimal(2).ln()).exp()
        return directed(Fraction(y))

require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode,'Fixed isolated entry')
guard=json.loads((OUT/'guard.json').read_text())
require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(PACKET/'launch-reference-v2.py')
    and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied']
    and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
require(all(os.stat('/proc/self/ns/'+name).st_ino==guard['namespace_ids'][name] for name in ('user','net')),'Namespace changed')
require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'no_new_privs required')
values=[0.0,-0.0,0.5,-0.5,0.1,-0.1,1.25,-1.25,10.5,-10.5,1.0/4096,-1.0/4096,
        0.015625,-0.015625,-1022.5,-1073.5,-1074.5,1023.5]
values += [float(e) for e in (-1076,-1075,-1074,-1073,-1023,-1022,-1021,-53,-1,1,52,53,100,1023,1024,1025)]
inputs={encode(value) for value in values}
for value in (-1075.0,-1074.0,-1022.0,1024.0):
    bits=encode(value); inputs.update((bits-1,bits,bits+1))
inputs.update((1,0x8000000000000001,0x7ff0000000000000,0xfff0000000000000,
               0x7ff8000000001234,0xfff8000000001234))
for bits in (2,3,0xfffffffffffff,0x10000000000000,0x10000000000001):
    inputs.update((bits,bits|0x8000000000000000))
rows=[]
for bits in sorted(inputs):
    value=decode(bits)
    special=(bits&0x7ff0000000000000)==0x7ff0000000000000
    nan=special and (bits&0xfffffffffffff)!=0
    if special:
        expected=[0x7ff8000000000000 if nan else 0 if bits>>63 else 0x7ff0000000000000]*4
    else:
        expected=finite_reference(value,400)
        require(expected==finite_reference(value,480),'Independent precision disagreement')
    range_errors=[]; required=[]
    for result in expected:
        overflow=not special and value>=1024
        underflow=not special and (value<=-1075 or (value<-1022 and not value.is_integer() and result<0x10000000000000))
        range_errors.append(int(overflow or underflow))
        tiny=0<(bits&0x7fffffffffffffff)<=0x10000000000000
        required.append(0x28 if overflow else 0x30 if underflow else 0x20 if tiny else 0)
    rows.append({'input':bits,'expected':expected,'range':range_errors,'required_flags':required,'nan':int(nan)})
# Certify analytically exact half exponents using squared rational brackets;
# this does not call the target algorithm or Decimal transcendental routines.
for row in rows:
    x=decode(row['input'])
    if x in (0.5,-0.5,10.5,-10.5,1023.5,-1022.5,-1073.5,-1074.5):
        lo_bits,hi_bits=row['expected'][1],row['expected'][2]
        def exact_double(bits):
            return Fraction.from_float(decode(bits))
        target=power(int(2*x))
        require(exact_double(lo_bits)**2<target<exact_double(hi_bits)**2,'Half exponent bracket mismatch')
header=['// Independent 400/480-digit Decimal reference; half powers rational-bracket checked.',
        'struct Exp2Case { unsigned long long input, expected[4]; unsigned range[4], required[4], nan; };',
        'static constexpr Exp2Case exp2Cases[] = {']
for row in rows:
    outputs=', '.join('0x%016xULL'%value for value in row['expected'])
    ranges=', '.join(str(value) for value in row['range'])
    flags=', '.join(str(value) for value in row['required_flags'])
    header.append('    {0x%016xULL, {%s}, {%s}, {%s}, %d},'%(row['input'],outputs,ranges,flags,row['nan']))
header.append('};')
(OUT/'vectors.h').write_text('\n'.join(header)+'\n')
result={'reference_data_pass':True,'case_count':len(rows),'decimal_precisions':[400,480],
    'rounding_order':['nearest_even','down','up','toward_zero'],'independent_target_not_called':True,
    'half_exponents_rational_brackets_verified':True,'not_a_formal_all_inputs_rounding_proof':True,
    'vectors_sha256':sha(OUT/'vectors.h'),'rows':rows}
(OUT/'status.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({key:value for key,value in result.items() if key!='rows'},indent=2),flush=True)
