// Rewrite every CLI entry guard from a name suffix to a self-identity comparison.
//
// `path.resolve(process.argv[1]).endsWith('harness-147.mjs')` is true for *any* copy of the file with that name, so a module
// that the package harness imports from an extracted payload would run its own CLI inside the importing process, with the
// importer's arguments. Comparing the resolved entry point with this module's own path is the property that was meant.
import fs from 'node:fs';

const dir = process.argv[2];
const targets = [
  ['c-chain-147.mjs', "const isMain = process.argv[1] && path.resolve(process.argv[1]).endsWith('c-chain-147.mjs');"],
  ['h-package-147.mjs', "const isMain = process.argv[1] && path.resolve(process.argv[1]).endsWith('h-package-147.mjs');"],
  ['harness-147.mjs', "const isMain = process.argv[1] && path.resolve(process.argv[1]).endsWith('harness-147.mjs');"],
  ['n-numeric-147.mjs', "const isMain = process.argv[1] && path.resolve(process.argv[1]).endsWith('n-numeric-147.mjs');"],
  ['contract-vs-source-147.mjs',
    "if (process.argv[1] && path.resolve(process.argv[1]).endsWith('contract-vs-source-147.mjs')) {"],
  ['host-consume-147.mjs', "if (process.argv[1] && process.argv[1].endsWith('host-consume-147.mjs')) {"],
];

const selfLine = 'const SELF_PATH = fileURLToPath(import.meta.url);';
const guardOf = (name) => `const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH);`;

for (const [name, old] of targets) {
  const p = dir + '/' + name;
  let text = fs.readFileSync(p, 'utf8');
  if (!text.includes(old)) { console.log('SKIP (pattern absent): ' + name); continue; }
  // The replacement keeps the shape of the original statement: an `if` guard stays an `if`, a declared `const` stays a `const`.
  const replacement = old.startsWith('if (')
    ? `if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH)) {`
    : guardOf(name);
  text = text.replace(old, replacement);
  // The self path needs both imports. `fileURLToPath` may already be there under a different name; it is added only if absent.
  if (!/from 'node:url'/.test(text)) {
    text = text.replace(/^(import .*\n)/m, `$1import { fileURLToPath } from 'node:url';\n`);
  } else if (!/fileURLToPath/.test(text.split('\n').slice(0, 30).join('\n'))) {
    text = text.replace(/import \{ ([^}]*) \} from 'node:url';/, (m, names) =>
      `import { ${[...new Set([...names.split(',').map((s) => s.trim()).filter(Boolean), 'fileURLToPath'])].join(', ')} } from 'node:url';`);
  }
  if (!text.includes(selfLine)) {
    // The constant goes immediately before the guard so a reader sees the two lines together.
    text = text.replace(replacement, `${selfLine}\n${replacement}`);
  }
  fs.writeFileSync(p, text);
  console.log('rewritten: ' + name);
}
