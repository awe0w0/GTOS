import fs from 'node:fs';
const p = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a/worktree-147/tools/gtos-dsh-144/drive-144.mjs';
const t = fs.readFileSync(p, 'utf8');
const counts = new Map();
for (const m of t.matchAll(/[A-Za-z0-9_./$-]*144[A-Za-z0-9_./$-]*/g)) {
  const s = m[0];
  counts.set(s, (counts.get(s) || 0) + 1);
}
const rows = [...counts].sort((a, b) => b[1] - a[1]);
console.log('distinct tokens containing 144:', rows.length);
for (const [s, n] of rows.slice(0, 60)) console.log(String(n).padStart(4) + '  ' + s);
