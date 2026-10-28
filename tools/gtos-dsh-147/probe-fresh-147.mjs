// Probe: import the packaged consumers one at a time from the extracted tree and report what happens, so a module that
// dispatches on `process.argv` at load is named rather than guessed at.
import { pathToFileURL } from 'node:url';
import fs from 'node:fs';

const root = process.argv[2];
const mods = ['engine/host-consume-147.mjs', 'engine/references/resource-semantics-147.mjs',
  'engine/h-package-147.mjs', 'engine/c-chain-147.mjs', 'engine/n-numeric-147.mjs'];
for (const m of mods) {
  const abs = root + '/' + m;
  process.stderr.write('importing ' + m + ' exists=' + fs.existsSync(abs) + '\n');
  try {
    const ns = await import(pathToFileURL(abs).href);
    process.stderr.write('  ok exports=' + Object.keys(ns).sort().join(',') + '\n');
  } catch (err) {
    process.stderr.write('  THREW ' + (err && err.message) + '\n');
  }
}
process.stderr.write('done\n');
