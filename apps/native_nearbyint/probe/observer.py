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
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17 if mode in (2,3) else 0}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE NEARBYINT BOOT\n')==1 and text.count('NEARBYINT CHECKS PASS\n')==5
    assert text.count('NATIVE NEARBYINT GUEST PASS\n')==1 and not any('FAIL' in line for line in text.splitlines())
    cases=rows(text,'NEARBYINT CASE');reaps=rows(text,'NEARBYINT REAP');final=rows(text,'NEARBYINT FINAL')
    assert len(cases)==5 and len(reaps)==6 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    case_keys={'mode','id','stage','error','checks','numeric','calls','vectors','flag_states','context',
               'entry_stage','entry_result','progress','cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline'}
    reap_keys={'id','free','expected','fp_init','fp_invalid'};ids=set()
    for mode,(c,r) in enumerate(zip(cases,reaps[:5])):
        normal=mode in (0,1,4);retained=mode in (2,3)
        assert set(c)==case_keys and set(r)==reap_keys and c['mode']==mode
        assert 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['error']==0 and c['numeric']==1538 and c['calls']==256 and c['vectors']==32 and c['flag_states']==2
        assert c['context']==(42 if mode==4 else 0)
        assert c['checks']==(3081 if mode==1 else 1583 if mode==4 else 1543 if retained else 1541) and c['cs']==0x23
        assert 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        if normal:
            assert c['stage']==2 and c['exit']==c['entry_result']==0 and c['entry_stage']==4
            assert c['vector']==c['pf']==c['address']==c['progress']==0
        else:
            assert c['stage']==3 and c['entry_stage']==2 and c['entry_result']==0xffffffff
            if mode==2:assert c['exit']==77 and c['vector']==c['pf']==c['address']==0 and c['progress']>0
            else:assert c['exit']==0x8000000e and c['vector']==14 and c['pf']==7 and c['address']==0x100000 and c['progress']==0
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['fp_init']==r['fp_invalid']==mode+1
    faults=rows(text,'NATIVE USER FAULT');assert len(faults)==1
    assert set(faults[0])=={'id','vector'} and faults[0]['id']==cases[3]['id'] and faults[0]['vector']==14
    controls=rows(text,'NEARBYINT CONTROL');assert len(controls)==1;c=controls[0];r=reaps[5]
    keys={'mode','id','stage','error','checks','line','actual_lo','actual_hi','expected_lo','expected_hi','cw','mxcsr','sw',
          'entry_stage','entry_result','exit','vector','cs','cr3','kernel','pages','cost','free','baseline'}
    assert set(c)==keys and set(r)==reap_keys and c['mode']==5 and 0<c['id']<16 and c['id'] not in ids
    assert c['stage']==0 and c['error']==c['checks']==8 and 0<c['line']<200
    assert c['actual_lo']==c['actual_hi']==c['expected_lo']==c['expected_hi']==0
    assert not (c['cw']&0xc00) and not (c['mxcsr']&0x6000) and c['mxcsr']&0x20 and not (c['sw']&0x20)
    assert c['entry_stage']==2 and c['entry_result']==0xffffffff and c['exit']==91 and c['vector']==0 and c['cs']==0x23
    assert 0<c['cr3']<mib*1048576 and c['kernel']==cases[0]['kernel'] and c['cr3']!=c['kernel'] and not (c['cr3']&4095)
    assert c['pages']==plans[0]['load_pages'] and c['cost']==plans[0]['process_cost'] and c['baseline']==baseline and baseline-c['free']==c['cost']
    assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['fp_init']==r['fp_invalid']==6
    f=final[0];assert set(f)=={'free','expected','reaps','trap_stack'}
    assert f['free']==f['expected']==baseline and f['reaps']==6 and 0<f['trap_stack']<4*4096-512
    return {'reaps':6,'numeric_checks_per_task':1538,'calls_per_numeric_run':256,'vectors':32,'flag_states':2,
            'fp_context_checks':42,'checks':sum(c['checks'] for c in cases)+controls[0]['checks'],'cases':cases,'flag_control':controls[0],'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ('NATIVE NEARBYINT BOOT\n','NEARBYINT CHECKS PASS\n','NATIVE NEARBYINT GUEST PASS\n'):
        reject(text.replace(marker,'',1));reject(text+marker)
    reject(text+'UNEXPECTED FAIL\n')
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('NEARBYINT CASE ','NEARBYINT CONTROL ','NEARBYINT REAP ','NEARBYINT FINAL ','NATIVE USER FAULT ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            replacement='00000000' if match.group(1)=='progress' and ' mode=00000002 ' in line or match.group(2)=='FFFFFFFF' else 'FFFFFFFF'
            changed=line[:match.start(2)]+replacement+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
