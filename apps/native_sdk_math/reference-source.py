#!/usr/bin/env python3
"""Independent finite-bit reference data; no target, subprocess or host libm calls."""
import ctypes,decimal,hashlib,json,os,pathlib,struct,sys
from fractions import Fraction
W=pathlib.Path('/mnt/f/GTOS-Chromium');P=W/'artifacts/native-math-md5-20261017-a';OUT=P/'reference-01'
SIGN=1<<63;INF=0x7ff0000000000000;MAX=INF-1

def require(ok,why):
    if not ok:raise RuntimeError(why)

def sha(path):
    h=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def encode(value):return struct.unpack('>Q',struct.pack('>d',value))[0]
def decode(bits):return struct.unpack('>d',struct.pack('>Q',bits))[0]
def power(e):return Fraction(1<<e,1) if e>=0 else Fraction(1,1<<-e)

def positive(y):
    if not y:return [0]*4
    if y>=power(1024):return [INF,MAX,INF,MAX]
    e=y.numerator.bit_length()-y.denominator.bit_length()
    if y<power(e):e-=1
    scaled=y/power(max(e-52,-1074));q,r=divmod(scaled.numerator,scaled.denominator)
    lo=q if e<-1022 else ((e+1023)<<52)+q-(1<<52);hi=lo+bool(r)
    nearest=lo if 2*r<scaled.denominator else hi if 2*r>scaled.denominator else lo+(lo&1)
    return [nearest,lo,hi,lo]

def directed(y):
    if y>=0:return positive(y)
    v=positive(-y);return [v[0]|SIGN,v[2]|SIGN,v[1]|SIGN,v[3]|SIGN]

def reference(name,bits,precision):
    absolute=bits&~SIGN;negative=bool(bits&SIGN)
    if absolute>INF:return [INF|0x8000000000000]*4,'nan'
    if name=='round':
        if absolute>=INF:return [bits]*4,'infinity'
        x=Fraction.from_float(decode(bits));q,r=divmod(abs(x).numerator,abs(x).denominator)
        q+=2*r>=abs(x).denominator;out=positive(Fraction(q))[0]|(SIGN if negative else 0)
        return [out]*4,'round_finite'
    if name=='log':
        if absolute==0:return [INF|SIGN]*4,'pole'
        if negative:return [INF|0x8000000000000]*4,'domain'
        if absolute==INF:return [INF]*4,'infinity'
        if bits==encode(1.0):return [0]*4,'exact'
    else:
        if absolute==INF:return [0 if negative else INF]*4,'infinity'
        if absolute==0:return [encode(1.0)]*4,'exact'
        # Finite inputs outside these rational bounds are far beyond the
        # binary64 overflow/half-minimum boundaries; avoid Decimal exponent caps.
        if decode(bits)>1000:return [INF,MAX,INF,MAX],'finite'
        if decode(bits)<-1000:return [0,0,1,0],'finite'
    with decimal.localcontext() as context:
        context.prec=precision;x=decimal.Decimal.from_float(decode(bits))
        y=x.ln() if name=='log' else x.exp()
    return directed(Fraction(y)),'finite'

require(len(sys.argv)==1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,'Fixed isolated entry')
guard=json.loads((OUT/'guard.json').read_text());require(guard['child_sha256']==sha(__file__) and guard['launcher_sha256']==sha(P/'launch-reference-v1.py') and guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and not guard['directory_execute_permissions'] and guard['writable_output']==str(OUT),'Guard mismatch')
require(all(os.stat('/proc/self/ns/'+n).st_ino==guard['namespace_ids'][n] for n in ('user','net')) and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39,0,0,0,0)==1,'Namespace/no_new_privs mismatch')
common={0,SIGN,1,SIGN|1,2,SIGN|2,0xfffffffffffff,SIGN|0xfffffffffffff,0x10000000000000,SIGN|0x10000000000000,MAX,SIGN|MAX,INF,SIGN|INF,INF|0x8000000001234,SIGN|INF|0x8000000001234,INF|0x1234,SIGN|INF|0x1234}
cases={name:set(common) for name in ('log','exp','round')}
for name,values in {
 'log':[0.1,0.5,1.0,1.25,2.0,10.0,1024.0,-0.5,-1.0],
 'exp':[0.1,-0.1,0.5,-0.5,1.0,-1.0,10.0,-10.0,700.0,-700.0,-1000.0,1000.0],
 'round':[0.25,-0.25,0.5,-0.5,1.5,-1.5,2.5,-2.5,4503599627370496.0,-4503599627370496.0]
}.items():
    for value in values:
        bits=encode(value);cases[name].update((bits-1,bits,bits+1))
for bits in (0x3c90000000000000,0x3ca0000000000000,0x40862e42fefa39ef,0xc086232bdd7abcd2,0xc0874385446d71c3,0xc0874910d52d3052):
    cases['exp'].update((bits-1,bits,bits+1))
rows=[]
for function,name in enumerate(('log','exp','round')):
    for bits in sorted(cases[name]):
        a,kind=reference(name,bits,600);b,kind2=reference(name,bits,900)
        require(a==b and kind==kind2,'Independent precision disagreement')
        require(all(0<=v<(1<<64) for v in a),'Binary64 output encoding bounds')
        rows.append({'function':function,'input':bits,'expected':a,'nan':int(kind in ('nan','domain')),'kind':kind})
# Exact rational cases verify directed negative ordering, half-even, subnormal
# boundaries and sign-zero in the independent encoder itself.
require(directed(Fraction(-1)-power(-53))==[0xbff0000000000000,0xbff0000000000001,0xbff0000000000000,0xbff0000000000000],'Negative directed half-even')
require(positive(power(-1075))==[0,0,1,0] and positive(power(-1074))==[1]*4,'Subnormal rational boundaries')
require(reference('round',encode(-0.5),600)[0]==[encode(-1.0)]*4 and reference('round',SIGN,600)[0]==[SIGN]*4,'Half-away and sign-zero rational cases')
header=['// Independent 600/900-digit Decimal and exact rational reference; no target calls.',
        'struct NativeMathCase { unsigned function; unsigned long long input,expected[4]; unsigned nan; };',
        'static constexpr NativeMathCase nativeMathCases[] = {']
for row in rows:header.append('    {%d,0x%016xULL,{%s},%d},'%(row['function'],row['input'],','.join('0x%016xULL'%v for v in row['expected']),row['nan']))
header+=['};','static constexpr unsigned nativeMathCaseCount = sizeof(nativeMathCases)/sizeof(nativeMathCases[0]);']
(OUT/'vectors.h').write_text('\n'.join(header)+'\n')
result={'reference_data_pass':True,'case_counts':{name:len(cases[name]) for name in cases},'decimal_precisions':[600,900],'exact_rational_encoder_controls_pass':True,'input_nan_payloads_included':True,'sNaN_requires_raw_bits_native_ABI_call':True,'errno_and_exception_contract_not_inferred_from_approximation':True,'independent_target_not_called':True,'not_all_inputs_formal_proof':True,'rounding_order':['nearest_even','down','up','toward_zero'],'vectors_sha256':sha(OUT/'vectors.h'),'rows':rows}
(OUT/'status.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({key:value for key,value in result.items() if key!='rows'}),flush=True)
