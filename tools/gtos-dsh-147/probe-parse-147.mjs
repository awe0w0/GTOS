import fs from 'node:fs';
import { parseChannel, channelView } from './scene-contract-147.mjs';
const R = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const d = JSON.parse(fs.readFileSync(R + '/g-matrix-gm144-01-144.json', 'utf8'));
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
for (const scene of ['runtime-recovery-smp1', 'runtime-frame-recovery-smp1']) {
  const t = d.trials.find((x) => x.scene === scene);
  const txt = fs.readFileSync(toHost(t.serial.path), 'utf8');
  const p = parseChannel(txt);
  console.log('===', scene, 'trial', t.trial, 'lines', p.lines.length, 'numbers', p.numbers.size);
  const probe = scene === 'runtime-recovery-smp1'
    ? ['RECOVERY DIAG BASELINE ALLOCATED', 'RECOVERY DIAG BASELINE FREE', 'RECOVERY DIAG BASELINE DIGEST', 'RECOVERY DIAG CAPACITY']
    : ['FRAMEREC DIAG FINAL REAPED', 'FRAMEREC DIAG FINAL ALLOCATED', 'FRAMEREC DIAG SIBLING SLOT', 'FRAMEREC DIAG RECOVERED DIRECTORY'];
  for (const label of probe) {
    console.log('  ' + label + ' -> ' + JSON.stringify(p.numbers.get(label)));
    const hits = p.lines.filter((l) => l.startsWith(label + ' ')).slice(0, 3);
    console.log('      raw lines: ' + JSON.stringify(hits));
  }
}
