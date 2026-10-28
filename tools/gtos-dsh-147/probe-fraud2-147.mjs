import fs from 'node:fs';
import { selftest, MUTATIONS, judgeScene, buildFixture } from './n-numeric-147.mjs';
import { SCENES } from './scene-contract-147.mjs';
const R = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const d = JSON.parse(fs.readFileSync(R + '/g-matrix-gm144-01-144.json', 'utf8'));
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
const corpus = {};
for (const t of d.trials) {
  if (corpus[t.scene]) continue;
  corpus[t.scene] = { serial: fs.readFileSync(toHost(t.serial.path), 'utf8'), e9: fs.readFileSync(toHost(t.e9.path), 'utf8') };
}
// reproduce the selftest's own fixture set and scene resolution
const fixtures = {};
for (const s of SCENES) { if (!corpus[s.name]) continue; fixtures[s.name] = buildFixture(s.name, corpus[s.name]); }
console.log('fixture keys:', Object.keys(fixtures).join(','));
const m = MUTATIONS.find((x) => x.id === 'value-expected-fraud');
const sceneName = { 'unknown-scene': 'runtime-init-smp1', 'wrong-refusal-reason': 'e99-priority',
  'esp0-aligned-but-not-a-stack': 'runtime-init-smp1', 'survivor-data-moved': 'runtime-recovery-smp1',
  'sibling-data-moved': 'runtime-frame-recovery-smp1', 'family-swapped': 'runtime-frame-recovery-smp1' }[m.id]
  || Object.keys(fixtures).find((n) => /^workers-smp/.test(n) && m.id === 'value-expected-fraud')
  || Object.keys(fixtures)[0];
console.log('resolved sceneName:', sceneName);
const otherName = Object.keys(fixtures).find((n) => n !== sceneName);
const mutated = m.apply({ name: sceneName, sceneName, channels: fixtures[sceneName], other: fixtures[otherName] });
console.log('mutated:', mutated && mutated.name);
const v = judgeScene(mutated.name, mutated.channels);
console.log('v.ok =', v.ok, 'failures', v.failures.length);
for (const f of v.failures.slice(0, 4)) console.log('  ', f.obligation, '--', f.detail);
