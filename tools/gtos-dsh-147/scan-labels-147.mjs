import fs from 'node:fs';
const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const src = fs.readFileSync(R + '/worktree-147/src/kernel.cpp', 'utf8').split(/\r?\n/);
const blocks = [
  ['StopMemory', 1031, 1057], ['StopTables', 1057, 1062], ['StopAp', 1063, 1126],
  ['StopPaging', 1130, 1204], ['StopWorkers', 1210, 1415], ['StopRuntimeInit', 1427, 1557],
  ['StopRuntimeObjects', 1569, 1733], ['StopRuntimeRecovery', 1747, 1998],
  ['StopRuntimeFrameRecovery', 2019, 2568], ['StopReject', 1009, 1030],
];
const out = {};
for (const [name, a, b] of blocks) {
  const labels = new Set(), prints = new Set();
  for (let i = a - 1; i < b; i += 1) {
    const line = src[i] || '';
    for (const m of line.matchAll(/LogValue\("([^"]+)"/g)) labels.add(m[1].trim());
    for (const m of line.matchAll(/"((?:[A-Z][A-Z0-9]*)(?: [A-Z0-9]+)+)\\n"/g)) prints.add(m[1].trim());
  }
  out[name] = { logvalue: [...labels].sort(), printf: [...prints].sort() };
  console.log('===', name, 'LogValue', labels.size, 'printf', prints.size);
  console.log('  L:', [...labels].sort().join(' | '));
  console.log('  P:', [...prints].sort().join(' | '));
}
fs.writeFileSync(R + '/stage-output/kernel-labels-scan.json', JSON.stringify(out, null, 2) + '\n');
