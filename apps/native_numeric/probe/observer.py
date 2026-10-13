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
            # One qualified ELF starts at mode zero; the kernel patches only
            # this checked data word before each real CreateElf admission.
            assert struct.unpack_from('<2I',data,offset)==(1,0)
            record=True
        pages+=(memory+4095)//4096
    assert record and pages<=256
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17 if mode else 0}

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line))
            for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('NATIVE C FILE BOOT\n')==1
    assert text.count('TRUNCATE OPEN CHECKS PASS\n')==1
    assert text.count('NATIVE MATH CHECKS PASS\n')==6
    assert text.count('NATIVE C FILE GUEST PASS\n')==1 and 'FAIL' not in text
    cases=rows(text,'C FILE CASE');reaps=rows(text,'C FILE REAP');final=rows(text,'C FILE FINAL')
    assert len(cases)==len(reaps)==6 and len(final)==1
    baseline=cases[0]['baseline'];assert 0<baseline<mib*256
    assert text.count('STANDARD CONSOLE STDOUT\n')==6 and text.count('STANDARD CONSOLE STDERR\n')==6
    assert text.count('Q'*320+'\n')==6 and text.count('BUFFERED STANDARD STDOUT\n')==6
    assert text.count('ALLOCATED STANDARD STDERR\n')==6 and text.count('STANDARD STREAM CHECKS PASS\n')==6
    calls=0;ids=set()
    case_keys={'mode','id','stage','error','checks','calls','mask','png','std_checks','heap','format_checks','math_checks','cs','cr3','kernel','exit','vector','pf','address','pages','cost','free','baseline','open'}
    reap_keys={'id','free','expected','open','released','failures','fp_init','fp_invalid'}
    for mode,(c,r) in enumerate(zip(cases,reaps)):
        assert set(c)==case_keys and set(r)==reap_keys
        assert c['mode']==mode and 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        assert c['error']==0 and 10000<c['checks']<20000 and 200<c['calls']<2000
        assert c['std_checks']==46
        assert c['math_checks']==921
        assert 3000<c['format_checks']<4000
        assert c['heap']==(1 if mode else 0)
        assert c['mask']==0x3f and c['png']==1108 and c['cs']==0x23
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
        assert c['open']==(0 if mode==0 else 2)
        assert r['id']==c['id'] and r['free']==r['expected']==baseline and r['open']==0
        assert r['released']==2*mode and r['failures']==(0 if mode<3 else 1)
        assert r['fp_init']==r['fp_invalid']==mode+1
        calls+=c['calls']
    f=final[0]
    assert set(f)=={'free','expected','open','calls','io_errors','reads','writes','flushes','trap_stack'}
    assert f['free']==f['expected']==baseline and f['open']==0 and f['calls']==calls and f['io_errors']==42
    assert 0<f['reads']<1000000 and 0<f['writes']<100000 and 0<f['flushes']<10000 and 0<f['trap_stack']<4*4096-512
    for marker in ('FORMAT STDOUT integer=-7\n', 'FORMAT VSTDOUT precision=1.250\n',
                   'FORMAT STDERR integer=-7\n', 'FORMAT VSTDERR hex=1a\n',
                   'FORMAT FPRINTF ok -9\n', 'FORMAT VFPRINTF 0000001a\n'):
        assert text.count(marker)==6
    return {'reaps':6,'released_handles':10,'failed_flush_reclaims':1,'io_errors':42,'png_bytes_per_process':1108,'checks':sum(c['checks'] for c in cases),'format_checks':sum(c['format_checks'] for c in cases),'native_calls':calls,'cases':cases,'final':f}

def negative_controls(text,plans,mib):
    count=0
    def reject(mutated):
        nonlocal count
        try:verify(mutated,plans,mib)
        except (AssertionError,KeyError,ValueError,struct.error):count+=1
        else:raise AssertionError('Observer accepted mutated evidence')
    for marker in ['NATIVE C FILE BOOT\n','TRUNCATE OPEN CHECKS PASS\n','NATIVE C FILE GUEST PASS\n','NATIVE MATH CHECKS PASS\n']:
        reject(text.replace(marker,'',1));reject(text+marker)
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('C FILE CASE ','C FILE REAP ','C FILE FINAL ')):continue
        lines=text.splitlines(keepends=True)
        reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            changed=line[:match.start(2)]+'FFFFFFFF'+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
