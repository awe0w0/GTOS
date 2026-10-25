import fs from 'node:fs';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const control = process.argv[2] ? JSON.parse(fs.readFileSync(process.argv[2], 'utf8')) : {};
const subject = control.module || { path: fileURLToPath(new URL('../tools/native-artifacts.mjs', import.meta.url)) };
const hash = path => crypto.createHash('sha256').update(fs.readFileSync(path)).digest('hex');
const before = hash(subject.path);
if (subject.sha256 && before !== subject.sha256) throw new Error('subject pin');
const { artifactLayout } = await import(pathToFileURL(subject.path).href);
const checks = [];
function check(ok, name) { checks.push({ ok, name }); if (!ok) throw new Error(name); }
function rejects(fn, name) { let refused = false; try { fn(); } catch { refused = true; } check(refused, name); }
const work = '/var/tmp/gtos-dsh-102-e759-storage-objects';
const ids = [{ slug: 'arp', has_depfile: true }, { slug: 'interruptstubs.asm', has_depfile: false }];
const original = JSON.stringify(ids);
const actual = artifactLayout(work, ids);
check(actual.entries[0].staged_linux === work + '/inherited/arp.o', 'canonical Linux object path');
check(actual.entries[0].depfile_linux === work + '/inherited/arp.d', 'dependency paired by identity');
check(actual.entries[1].depfile_linux === null, 'assembler dependency remains absent');
check(JSON.stringify(actual.required_paths) === JSON.stringify(['objects/reused/arp.o', 'objects/reused/arp.d', 'objects/reused/interruptstubs.asm.o']), 'shape paths pair and retain order');
check(JSON.stringify(actual.object_inputs) === JSON.stringify([work + '/inherited/arp.o', work + '/inherited/interruptstubs.asm.o']), 'link consumes the same ordered identities');
check(JSON.stringify(ids) === original, 'input identities unchanged');
for (const source of ['F:\\GTOS-Chromium\\objects\\arp.o', '//wsl$/Ubuntu-20.04/var/tmp/arp.o', '\\\\wsl$\\Ubuntu-20.04\\var\\tmp\\arp.o', '/var/tmp/arp.o', 'C:/path with spaces/中文/arp.o']) {
  check(JSON.stringify(artifactLayout(work, [{ ...ids[0], source }])) === JSON.stringify(artifactLayout(work, [ids[0]])), 'opaque source spelling cannot change destination: ' + source);
}
for (const slug of ['', '..', '../arp', '/arp', 'C:arp', 'arp\\x', 'arp/x', 'ARP', '8arp', 'arp.o', 'arp..asm', 'arp.asm.asm', 'arp:stream', 'arp\t', 'arp\n', null, 1]) {
  rejects(() => artifactLayout(work, [{ slug, has_depfile: true }]), 'invalid slug refused: ' + JSON.stringify(slug));
}
for (const root of ['', '/tmp/work', '/var/tmp/../work', '/var/tmp/work/child', 'F:/var/tmp/work', '//wsl$/Ubuntu-20.04/work', '/var/tmp/work\\x', null]) {
  rejects(() => artifactLayout(root, ids), 'noncanonical work root refused: ' + root);
}
for (const value of [undefined, null, 0, 1, 'false']) rejects(() => artifactLayout(work, [{ slug: 'arp', has_depfile: value }]), 'explicit dependency presence: ' + value);
rejects(() => artifactLayout(work, []), 'empty identity set refused');
rejects(() => artifactLayout(work, null), 'missing identity set refused');
rejects(() => artifactLayout(work, [null]), 'null row refused');
rejects(() => artifactLayout(work, [ids[0], { ...ids[0] }]), 'duplicate identity refused');
let stable = true;
for (let cycle = 0; cycle < 512; cycle++) stable = stable && JSON.stringify(artifactLayout(work, ids)) === JSON.stringify(actual);
check(stable && JSON.stringify(ids) === original, '512 repeat cycles preserve result and input');
check(hash(subject.path) === before, 'tested module unchanged');
const result = { task: control.task || 'native-artifacts', ok: true, assertions: checks.length, failed: 0, stability_cycles: 512, module: { path: subject.path, bytes: fs.statSync(subject.path).size, sha256: before }, native_commands_started: 0, checks };
if (control.result_path) fs.writeFileSync(control.result_path, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify(result));
