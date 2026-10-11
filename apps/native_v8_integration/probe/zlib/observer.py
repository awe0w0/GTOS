import json
import re
from pathlib import Path

fixture=json.loads((Path(__file__).parent/'fixture-meta.json').read_text())

def rows(text,prefix):
    return [dict((k,int(v,16)) for k,v in re.findall(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})(?= |$)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]

def verify(text,plans,mib,have):
    assert have is True
    assert rows(text,'NATIVE ZLIB BOOT')==[{'feature':fixture['prefix_adler']}]
    assert text.count('NATIVE ZLIB GUEST PASS\n')==1 and 'FAIL' not in text
    cases=rows(text,'NATIVE ZLIB CASE');reaps=rows(text,'NATIVE ZLIB REAP');final=rows(text,'NATIVE ZLIB FINAL')
    assert len(cases)==6 and len(reaps)==5 and len(final)==1
    ids=set()
    for mode,c in enumerate(cases):
        assert set(c)=={'mode','id','auto','tls','fini','entry','result','exit','vector','cs','cr3','feature','reads','fault_error','fault_address','codec_result','codec_calls','codec_bytes','codec_failed','codec_frees','codec_before','codec_after'}
        assert c['mode']==mode and 0<c['id']<16 and c['id'] not in ids;ids.add(c['id'])
        normal=mode<2;fault=mode in [2,3]
        assert c['feature']==fixture['adler' if mode==0 else 'prefix_adler'] and c['reads']>0
        if mode==0:
            assert c['codec_result']==0xfffffffc and c['codec_calls']==5 and c['codec_frees']==4
            assert c['codec_bytes']==74164 and c['codec_failed']==65536
            assert 32768<c['codec_before']==c['codec_after']<=65504 and c['codec_before']%16==0
        else:assert all(c[k]==0 for k in ['codec_result','codec_calls','codec_bytes','codec_failed','codec_frees','codec_before','codec_after'])
        if mode in [0,1,5]:assert c['reads']>1
        assert c['auto']==c['tls']==c['fini']==int(normal)
        assert c['entry']==(4 if normal else 2) and c['result']==(17 if normal else 0xffffffff)
        assert c['exit']==(17 if normal else 29 if mode==4 else 73 if mode==5 else 0x8000000e)
        assert c['vector']==(14 if fault else 0) and c['cs']==0x23 and 0<c['cr3']<mib*1048576 and not c['cr3']%4096
        assert c['fault_error']==(6 if mode==2 else 7) if fault else c['fault_error']==0
        if fault:assert c['fault_address']==0x80010000 if mode==2 else any(lo<=c['fault_address']<hi for lo,hi in plans[mode]['readonly_segments'])
        else:assert c['fault_address']==0
    assert all(c['cr3']!=cases[5]['cr3'] for c in cases[:5])
    for mode,r in enumerate(reaps):
        assert set(r)=={'mode','cost','held','free','expected','open','released','peer_before','peer_after','ring_before','ring_after','boot_before','boot_after'}
        assert r['mode']==mode and r['cost']==r['held']==plans[mode]['process_cost']+17
        assert r['free']==r['expected'] and 0<r['free']<mib*256 and r['open']==3 and r['released']==(2 if mode<2 else 3)
        assert r['peer_after']!=r['peer_before'] and r['ring_after']!=r['ring_before'] and r['boot_after']>r['boot_before']
    f=final[0];assert set(f)=={'checks','free','expected','open','released','trap_stack'}
    assert f['free']==f['expected'] and 0<f['free']<mib*256 and f['open']==0 and f['released']==16 and 100<f['checks']<1000000 and 0<f['trap_stack']<4*4096-512
    assert all(r['expected']==f['expected']-plans[5]['process_cost']-17 for r in reaps)
    return {'cases':6,'target_reaps':5,'all_reaped':6,'frames':f['free'],'checks':f['checks'],'max_trap_stack':f['trap_stack']}

def negative_controls(text,plans,mib,have):
    count=0
    def reject(altered):
        nonlocal count
        try:verify(altered,plans,mib,have)
        except (AssertionError,KeyError,IndexError):count+=1;return
        raise AssertionError('Observer accepted mutated evidence')
    reject(text.replace('NATIVE ZLIB GUEST PASS\n','',1));reject(text+'NATIVE ZLIB GUEST PASS\n')
    for index,line in enumerate(text.splitlines(keepends=True)):
        if not line.startswith(('NATIVE ZLIB BOOT ','NATIVE ZLIB CASE ','NATIVE ZLIB REAP ','NATIVE ZLIB FINAL ')):continue
        lines=text.splitlines(keepends=True);reject(''.join(lines[:index]+lines[index+1:]))
        for match in re.finditer(r' ([a-z_][a-z_0-9]*)=([0-9A-F]{8})',line):
            if match.group(1) in ['peer_before','peer_after','ring_before','ring_after','boot_before','boot_after']:continue
            value='00000000' if match.group(1)=='reads' else 'FFFFFFFE' if match.group(2)=='FFFFFFFF' else 'FFFFFFFF'
            changed=line[:match.start(2)]+value+line[match.end(2):]
            reject(''.join(lines[:index]+[changed]+lines[index+1:]))
        if line.startswith('NATIVE ZLIB REAP '):
            values=rows(line,'NATIVE ZLIB REAP')[0]
            for before,after in [('peer_before','peer_after'),('ring_before','ring_after'),('boot_before','boot_after')]:
                changed=re.sub(r' '+after+r'=[0-9A-F]{8}', ' '+after+'='+format(values[before],'08X'),line)
                reject(''.join(lines[:index]+[changed]+lines[index+1:]))
    return count
