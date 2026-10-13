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
    assert record and pages<=256 and 0<=mode<4
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17 if mode in (1,2) else 0}

def rows(text,prefix):
    result=[]
    for line in text.splitlines():
        if not line.startswith(prefix+' '):continue
        matches=re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line)
        row=dict((k,int(v,16)) for k,v in matches)
        assert len(matches)==len(row)
        result.append(row)
    return result

def verify(text,plans,mib):
    assert text.count('NATIVE FOPEN BOOT\n')==1 and text.count('FOPEN CHECKS PASS\n')==4
    assert text.count('NATIVE FOPEN GUEST PASS\n')==1 and 'FAIL' not in text and 'USER ERROR' not in text
    cases=rows(text,'FOPEN CASE');reaps=rows(text,'FOPEN REAP');final=rows(text,'FOPEN FINAL')
    assert len(cases)==len(reaps)==4 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    keys={'mode','id','stage','checks','unit_checks','loops','opens','calls','progress','open','entry_stage','entry_result',
          'cs','cr3','kernel','exit','vector','pages','cost','free','baseline'}
    reap_keys={'id','free','expected','open','released','reclaims','failures','fp_init','fp_invalid'};ids=set()
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        retained=mode in (1,2);loops=4 if mode==3 else 1
        assert set(c)==keys and set(r)==reap_keys and c['mode']==mode
        assert 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['unit_checks']==72 and c['loops']==loops and c['opens']==19*loops+retained
        assert c['checks']==72*loops+2+3*retained and c['opens']<=c['calls']<512 and c['open']==retained
        assert c['cs']==0x23 and 0<c['cr3']<mib*1048576 and 0<c['kernel']<mib*1048576
        assert c['cr3']!=c['kernel'] and c['kernel']==cases[0]['kernel'] and not ((c['cr3']|c['kernel'])&4095)
        if retained:
            assert c['stage']==3 and c['entry_stage']==2 and c['entry_result']==0xffffffff
            if mode==1:assert c['exit']==73 and c['vector']==0 and c['progress']>0
            else:assert c['exit']==0x8000000e and c['vector']==14 and c['progress']==0
        else:assert c['stage']==2 and c['entry_stage']==4 and c['entry_result']==c['exit']==c['vector']==c['progress']==0
        cost=plans[mode]['process_cost']+plans[mode]['heap_cost']
        assert c['pages']==plans[mode]['load_pages'] and c['cost']==cost and c['baseline']==baseline and baseline-c['free']==cost
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['open']==r['failures']==0
        assert r['released']==(0,1,2,2)[mode] and r['reclaims']==mode+1 and r['fp_init']==r['fp_invalid']==mode+1
    faults=rows(text,'NATIVE USER FAULT');assert len(faults)==1 and set(faults[0])=={'id','vector'}
    assert faults[0]['id']==cases[2]['id'] and faults[0]['vector']==14
    f=final[0];assert set(f)=={'free','expected','reaps','open','trap_stack','reads','writes','flushes'}
    assert f['free']==f['expected']==baseline and f['reaps']==4 and f['open']==0 and 0<f['trap_stack']<4*4096-512
    assert all(0<f[key]<0x100000 for key in ('reads','writes','flushes'))
    return {'reaps':4,'checks':sum(c['checks'] for c in cases),'cases':cases,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ('NATIVE FOPEN BOOT\n','FOPEN CHECKS PASS\n','NATIVE FOPEN GUEST PASS\n'):
        reject(text.replace(marker,'',1));reject(text+marker)
    reject(text+'UNEXPECTED FAIL\n')
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('FOPEN CASE ','FOPEN REAP ','FOPEN FINAL ','NATIVE USER FAULT ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            replacement='00000000' if match.group(2)=='FFFFFFFF' or match.group(1)=='progress' and ' mode=00000001 ' in line else 'FFFFFFFF'
            changed=line[:match.start(2)]+replacement+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
        first=re.search(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line)
        if first:reject(''.join(lines[:index]+[line.rstrip('\n')+first.group(0)+'\n']+lines[index+1:]))
    return count
