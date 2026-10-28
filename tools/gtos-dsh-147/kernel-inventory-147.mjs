import fs from 'node:fs';
const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const src = fs.readFileSync(R + '/worktree-147/src/kernel.cpp', 'utf8').split(/\r?\n/);
// The stop blocks, located by their `if (bootDiagnostic.stop == ...)` guards rather than by line numbers.
const stops = [];
src.forEach((l, i) => {
  const m = l.match(/if \(bootDiagnostic\.stop == common::BootDiagnostic::(Stop\w+)\)/);
  if (m) stops.push({ stop: m[1], start: i });
});
stops.forEach((s, k) => { s.end = k + 1 < stops.length ? stops[k + 1].start : src.length; });
const out = {};
for (const s of stops) {
  const labels = [];
  const markers = [];
  for (let i = s.start; i < s.end; i += 1) {
    const line = src[i];
    for (const m of line.matchAll(/LogValue\("([^"]+)"/g)) labels.push(m[1].replace(/\s+$/, ''));
    for (const m of line.matchAll(/"((?:[A-Z][A-Z0-9]*)(?: [A-Z0-9]+)+)\\n"/g)) markers.push(m[1]);
  }
  const count = (arr) => {
    const c = new Map();
    for (const x of arr) c.set(x, (c.get(x) || 0) + 1);
    return c;
  };
  out[s.stop] = {
    lines: s.end - s.start,
    label_counts: Object.fromEntries([...count(labels)].sort()),
    marker_counts: Object.fromEntries([...count(markers)].sort()),
  };
}
fs.writeFileSync(R + '/stage-output/kernel-inventory-147.json', JSON.stringify(out, null, 2) + '\n');
for (const [stop, v] of Object.entries(out)) {
  console.log('===', stop, v.lines, 'lines');
  const dup = Object.entries(v.label_counts).filter(([, n]) => n > 1);
  console.log('  labels:', Object.keys(v.label_counts).length, dup.length ? '  repeated: ' + dup.map(([k, n]) => k + 'x' + n).join(', ') : '  none repeated');
}
