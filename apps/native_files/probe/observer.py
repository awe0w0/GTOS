"""Independent strict observer for actual CPL3 native-file guests."""
import re,struct,pathlib
CASE='mode id stage error checks mask calls bad png base vm file dir cs cr3 kernel exit vector pf address pages cost baseline free open'.split()
REAP='id free expected open reclaims released failures last fp_init fp_invalid fp_fail'.split()
FINAL='free expected checks calls open reclaims released failures fp_init fp_invalid fp_fail reads writes flushes io_errors trap_stack_observed'.split()
PEER='id error checks commands bad observed_bad calls observed_calls'.split()
UNAVAILABLE='attached calls observed_calls bad mask free expected open mounted reclaims released failures last fp_init fp_invalid fp_fail reads writes flushes'.split()
def fields(line,prefix,keys):
    pattern=re.escape(prefix)+''.join(' '+re.escape(k)+r'=([0-9A-F]{8})' for k in keys)
    match=re.fullmatch(pattern,line);assert match,'Unexpected observer line '+line
    return dict(zip(keys,[int(x,16) for x in match.groups()]))
def geometry(path):
    data=pathlib.Path(path).read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01'
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);assert h[0]==2 and h[1]==3 and h[8]==32 and h[9]==2
    pages=set();tables=set()
    for i in range(h[9]):
        p=struct.unpack_from('<8I',data,h[4]+i*32)
        typ,off,va,pa,filesz,memsz,flags,align=p
        assert typ==1 and 0x40000000<=va<0x40021000 and off+filesz<=len(data) and filesz<=memsz and flags in [5,6]
        if memsz:
            assert va+memsz<=0x40021000
            pages.update(range(va//4096,(va+memsz+4095)//4096));tables.update(range(va>>22,((va+memsz-1)>>22)+1))
    assert 0<len(pages)<=254;tables.add(0xbfffd000>>22)
    return {'pages':len(pages),'static_cost':len(pages)+2+len(tables)+1}
def verify(text,phase,plans):
    lines=text.splitlines();assert lines[0]=='NATIVE FILE BOOT '+phase and lines[-1]=='NATIVE FILE GUEST PASS'
    if phase in ['detached','unmounted']:
        assert len(lines)==3
        x=fields(lines[1],'FILE UNAVAILABLE',UNAVAILABLE);attached=int(phase=='unmounted')
        assert x['attached']==attached and x['calls']==32*attached and x['observed_calls']==32 and x['bad']==32 and x['mask']==0xffff
        assert x['free']==x['expected']>0 and x['reclaims']==attached and x['fp_init']==x['fp_invalid']==1
        assert all(x[k]==0 for k in ['open','mounted','released','failures','last','fp_fail','reads','writes','flushes'])
        return {'unavailable':x}
    assert phase in ['writer','reader'];reader=phase=='reader';modes=[5] if reader else [0,1,2,3,0]
    index=1;cases=[];reaps=[]
    baseline=None;used=set()
    for i,mode in enumerate(modes):
        id=1 if reader else i+2
        if mode==1:
            assert lines[index]=='' and lines[index+1]=='NATIVE USER FAULT id='+format(id,'08X')+' vector=0000000E';index+=2
        c=fields(lines[index],'FILE CASE',CASE);index+=1
        assert c['mode']==mode and c['id']==id and c['error']==0 and c['stage']==(3 if mode in [1,2,3] else 2)
        assert c['checks']==(0x2468 if reader else 0x461a+i) and c['calls']==(10 if reader else 0x226+i)
        assert c['bad']==(0 if reader else 116) and c['mask']==(0x4013 if reader else 0xffff) and c['png']==1108
        assert c['base']==0x80000000 and c['vm']==i+1 and c['cs']==0x23
        assert c['cr3']>=4096 and c['cr3']%4096==0 and c['kernel']>=4096 and c['kernel']%4096==0 and c['cr3']!=c['kernel']
        assert c['pages']==plans[mode]['pages'] and c['cost']==plans[mode]['static_cost']+3
        if baseline is None:baseline=c['baseline']
        assert c['baseline']==baseline and c['free']+c['cost']==baseline and c['open']==(0 if reader else 4)
        assert c['exit']==(0x8000000e if mode==1 else 73 if mode in [2,3] else 0)
        assert c['vector']==(14 if mode==1 else 0) and c['pf']==(4 if mode==1 else 0) and c['address']==(0x80003000 if mode==1 else 0)
        if reader:assert c['file']==c['dir']==0
        else:
            assert c['file']>0 and c['dir']>c['file'] and c['file'] not in used and c['dir'] not in used
            used.update([c['file'],c['dir']])
        r=fields(lines[index],'FILE REAP',REAP);index+=1
        assert r['id']==id and r['free']==r['expected']==baseline and r['open']==(0 if reader else 2)
        assert r['reclaims']==i+1 and r['released']==(0 if reader else 2*(i+1)) and r['failures']==int(i>=3 and not reader)
        assert r['last']==(0xfffffffb if mode==3 else 0) and r['fp_fail']==0 and r['fp_init']==id and r['fp_invalid']==i+1
        cases.append(c);reaps.append(r)
    peer=None
    if not reader:
        peer=fields(lines[index],'FILE PEER',PEER);index+=1
        assert peer=={'id':1,'error':0,'checks':112,'commands':10,'bad':100,'observed_bad':100,'calls':124,'observed_calls':124}
    f=fields(lines[index],'FILE FINAL',FINAL);index+=1
    assert index==len(lines)-1 and f['free']==f['expected']>0 and f['open']==f['fp_fail']==0
    assert f['free']==baseline+(0 if reader else plans[4]['static_cost'])
    assert f['calls']==sum(c['calls'] for c in cases)+(0 if reader else peer['calls'])
    assert f['reclaims']==f['fp_init']==f['fp_invalid']==(1 if reader else 6)
    assert f['released']==(0 if reader else 12) and f['failures']==(0 if reader else 1) and f['io_errors']==(0 if reader else 15)
    assert 0<f['trap_stack_observed']<16384-512 and f['checks']>(100 if reader else 45000)
    assert f['reads']>10 and (f['writes']==f['flushes']==0 if reader else f['writes']>100 and f['flushes']>20)
    return {'cases':cases,'reaps':reaps,'peer':peer,'final':f}
