import fs from 'node:fs';
import crypto from 'node:crypto';
const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const W = R + '/worktree-147';
const c = JSON.parse(fs.readFileSync(R + '/build-material-catalog-147.json', 'utf8'));
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => { try { return fs.statSync(p).isFile(); } catch { return false; } };

// The catalog's source_view is the view the 61 objects were compiled against. The worktree is the
// current source. A file that differs is a revision boundary; every object whose depfile names it
// cannot be reused, because its translation unit was compiled against other bytes.
const diff = [];
for (const row of c.source_view) {
  const p = W + '/' + row.logical;
  if (!exists(p)) { diff.push({ logical: row.logical, why: 'absent' }); continue; }
  const b = fs.readFileSync(p);
  if (sha(b) !== row.source.sha256 || b.length !== row.source.bytes) {
    diff.push({ logical: row.logical, why: 'differs', catalog: { bytes: row.source.bytes, sha256: row.source.sha256 }, current: { bytes: b.length, sha256: sha(b) } });
  }
}
const diffSet = new Set(diff.map((d) => d.logical));
console.log('source_view rows:', c.source_view.length, 'differing:', diff.length);
for (const d of diff) console.log('  DIFF', d.logical, d.why, d.catalog ? d.catalog.bytes + '->' + d.current.bytes : '');

// Which object's own logical source differs?
const selfChanged = c.objects.filter((o) => diffSet.has(o.logical)).map((o) => o.slug);
// Which objects depend on a differing header through their depfile?
const depChanged = [];
for (const o of c.objects) {
  if (selfChanged.includes(o.slug)) continue;
  const hits = (o.dep_sources || []).filter((s) => diffSet.has(s));
  if (hits.length) depChanged.push({ slug: o.slug, logical: o.logical, hits });
}
console.log('\nself-changed objects (' + selfChanged.length + '):', selfChanged.join(' '));
console.log('\ndepends-on-changed objects (' + depChanged.length + '):');
for (const d of depChanged) console.log('  ' + d.slug + '  <- ' + d.hits.join(', '));
const rebuild = new Set([...selfChanged, ...depChanged.map((d) => d.slug)]);
console.log('\nREBUILD SET (' + rebuild.size + ' of ' + c.objects.length + '):');
console.log([...rebuild].sort().join(' '));
console.log('REUSE SET (' + (c.objects.length - rebuild.size) + '):');
console.log(c.objects.map((o) => o.slug).filter((s) => !rebuild.has(s)).sort().join(' '));
fs.writeFileSync(R + '/stage-output/rebuild-scope-147.json', JSON.stringify({
  catalog: 'build-material-catalog-147.json', source_view_rows: c.source_view.length,
  differing: diff, rebuild: [...rebuild].sort(), reuse: c.objects.map((o) => o.slug).filter((s) => !rebuild.has(s)).sort(),
}, null, 2) + '\n');