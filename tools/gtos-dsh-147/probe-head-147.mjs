import fs from 'node:fs';
import { parseChannel } from './scene-contract-147.mjs';
const R = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const d = JSON.parse(fs.readFileSync(R + '/g-matrix-gm144-01-144.json', 'utf8'));
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
const t = d.trials.find((x) => x.scene === 'runtime-init-smp1');
const p = parseChannel(fs.readFileSync(toHost(t.serial.path), 'utf8'));
console.log('lines:', p.lines.length);
for (const l of p.lines.slice(0, 20)) console.log('  | ' + l);
