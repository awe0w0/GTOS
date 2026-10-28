import fs from 'node:fs';
import crypto from 'node:crypto';
const WSL = '//wsl$/Ubuntu-20.04';
const L = '/var/tmp/gtos-dsh-147-e759-scene-bound';
const S = '/var/tmp/gtos-dsh-147-e759-scene-bound-view';
const host = (p) => WSL + p;
const rows = [];
for (const [name, p] of [['L', L], ['S', S]]) {
  const before = fs.existsSync(host(p));
  if (before) { rows.push({ name, path: p, existed: true, created: false }); continue; }
  fs.mkdirSync(host(p), { recursive: true });
  const st = fs.statSync(host(p));
  rows.push({ name, path: p, existed: false, created: true, is_dir: st.isDirectory(),
    nested_in_the_other: p.startsWith(L + '/') || p.startsWith(S + '/') });
}
const out = {
  task: 'GTOS-DSH-147',
  stage: 'setup-147-01',
  what: 'L and S measured absent, then registered and created by Node structured file work; not by direct WSL execution',
  created_by: 'node fs.mkdirSync on the //wsl$/Ubuntu-20.04 share path',
  direct_wsl_execution_used: false,
  independent_not_nested: rows.every((r) => r.nested_in_the_other === false || r.nested_in_the_other === undefined),
  rows,
  ok: rows.every((r) => r.created === true),
};
fs.writeFileSync('F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a/setup-147-01.json',
  JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify(out, null, 2));