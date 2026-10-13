import pathlib,re,struct

def geometry(path,mode):
    data=pathlib.Path(path).read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    assert data[:7]==b'\x7fELF\x01\x01\x01' and h[0]==2 and h[1]==3 and h[8]==32
    assert h[9]==2 and 0x40000000<=h[3]<0x40040000
    pages=0;record=False
    for i in range(h[9]):
        kind,offset,address,physical,size,memory,flags,alignment=struct.unpack_from('<8I',data,h[4]+i*32)
        assert kind==1 and size<=memory and offset+size<=len(data) and alignment==4096
        assert flags in (5,6) and physical==address
        if flags==5:assert address==0x40000000 and memory<=0x40000
        else:
            assert address==0x40040000 and 72<=size<=memory<=4096
            assert struct.unpack_from('<2I',data,offset)==(1,0);record=True
        pages+=(memory+4095)//4096
    assert record and pages<=256
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':0 if mode in (0,1,5) else 17}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE EXIT BOOT\n')==1
    assert text.count('GTOS NATIVE HEAP FAIL V1\n')==1
    assert all(line=='GTOS NATIVE HEAP FAIL V1' or
               re.fullmatch(r'EXIT CLOSE FAILURE kind=[0-9A-F]{8} bytes=[0-9A-F]{8} requested=[0-9A-F]{8}',line)
               for line in text.splitlines() if 'FAIL' in line)
    assert text.count('EXIT REGISTRY CHECKS PASS\n')==7
    assert text.count('EXIT LIFECYCLE CHECKS PASS\n')==3
    assert text.count('NATIVE EXIT GUEST PASS\n')==1
    cases=rows(text,'EXIT CASE');reaps=rows(text,'EXIT REAP');final=rows(text,'EXIT FINAL')
    assert len(cases)==len(reaps)==7 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    case_keys={'mode','id','stage','error','checks','registry','events','global','entry_stage','entry_result','calls','mask','cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline','open'}
    reap_keys={'id','free','expected','open','released','failures','fp_init','fp_invalid'}
    calls=0;released=0;ids=set();registry=cases[0]['registry']
    assert registry==529
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        normal=mode in (0,1,5)
        assert set(c)==case_keys and set(r)==reap_keys and c['mode']==mode
        assert 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['error']==0 and c['registry']==registry and registry<c['checks']<registry+64
        assert c['events']==(63 if normal else 3 if mode in (2,3) else 0)
        assert c['global']==(2 if normal else 1) and c['cs']==0x23
        assert c['calls']==(6 if normal else 2) and c['mask']==(0x15 if normal else 1)
        assert 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        if normal:
            result=17 if mode==0 else 29 if mode==1 else 31
            assert c['stage']==2 and c['exit']==c['entry_result']==result and c['entry_stage']==4
            assert c['vector']==c['pf']==c['address']==0
        elif mode==2:
            assert c['stage']==6 and c['exit']==0x8000000e and c['vector']==14 and c['pf']==7 and c['address']==0x100000
            assert c['entry_stage']==3 and c['entry_result']==17
        elif mode in (3,4):
            assert c['stage']==(6 if mode==3 else 7) and c['exit']==77 and c['vector']==c['pf']==c['address']==0
            assert c['entry_stage']==(3 if mode==3 else 2) and c['entry_result']==(17 if mode==3 else 0xffffffff)
        else:
            assert c['stage']==7 and c['exit']==0x48000021 and c['vector']==c['pf']==c['address']==0
            assert c['entry_stage']==2 and c['entry_result']==0xffffffff
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert c['open']==(0 if normal else 2)
        released+=0 if normal else 2
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['open']==r['failures']==0
        assert r['released']==released and r['fp_init']==r['fp_invalid']==mode+1
        calls+=c['calls']
    f=final[0]
    assert set(f)=={'free','expected','open','calls','io_errors','reads','writes','flushes','trap_stack'}
    assert f['free']==f['expected']==baseline and f['open']==0 and f['calls']==calls and f['io_errors']==2
    assert 0<f['reads']<1000000 and 0<f['writes']<100000 and 0<f['flushes']<10000 and 0<f['trap_stack']<4*4096-512
    close_failures=rows(text,'EXIT CLOSE FAILURE');assert len(close_failures)==2
    for kind,item in enumerate(close_failures):
        assert set(item)=={'kind','bytes','requested'} and item['kind']==kind
        assert item['requested']==(7 if kind==0 else 1) and 0<=item['bytes']<=item['requested']
    return {'reaps':7,'released_handles':released,'io_errors':2,'registry_checks_per_task':registry,
            'registry_checks':7*registry,'checks':sum(c['checks'] for c in cases),'native_calls':calls,'cases':cases,'close_failures':close_failures,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ('NATIVE EXIT BOOT\n','EXIT REGISTRY CHECKS PASS\n','EXIT LIFECYCLE CHECKS PASS\n','NATIVE EXIT GUEST PASS\n','GTOS NATIVE HEAP FAIL V1\n'):
        reject(text.replace(marker,'',1));reject(text+marker)
    reject(text.replace('GTOS NATIVE HEAP FAIL V1\n','GTOS NATIVE HEAP FAIL V2\n',1))
    reject(text+'UNEXPECTED FAIL\n')
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('EXIT CASE ','EXIT REAP ','EXIT FINAL ','EXIT CLOSE FAILURE ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            replacement='00000000' if match.group(2)=='FFFFFFFF' else 'FFFFFFFF'
            changed=line[:match.start(2)]+replacement+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
