import re

DURATIONS = [0, 1, 999, 1000, 1001, 999999, 10000000, 10000001, 25000000, 0, 1, 15000001]
KINDS = [1] * 12

def fields(line):
    pairs = re.findall(r'(\w+)=([0-9A-F]+)', line)
    assert len(pairs) == len(dict(pairs)), 'duplicate field'
    return {k: int(v, 16) for k, v in pairs}

def mono_ns(ticks):
    return (ticks * 11931 * 1000000 // 1193182) * 1000

def verify(log):
    assert log.count('NATIVE C SLEEP SMOKE BOOT\n') == 1
    assert log.count('NATIVE C SLEEP SMOKE PASS\n') == 1
    assert 'FAILED' not in log and 'DIAGNOSTIC FAIL' not in log
    assert 'PANIC' not in log
    assert 'UNHANDLED INTERRUPT' not in log.replace('UNHANDLED INTERRUPT 0x00000027', '')
    lines = log.splitlines()
    cases = [fields(l) for l in lines if l.startswith('SLEEP CASE ')]
    events = [fields(l) for l in lines if l.startswith('SLEEP EVENT ')]
    reaps = [fields(l) for l in lines if l.startswith('SLEEP REAP ')]
    finals = [fields(l) for l in lines if l.startswith('SLEEP FINAL ')]
    assert len(cases) == len(reaps) == 5 and len(events) == 60 and len(finals) == 1
    assert [c['mode'] for c in cases] == [0, 1, 2, 3, 0]
    for iteration, (c, r) in enumerate(zip(cases, reaps)):
        mode = c['mode']
        assert c['stage'] == (2 if mode == 0 else 3)
        assert c['error'] == 0 and c['checks'] == 8851 and c['samples'] == 12
        assert c['rejected'] == 8 and c['errno'] == 0x5533
        assert c['cs'] == 0x23 and c['cr3'] >= 4096 and c['cr3'] != c['kernel_cr3'] and c['cr3'] % 4096 == c['kernel_cr3'] % 4096 == 0 and c['kernel_cr3'] >= 4096
        assert 0 < c['load_pages'] <= 256 and c['cost'] == c['load_pages'] + 5
        if mode == 1:
            assert (c['exit'], c['vector'], c['pf'], c['cr2']) == (0x8000000e, 14, 4, 0xbfffcffc)
        else:
            assert c['exit'] == (73 if mode >= 2 else 0) and c['vector'] == c['pf'] == c['cr2'] == 0
        if mode >= 2:
            assert c['wait_clocks'] >= 2 and c['wait_yields'] >= 1
        else:
            assert c['wait_clocks'] == c['wait_yields'] == 0
        for index, e in enumerate(events[iteration * 12:(iteration + 1) * 12]):
            assert e['index'] == index and e['kind'] == KINDS[index] and e['request'] == DURATIONS[index]
            assert e['before_calls'] == e['after_calls'] == 1
            assert e['before_ns'] < 1000000000 and e['after_ns'] < 1000000000
            before = e['before_seconds'] * 1000000000 + e['before_ns']
            after = e['after_seconds'] * 1000000000 + e['after_ns']
            assert before == mono_ns(e['before_ticks']) and after == mono_ns(e['after_ticks'])
            assert after - before >= e['request']
            if e['request'] == 0:
                assert e['clocks'] == e['yields'] == e['first_ticks'] == e['last_ticks'] == 0
            else:
                assert e['clocks'] >= 2 and e['yields'] > 0
                assert e['before_ticks'] <= e['first_ticks'] < e['last_ticks'] <= e['after_ticks']
                assert mono_ns(e['last_ticks']) - mono_ns(e['first_ticks']) >= e['request']
        assert r['free'] == r['expected']
        assert r['fp_initialized'] == iteration + 2 and r['fp_invalidated'] == iteration + 1
        assert r['fp_saves'] > 0 and r['fp_restores'] > 0 and r['fp_failures'] == 0
    final = finals[0]
    assert final['free'] == final['expected']
    assert final['fp_initialized'] == final['fp_invalidated'] == 6 and final['fp_failures'] == 0
    return dict(cases=cases, reaps=reaps, final=final, independently_checked_samples=len(events))
