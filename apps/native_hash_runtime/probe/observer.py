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
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17 if mode in (4,5,6) else 0}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE HASH BOOT\n')==1 and text.count('HASH RUNTIME CHECKS PASS\n')==8
    assert text.count('GTOS NATIVE HEAP FAIL V1\n')==2 and text.count('NATIVE HASH GUEST PASS\n')==1
    overflow='overflow_error was thrown in -fno-exceptions mode with message "__next_prime overflow"GTOS NATIVE HEAP FAIL V1'
    assert text.splitlines().count(overflow)==1 and text.splitlines().count('GTOS NATIVE HEAP FAIL V1')==1
    assert all(line in (overflow,'GTOS NATIVE HEAP FAIL V1') for line in text.splitlines() if 'FAIL' in line)
    cases=rows(text,'HASH CASE');reaps=rows(text,'HASH REAP');final=rows(text,'HASH FINAL')
    assert len(cases)==len(reaps)==8 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    case_keys={'mode','id','stage','error','checks','prime','hash','sort','container','entry_stage','entry_result','progress',
               'cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline'}
    reap_keys={'id','free','expected','fp_init','fp_invalid'};ids=set()
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        normal=mode in (0,7);retained=mode in (4,5,6)
        assert set(c)==case_keys and set(r)==reap_keys and c['mode']==mode
        assert 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['error']==0 and c['prime']==4106 and c['hash']==114 and c['sort']==4768 and c['container']==1416
        assert c['checks']==(11827 if mode==7 else 10411 if retained else 10409) and c['cs']==0x23
        assert 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        if normal:
            assert c['stage']==2 and c['exit']==c['entry_result']==0 and c['entry_stage']==4
            assert c['vector']==c['pf']==c['address']==c['progress']==0
        else:
            assert c['stage']==3 and c['entry_stage']==2 and c['entry_result']==0xffffffff
            if mode==1:assert c['exit']==0x48000021 and c['vector']==c['pf']==c['address']==c['progress']==0
            elif mode==2:assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==4 and c['address']==0x40041000 and c['progress']==0
            elif mode==3:
                assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==7
                assert 0x40000000<=c['progress']<0x40040000 and c['progress']<=c['address']<c['progress']+8
            elif mode==4:assert c['exit']==77 and c['vector']==c['pf']==c['address']==0 and c['progress']>0
            elif mode==5:assert c['exit']==0x48000010 and c['vector']==c['pf']==c['address']==c['progress']==0
            else:assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==7 and c['address']==0x100000 and c['progress']==0
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['fp_init']==r['fp_invalid']==mode+1
    faults=rows(text,'NATIVE USER FAULT');assert len(faults)==3
    for fault,mode in zip(faults,(2,3,6)):
        assert set(fault)=={'id','vector'} and fault['id']==cases[mode]['id'] and fault['vector']==14
    f=final[0];assert set(f)=={'free','expected','reaps','trap_stack'}
    assert f['free']==f['expected']==baseline and f['reaps']==8 and 0<f['trap_stack']<4*4096-512
    return {'reaps':8,'prime_checks_per_task':4106,'hash_checks_per_task':114,'sort_checks_per_task':4768,
            'container_checks_per_task':1416,'checks':sum(c['checks'] for c in cases),'cases':cases,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ('NATIVE HASH BOOT\n','HASH RUNTIME CHECKS PASS\n','NATIVE HASH GUEST PASS\n','GTOS NATIVE HEAP FAIL V1\n'):
        reject(text.replace(marker,'',1));reject(text+marker)
    reject(text.replace('GTOS NATIVE HEAP FAIL V1\n','GTOS NATIVE HEAP FAIL V2\n',1));reject(text+'UNEXPECTED FAIL\n')
    reject(text.replace('__next_prime overflow','__next_prime changed',1))
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('HASH CASE ','HASH REAP ','HASH FINAL ','NATIVE USER FAULT ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            # Running hash loop progress is intentionally nondeterministic; zero is always invalid.
            replacement='00000000' if match.group(1)=='progress' and ' mode=00000004 ' in line or match.group(2)=='FFFFFFFF' else 'FFFFFFFF'
            changed=line[:match.start(2)]+replacement+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
