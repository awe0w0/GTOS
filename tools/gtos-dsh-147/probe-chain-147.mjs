// GTOS-DSH-147: run judgeChain over a few real stages and print the facts, without writing a packet record. This exists
// so the judge can be exercised at a small scale while the guest matrix is still running, and so a defect in the judge is
// found before the one complete C record is written.
import { judgeChain, realRoots, pinsOf } from './c-chain-147.mjs';

const pins = pinsOf();
const roots = realRoots();
const stages = process.argv.slice(2);
if (!stages.length) throw new Error('usage: probe-chain-147.mjs <stage> [...]');
for (const stage of stages) {
  const j = judgeChain(roots, stage, pins);
  const bad = j.facts.filter((f) => !f.ok);
  console.log(JSON.stringify({ stage, ok: j.ok, facts: j.facts.length, held: j.facts.length - bad.length,
    problems: bad.map((f) => ({ id: f.id, detail: f.detail })).slice(0, 12) }, null, 2));
}
