import re

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib,have):
    boot=rows(text,'ABSL SEED BOOT');assert len(boot)==1 and boot[0]=={'feature':int(have)}
    assert text.count('ABSL SEED GUEST PASS\n')==1 and 'FAIL' not in text
    cases=rows(text,'ABSL SEED CASE');reaps=rows(text,'ABSL SEED REAP');final=rows(text,'ABSL SEED FINAL')
    assert len(cases)==6 and len(reaps)==5 and len(final)==1
    ids=set()
    for mode,c in enumerate(cases):
        assert set(c)=={'mode','id','auto','tls','fini','entry','result','exit','vector','cs','cr3','feature','reads','fault_error','fault_address'}
        assert c['mode']==mode and 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        normal=mode<2 or (not have and mode<4)
        fault=have and mode in [2,3]
        assert c['feature']==int(have)
        assert c['reads']>(1 if mode==5 else 0)
        if mode!=5:assert c['reads']==(2 if mode in [0,2,3] else 4 if mode==1 else 1)
        assert c['auto']==c['tls']==c['fini']==int(normal)
        assert c['entry']==(4 if normal else 2) and c['result']==(17 if normal else 0xffffffff)
        assert c['exit']==(17 if normal else 29 if mode==4 else 73 if mode==5 else 0x8000000e)
        assert c['vector']==(14 if fault else 0) and c['cs']==0x23 and 0<c['cr3']<mib*1048576 and not c['cr3']%4096
        assert c['fault_error']==(6 if mode==2 else 7) if fault else c['fault_error']==0
        if fault:assert c['fault_address']==0x80010000 if mode==2 else 0x40000000<=c['fault_address']<0x40040000
        else:assert c['fault_address']==0
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

def negative_controls(text,plans,mib,have):
    count=0
    def reject(altered):
        nonlocal count
        try:verify(altered,plans,mib,have)
        except (AssertionError,KeyError,IndexError):count+=1;return
        raise AssertionError('Observer accepted mutated evidence')
    for marker in [next(line+'\n' for line in text.splitlines() if line.startswith('ABSL SEED BOOT ')),'ABSL SEED GUEST PASS\n']:
        reject(text.replace(marker,'',1));reject(text+marker)
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('ABSL SEED BOOT ','ABSL SEED CASE ','ABSL SEED REAP ','ABSL SEED FINAL ')):continue
        lines=text.splitlines(keepends=True)
        reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            if match.group(1) in ['peer_before','peer_after','ring_before','ring_after','boot_before','boot_after']:continue
            changed=line[:match.start(2)]+('00000000' if match.group(1)=='reads' else 'FFFFFFFE' if match.group(2)=='FFFFFFFF' else 'FFFFFFFF')+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
        if line.startswith('ABSL SEED REAP '):
            values=rows(line,'ABSL SEED REAP')[0]
            for before,after in [('peer_before','peer_after'),('ring_before','ring_after'),('boot_before','boot_after')]:
                changed=re.sub(r' '+after+r'=[0-9A-F]{8}', ' '+after+'='+format(values[before],'08X'),line)
                reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
