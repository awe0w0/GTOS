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
    assert record and pages<=256 and 0<=mode<8
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE CXA VIRTUAL BOOT\n')==1 and text.count('NATIVE CXA VIRTUAL GUEST PASS\n')==1
    assert text.count('GTOS NATIVE HEAP FAIL V1\n')==8
    assert text.count('Pure virtual function called!')==4 and text.count('Deleted virtual function called!')==4
    assert 'FAILED' not in text and 'GUEST FAIL' not in text and 'NATIVE USER FAULT' not in text and 'USER ERROR' not in text
    cases=rows(text,'CXA VIRTUAL CASE');reaps=rows(text,'CXA VIRTUAL REAP');final=rows(text,'CXA VIRTUAL FINAL')
    assert len(cases)==len(reaps)==8 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    keys={'mode','id','stage','error','checks','bytes','errno','writes','injected','entry_stage','entry_result',
          'cs','cr3','kernel','exit','vector','pages','cost','free','baseline'}
    reap_keys={'id','free','expected','fp_init','fp_invalid'};ids=set()
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        assert set(c)==keys and set(r)==reap_keys and c['mode']==mode
        assert 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['stage']==1 and c['error']==0 and c['checks']==4 and c['bytes']==(32 if mode&1 else 29)
        assert c['errno']==(1234 if mode<2 else 33) and c['writes']==2 and c['injected']==(mode>=2)
        assert c['exit']==0x48000021 and c['vector']==0 and c['entry_stage']==2 and c['entry_result']==0xffffffff and c['cs']==0x23
        assert 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and c['kernel']==cases[0]['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['fp_init']==r['fp_invalid']==mode+1
    f=final[0];assert set(f)=={'free','expected','reaps','trap_stack'}
    assert f['free']==f['expected']==baseline and f['reaps']==8 and 0<f['trap_stack']<4*4096-512
    return {'reaps':8,'checks':32,'cases':cases,'return_fault_controls':6,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ('NATIVE CXA VIRTUAL BOOT\n','NATIVE CXA VIRTUAL GUEST PASS\n','GTOS NATIVE HEAP FAIL V1\n',
                   'Pure virtual function called!','Deleted virtual function called!'):
        reject(text.replace(marker,'',1));reject(text+marker)
    reject(text+'UNEXPECTED FAILED\n')
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('CXA VIRTUAL CASE ','CXA VIRTUAL REAP ','CXA VIRTUAL FINAL ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            replacement='00000000' if match.group(2)=='FFFFFFFF' else 'FFFFFFFF'
            changed=line[:match.start(2)]+replacement+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
