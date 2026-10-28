import fs from 'node:fs';
import { selftest, buildFixture, MUTATIONS, judgeScene } from './n-numeric-147.mjs';
import { parseChannel, channelView } from './scene-contract-147.mjs';
const R = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const d = JSON.parse(fs.readFileSync(R + '/g-matrix-gm144-01-144.json', 'utf8'));
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
const corpus = {};
for (const t of d.trials) {
  if (corpus[t.scene]) continue;
  corpus[t.scene] = { serial: fs.readFileSync(toHost(t.serial.path), 'utf8'), e9: fs.readFileSync(toHost(t.e9.path), 'utf8') };
}
const fx = buildFixture('workers-smp2', corpus['workers-smp2']);
const m = MUTATIONS.find((x) => x.id === 'value-expected-fraud');
const mutated = m.apply({ name: 'workers-smp2', sceneName: 'workers-smp2', channels: fx, other: fx });
console.log('mutated name:', mutated.name);
const v = judgeScene(mutated.name, mutated.channels);
console.log('judge ok:', v.ok, 'failures:', v.failures.length);
for (const f of v.failures.slice(0, 6)) console.log('   ', f.obligation, '--', f.detail);
const p = parseChannel(mutated.channels.serial);
const view = channelView(p);
const tickets = view.tickets();
console.log('tickets:', tickets.length, 'first:', JSON.stringify(tickets[0]));
console.log('first VALUE line:', p.lines.find((l) => l.startsWith('WORKER DIAG VALUE')));
console.log('first EXPECTED line:', p.lines.find((l) => l.startsWith('WORKER DIAG EXPECTED')));
