// GTOS-DSH-147: why the relocated chain's sidecars are not found. Prints the real and relocated paths and the two
// bridge-module copies' hashes, so the failure is a number rather than a guess.
import fs from 'node:fs';
import crypto from 'node:crypto';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const S = '/var/tmp/gtos-dsh-147-e759-scene-bound-view';
const toHost = (p) => (/^[A-Za-z]:[\\/]/.test(p) || /^[\\/]{2}/.test(p)
  ? p.replace(/\//g, '\\')
  : /^\/mnt\/([a-z])\/(.*)$/.test(p) ? p.replace(/^\/mnt\/([a-z])\//, (m, d, rest) => d.toUpperCase() + ':/' + rest)
    : '\\\\wsl$\\Ubuntu-20.04' + p.replace(/\//g, '\\'));
const stage = 'pbl147-12';
const bridge = JSON.parse(fs.readFileSync(toHost(R + '/results/' + stage + '.json'), 'utf8'));
console.log('bridge.inner_report.path', bridge.inner_report.path);
console.log('bridge.raw.path        ', bridge.raw.path);
console.log('exists inner (real)    ', fs.existsSync(toHost(bridge.inner_report.path)));
console.log('exists raw (real)      ', fs.existsSync(toHost(bridge.raw.path)));
const ctl = JSON.parse(fs.readFileSync(toHost(R + '/stage-output/' + stage + '.control.json'), 'utf8'));
const bm = ctl.bridge.bridge_module_path;
console.log('control bridge_module  ', bm);
console.log('  bytes', fs.statSync(toHost(bm)).size, sha(fs.readFileSync(toHost(bm))));
const contract = JSON.parse(fs.readFileSync(toHost(R + '/contract-147.json'), 'utf8'));
const entry = contract.executor_closure.find((e) => e.path.endsWith('private-boot-checkpoints-147.mjs'));
console.log('contract bridge        ', entry.path);
console.log('  bytes', fs.statSync(toHost(entry.path)).size, sha(fs.readFileSync(toHost(entry.path))));
console.log('same bytes             ', sha(fs.readFileSync(toHost(bm))) === entry.sha256);
// The scratch dirs the negatives left behind, to see where the sidecars actually landed.
const scratch = S + '/tmp/cneg-cnd-' + stage + '/case-00-pristine';
for (const sub of ['report', 'work', 'source']) {
  const d = scratch + '/' + sub;
  const walk = (p, depth) => {
    let names = [];
    try { names = fs.readdirSync(toHost(p)); } catch { return; }
    for (const n of names) {
      const q = p + '/' + n;
      const st = fs.statSync(toHost(q));
      console.log('   ' + '  '.repeat(depth) + q.slice(scratch.length + 1) + (st.isDirectory() ? '/' : ' ' + st.size));
      if (st.isDirectory() && depth < 3) walk(q, depth + 1);
    }
  };
  console.log('--- ' + sub);
  walk(d, 0);
}
