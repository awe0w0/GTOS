import pathlib,re,struct

def geometry(path,mode):
    data=pathlib.Path(path).read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    assert data[:7]==b'\x7fELF\x01\x01\x01' and h[0]==2 and h[1]==3 and h[8]==32
    assert 0x40000000<=h[3]<0x40200000
    pages=0;record=False
    for i in range(h[9]):
        kind,offset,address,physical,size,memory,flags,alignment=struct.unpack_from('<8I',data,h[4]+i*32)
        if kind!=1:
            # PT_GNU_STACK and friends are loader notes, not loadable ranges.
            assert mode==6 and (kind>>24)==0x64
            continue
        assert size<=memory and offset+size<=len(data) and alignment==4096
        assert flags in (5,6) and physical==address
        if flags==5:
            assert address==0x40000000 and (memory<=0x40000 if mode<6 else memory>=0x40000)
        elif mode==6:
            # The libxml consumer carries no FILE record: its data segment is only checked as a bounded writable range.
            assert address>=0x40100000 and 0<size<=memory<=pow(2,21) and 0<offset<len(data)
            record=True
        else:
            assert address==0x40040000 and 72<=size<=memory<=4096
            assert struct.unpack_from('<2I',data,offset)==(1,mode)
            record=True
        pages+=(memory+4095)//4096
    assert record and pages<=256
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE C FILE BOOT\n')==1
    assert text.count('NATIVE C FILE GUEST PASS\n')==1 and 'FAIL' not in text
    cases=rows(text,'C FILE CASE');reaps=rows(text,'C FILE REAP');final=rows(text,'C FILE FINAL')
    libxml_all=rows(text,'C FILE LIBXML')
    libxml=[row for row in libxml_all if 'exit' in row];libxml_reaps=[row for row in libxml_all if 'expected' in row]
    assert len(cases)==len(reaps)==6 and len(final)==1 and len(libxml)==len(libxml_reaps)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    calls=0;ids=set()
    case_keys={'mode','id','stage','error','checks','calls','mask','png','cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline','open'}
    reap_keys={'id','free','expected','open','released','failures','fp_init','fp_invalid'}
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        assert set(c)==case_keys and set(r)==reap_keys
        assert c['mode']==mode and 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['error']==0 and 10000<c['checks']<20000 and 200<c['calls']<2000
        assert c['mask']==0x1f and c['png']==1108 and c['cs']==0x23
        assert 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        if mode>=4:
            assert c['stage']==4 and c['vector']==6 and c['exit']==0x80000006 and c['pf']==0 and c['address']==0
        elif mode==1:
            assert c['stage']==3 and c['vector']==14 and c['exit']==0x8000000e and c['pf']==7 and c['address']==0x100000
        else:
            assert c['stage']==(2 if mode==0 else 3) and c['vector']==c['pf']==c['address']==0 and c['exit']==(0 if mode==0 else 73)
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert c['open']==(0 if mode==0 else 1)
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['open']==0
        assert r['released']==mode and r['failures']==(0 if mode<3 else 1)
        assert r['fp_init']==r['fp_invalid']==mode+1
        calls+=c['calls']
    x=libxml[0];xr=libxml_reaps[0]
    assert set(x)=={'mode','id','stage','error','checks','calls','mask','png','cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline','open'}
    assert x['mode']==6 and 0<x['id']<16 and x['id'] not in ids
    assert x['error']==0 and x['exit']==0 and x['vector']==x['pf']==x['address']==0 and x['cs']==0x23
    assert x['cr3'] and x['cr3']!=x['kernel'] and not ((x['cr3']|x['kernel'])&4095)
    assert x['pages']==plans[6]['load_pages'] and x['baseline']==baseline
    assert set(xr)=={'id','free','expected','open'}
    assert xr['id']==x['id'] and xr['free']==xr['expected']==baseline and xr['open']==0
    assert 10<x['calls']<200 and (x['mask']&0x1f)==0x1f and not (x['mask']&~0xff) and x['checks']==0
    assert x['stage']==0 and x['png']==0 and x['open']==0
    # The process costs its load pages plus the heap it maps for the parser; the Reap below proves that heap comes back.
    assert x['baseline']-x['free']==x['cost']+plans[6]['heap_cost']
    calls+=x['calls']
    f=final[0]
    assert set(f)=={'free','expected','open','calls','io_errors','reads','writes','flushes','trap_stack','trap_calls'}
    assert f['free']==f['expected']==baseline and f['open']==0 and f['io_errors']==30
    # The trap counts every file operation each owner made; the file service counts the calls it actually served. They agree except for
    # operations the service refuses before counting them, which the libxml consumer can provoke and the six suites do not.
    assert f['trap_calls']==calls and calls<=f['calls']<=calls+8
    assert 0<f['reads']<1000000 and 0<f['writes']<100000 and 0<f['flushes']<10000 and 0<f['trap_stack']<4*4096-512
    return {'reaps':7,'released_handles':5,'failed_flush_reclaims':1,'io_errors':30,'png_bytes_per_process':1108,
            'checks':sum(c['checks'] for c in cases),'native_calls':calls,'cases':cases,'libxml':x,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ['NATIVE C FILE BOOT\n','NATIVE C FILE GUEST PASS\n']:
        reject(text.replace(marker,'',1));reject(text+marker)
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('C FILE CASE ','C FILE REAP ','C FILE FINAL ','C FILE LIBXML','C FILE LIBXML REAP ')):continue
        lines=text.splitlines(keepends=True)
        reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            changed=line[:match.start(2)]+'FFFFFFFF'+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
