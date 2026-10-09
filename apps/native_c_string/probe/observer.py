import re

def fields(line):
    pairs = re.findall(r'(\w+)=([0-9A-F]+)', line)
    assert len(pairs) == len(dict(pairs)), 'duplicate field'
    return {k: int(v, 16) for k, v in pairs}

def expected_counts():
    lengths = [0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024]
    edges = lengths + [1025, 2047, 2048, 2049, 4095, 4096]
    checks = 6 + 16 * 16 * sum(67 + 2 * n for n in lengths)
    checks += 16 * 25 * 13 * 1281 + 256 * 256 * 4 + sum(range(65))
    checks += 65 * 256 * 2 + 16 * sum(n + 4 for n in range(257)) + 48 + 4 * 65 * 10
    checks += sum(6 + 3 * n for n in edges) + 257 * 6 + 65 * 2
    cases = 16 * 16 * 27 + 16 * 25 * 13 + 256 * 256 + 65 * 256 + 16 * 257 + 48 + 4 * 65 + 33 + 257 + 65
    calls = [6978, 5233, 6945, 74561, 16673, 4369, 65793, 131329, 16897, 305, 4369, 2405, 325]
    return checks, cases, calls

def verify(log):
    assert log.count('NATIVE STRING DIAGNOSTIC BOOT\n') == log.count('NATIVE STRING DIAGNOSTIC PASS\n') == 1
    assert 'FAILED' not in log and 'DIAGNOSTIC FAIL' not in log and 'PANIC' not in log
    assert 'UNHANDLED INTERRUPT' not in log.replace('UNHANDLED INTERRUPT 0x00000027', '')
    lines = log.splitlines()
    records = [[int(v, 16) for v in l.split('words=', 1)[1].split()] for l in lines if l.startswith('STRING RECORD words=')]
    cases = [fields(l) for l in lines if l.startswith('STRING CASE ')]
    reaps = [fields(l) for l in lines if l.startswith('STRING REAP ')]
    finals = [fields(l) for l in lines if l.startswith('STRING FINAL ')]
    assert len(records) == len(cases) == len(reaps) == 5 and len(finals) == 1
    events = [l.split()[1] for l in lines if l.startswith(('STRING RECORD ', 'STRING CASE ', 'STRING REAP ', 'STRING FINAL '))]
    assert events == ['RECORD', 'CASE', 'REAP'] * 5 + ['FINAL']
    assert [c['mode'] for c in cases] == [0, 1, 2, 3, 0] and len({c['id'] for c in cases}) == 5
    checks, count, calls = expected_counts()
    for iteration, (w, c, r) in enumerate(zip(records, cases, reaps)):
        mode = c['mode']; repeats = c['repeats']
        assert c['stage'] == (2 if mode == 0 else 3) and c['error'] == 0 and c['id'] > 0
        assert len(w) == 29 and w[0] == 1 and w[1] == mode and w[2] == c['stage'] and w[3] == 0
        assert w[4] == c['checks'] == checks + repeats and w[5] == c['cases'] == count
        assert w[6] == 0x80000000 and w[7] > 0 and w[10:13] == [0x80005000, 65536, 8191]
        assert w[10] <= w[8] <= w[10] + w[11] - 4 and w[8] % 4 == 0 and w[9] == 0x5533
        assert w[13] == repeats and w[14] == w[15] == 0
        want = list(calls)
        if mode == 1: want[3] += 1
        if mode == 2: want[3] += repeats
        if mode == 3: want[2] += 1
        assert w[16:] == want
        assert repeats >= 100 if mode == 2 else repeats == 0
        assert c['cs'] == 0x23 and c['cr3'] >= 4096 and c['cr3'] != c['kernel_cr3']
        assert c['cr3'] % 4096 == c['kernel_cr3'] % 4096 == 0 and c['kernel_cr3'] >= 4096
        assert 0 < c['load_pages'] <= 256 and c['cost'] == c['load_pages'] + 5
        if mode in [1, 3]:
            assert (c['exit'], c['vector'], c['pf'], c['cr2']) == (0x8000000e, 14, 4 if mode == 1 else 6, 0x80004000)
        else:
            assert c['exit'] == (73 if mode == 2 else 0) and c['vector'] == c['pf'] == c['cr2'] == 0
        assert r['free'] == r['expected'] and r['fp_initialized'] == iteration + 2 and r['fp_invalidated'] == iteration + 1
        assert r['fp_saves'] > 0 and r['fp_restores'] > 0 and r['fp_failures'] == 0
    final = finals[0]
    assert final['free'] == final['expected'] and final['fp_initialized'] == final['fp_invalidated'] == 6 and final['fp_failures'] == 0
    assert final['free'] == reaps[-1]['free'] + 7
    return dict(cases=cases, records=records, reaps=reaps, final=final,independently_checked_cases=count*5,exact_reap_checks=6)
