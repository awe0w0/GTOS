import fs from 'node:fs';
const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const src = fs.readFileSync(R + '/worktree-147/tools/gtos-dsh-144/drive-144.mjs', 'utf8').split(/\r?\n/);
const phases = [];
src.forEach((l, i) => {
  const m = l.match(/(?:async function (phase\w*)|const (phase\w*)\s*=|function (phase\w*))/);
  if (m) phases.push((i + 1) + ': ' + l.trim().slice(0, 110));
});
console.log(phases.join('\n'));
console.log('\n=== route/stage specs written ===');
src.forEach((l, i) => { if (/route: '/.test(l)) console.log((i + 1) + ': ' + l.trim().slice(0, 140)); });