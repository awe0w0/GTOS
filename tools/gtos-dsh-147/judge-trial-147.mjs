// GTOS-DSH-147: judge one already-recorded trial's two real channels with the contract-driven numeric judge.
//
// This is a convenience wrapper around the same `judgeScene` the matrix judge calls. It exists so a single trial can be
// checked before a full matrix is launched: if the first cold start does not satisfy its scene's obligations, running
// twenty-eight of them only produces twenty-eight failures more slowly. It reads the channels from the paths the trial
// record itself names, and it does not fall back to any other file if one of them is missing.
import fs from 'node:fs';
import path from 'node:path';
import { judgeScene } from './n-numeric-147.mjs';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);

const trialPath = process.argv[2];
if (!trialPath) { console.error('usage: judge-trial-147.mjs <g-trial-<stage>-147.json> [...]'); process.exit(2); }
const files = process.argv.slice(2);

let allOk = true;
for (const f of files) {
  const t = JSON.parse(fs.readFileSync(f, 'utf8'));
  const read = (side) => {
    const p = t.channels && t.channels[side] ? t.channels[side].linux_path : null;
    if (!p) return null;
    if (!fs.existsSync(toHost(p))) return null;
    return fs.readFileSync(toHost(p), 'utf8');
  };
  const serial = read('serial');
  const e9 = read('e9');
  if (serial === null || e9 === null) {
    console.log('MISSING ' + t.stage + ' ' + t.scene + ': a channel named by the trial is not on disk');
    allOk = false;
    continue;
  }
  const v = judgeScene(t.scene, { serial, e9 });
  console.log((v.ok ? 'ok   ' : 'FAIL ') + t.stage + '  ' + t.scene
    + '  obligations=' + (v.counts ? v.counts.obligations : '?')
    + ' accepted=' + (v.counts ? v.counts.accepted : '?')
    + (v.ok ? '' : '  <- ' + (v.refused || (v.failures || []).slice(0, 4).map((x) => x.obligation + ': ' + x.why).join(' | '))));
  if (!v.ok) {
    for (const x of (v.failures || []).slice(0, 8)) console.log('       ' + x.obligation + ' :: ' + x.why);
    allOk = false;
  }
}
process.exit(allOk ? 0 : 1);
