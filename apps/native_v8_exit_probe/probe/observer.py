import re

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib):
    assert text.count('ABSL EXIT BOOT\n')==1 and text.count('ABSL EXIT GUEST PASS\n')==1 and 'FAIL' not in text
    cases=rows(text,'ABSL EXIT CASE');reaps=rows(text,'ABSL EXIT REAP');final=rows(text,'ABSL EXIT FINAL')
    assert len(cases)==6 and len(reaps)==5 and len(final)==1
    ids=set()
    for mode,c in enumerate(cases):
        assert set(c)=={'mode','id','auto','tls','fini','entry','result','exit','vector','cs','cr3'}
        assert c['mode']==mode and 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        normal=mode==0
        assert c['auto']==c['tls']==c['fini']==int(normal)
        assert c['entry']==(4 if normal else 2) and c['result']==(17 if normal else 0xffffffff)
        assert c['exit']==(17 if normal else 0x80000006 if mode==4 else 73 if mode==5 else 1)
        assert c['vector']==(6 if mode==4 else 0) and c['cs']==0x23 and 0<c['cr3']<mib*1048576 and not c['cr3']%4096
    assert all(c['cr3']!=cases[5]['cr3'] for c in cases[:5])
    for mode,r in enumerate(reaps):
        assert set(r)=={'mode','cost','held','free','expected','open','released','peer_before','peer_after','ring_before','ring_after','boot_before','boot_after'}
        assert r['mode']==mode and r['cost']==r['held']==plans[mode]['process_cost']+17
        assert r['free']==r['expected'] and 0<r['free']<mib*256 and r['open']==2 and r['released']==2
        assert r['peer_after']!=r['peer_before'] and r['ring_after']!=r['ring_before'] and r['boot_after']>r['boot_before']
    f=final[0];assert set(f)=={'checks','free','expected','open','released','trap_stack'}
    assert f['free']==f['expected'] and 0<f['free']<mib*256 and f['open']==0 and f['released']==12 and 100<f['checks']<10000 and f['trap_stack']<4*4096-512
    assert all(r['expected']==f['expected']-plans[5]['process_cost']-17 for r in reaps)
    return {'cases':6,'target_reaps':5,'all_reaped':6,'frames':f['free'],'checks':f['checks'],'max_trap_stack':f['trap_stack']}

def negative_controls(text,plans,mib):
    count=0
    def reject(altered):
        nonlocal count
        try:verify(altered,plans,mib)
        except (AssertionError,KeyError,IndexError):count+=1;return
        raise AssertionError('Observer accepted mutated evidence')
    for marker in ['ABSL EXIT BOOT\n','ABSL EXIT GUEST PASS\n']:
        reject(text.replace(marker,'',1));reject(text+marker)
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('ABSL EXIT CASE ','ABSL EXIT REAP ','ABSL EXIT FINAL ')):continue
        lines=text.splitlines(keepends=True)
        reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            if match.group(1) in ['peer_before','peer_after','ring_before','ring_after','boot_before','boot_after']:continue
            changed=line[:match.start(2)]+('FFFFFFFE' if match.group(2)=='FFFFFFFF' else 'FFFFFFFF')+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
        if line.startswith('ABSL EXIT REAP '):
            values=rows(line,'ABSL EXIT REAP')[0]
            for before,after in [('peer_before','peer_after'),('ring_before','ring_after'),('boot_before','boot_after')]:
                changed=re.sub(r' '+after+r'=[0-9A-F]{8}', ' '+after+'='+format(values[before],'08X'),line)
                reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
