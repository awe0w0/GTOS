import fs from 'node:fs';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';

const c = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const pin = p => {
  const b = fs.readFileSync(p);
  return {bytes: b.length, sha256: crypto.createHash('sha256').update(b).digest('hex')};
};
const before = pin(c.module.path);
if (before.bytes !== c.module.bytes || before.sha256 !== c.module.sha256) throw Error('module pin');
const api = await import(pathToFileURL(c.module.path).href);
const checks = [];
const need = (ok, name) => {
  checks.push({ok: ok === true, name});
  if (!ok) throw Error(name);
};
const raw = {requested_path: '/TEST_ONLY/log.raw', bytes: 0, sha256: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'};
const metadata = {
  kind: 'native_metadata', expected: {argv: ['/usr/bin/python3.8', '-B', '-S', '/TEST_ONLY/meta.py']},
  spec: {cmd: ['/usr/bin/python3.8', '-B', '-S', '/TEST_ONLY/meta.py']},
  inner: {
    argv: ['/usr/bin/python3.8', '-B', '-S', '/TEST_ONLY/meta.py'], landlock: {ok: true},
    probe_checks: Object.fromEntries(api.PROBE_KEYS.map(key => [key, true])),
    child: {returncode: 0, timed_out: false, group_measured: true, group_measured_final: true,
      descendants_left: [], killed: [], output_path: raw.requested_path, output_bytes: 0,
      output_file_bytes: 0, output_sha256: raw.sha256},
  }, raw, products: {},
};
const product = kind => {
  const e = structuredClone(metadata);
  e.kind = kind;
  e.expected = {argv: ['/TEST_ONLY/tool', '-o', '/TEST_ONLY/result.o'], output: '/TEST_ONLY/result.o'};
  e.spec.cmd = e.expected.argv.slice(); e.inner.argv = e.expected.argv.slice();
  e.products.object = {requested_path: e.expected.output, bytes: 52, sha256: '1'.repeat(64)};
  if (kind === 'object_compile') {
    e.expected.depfile = '/TEST_ONLY/result.d';
    e.products.depfile = {requested_path: e.expected.depfile, bytes: 20, sha256: '2'.repeat(64)};
  }
  return e;
};
const compile = product('object_compile'), link = product('relocatable_link');
const demangle = structuredClone(metadata); demangle.kind = 'symbol_demangle';
need(api.inspectNativeEvidence(metadata).ok, 'metadata has no object obligation');
need(api.inspectNativeEvidence(demangle).ok, 'demangler has no object obligation');
need(api.inspectNativeEvidence(compile).ok, 'compile requires both object and depfile');
need(api.inspectNativeEvidence(link).ok, 'relocatable link requires object only');
need(Object.isFrozen(api.NATIVE_KINDS) && Object.isFrozen(api.PROBE_KEYS), 'protocol vocabularies are frozen');
const reject = (seed, change, name) => {
  const e = structuredClone(seed); change(e);
  const r = api.inspectNativeEvidence(e);
  need(r.ok === false && r.failed.length > 0, name);
};
for (const [name, change] of [
  ['unknown category', e => {e.kind = 'owner_assembly';}],
  ['missing child', e => {delete e.inner.child;}],
  ['nonzero exit', e => {e.inner.child.returncode = 1;}],
  ['unobserved exit', e => {e.inner.child.returncode = null;}],
  ['timeout', e => {e.inner.child.timed_out = true;}],
  ['landlock failed', e => {e.inner.landlock.ok = false;}],
  ['landlock unknown', e => {e.inner.landlock.ok = null;}],
  ['empty probes', e => {e.inner.probe_checks = {};}],
  ['one missing probe', e => {delete e.inner.probe_checks.negative_cat_denied;}],
  ['one false probe', e => {e.inner.probe_checks.positive_python_allowed = false;}],
  ['additional false probe', e => {e.inner.probe_checks.extra = false;}],
  ['group not measured', e => {e.inner.child.group_measured = false;}],
  ['final group unknown', e => {delete e.inner.child.group_measured_final;}],
  ['live descendant', e => {e.inner.child.descendants_left = [42];}],
  ['cleanup was needed', e => {e.inner.child.killed = [42];}],
  ['missing raw', e => {e.raw = null;}],
  ['raw digest drift', e => {e.raw.sha256 = '0'.repeat(64);}],
  ['raw length drift', e => {e.raw.bytes = 1;}],
  ['raw capture length disagreement', e => {e.inner.child.output_file_bytes = 1;}],
  ['raw path from another stage', e => {e.raw.requested_path = '/TEST_ONLY/other.raw';}],
  ['spec argv drift', e => {e.spec.cmd[0] = '/TEST_ONLY/other';}],
  ['inner argv drift', e => {e.inner.argv[0] = '/TEST_ONLY/other';}],
  ['empty expected argv', e => {e.expected.argv = [];}],
  ['metadata cannot claim stale object', e => {e.products.object = compile.products.object;}],
]) reject(metadata, change, name);
reject(compile, e => {delete e.products.object;}, 'compile missing object refuses');
reject(compile, e => {delete e.expected.output;}, 'compile cannot erase its object obligation');
reject(compile, e => {e.products.object.bytes = 0;}, 'empty object refuses');
reject(compile, e => {e.products.object.requested_path = '/TEST_ONLY/other.o';}, 'another object cannot satisfy the obligation');
reject(compile, e => {delete e.products.depfile;}, 'compile missing depfile refuses');
reject(compile, e => {e.expected.depfile = '/TEST_ONLY/other.d';}, 'mismatched depfile refuses');
reject(compile, e => {e.products.depfile.sha256 = 'not a hash';}, 'invalid depfile pin refuses');
reject(link, e => {delete e.products.object;}, 'link missing object refuses');
reject(link, e => {e.products.depfile = compile.products.depfile;}, 'link cannot claim another depfile');
const untouched = JSON.stringify(compile);
const first = JSON.stringify(api.inspectNativeEvidence(compile));
for (let i = 0; i < 512; i++) {
  if (JSON.stringify(api.inspectNativeEvidence(compile)) !== first
      || !api.inspectNativeEvidence(metadata).ok || api.inspectNativeEvidence(null).ok) throw Error('repeated evaluation drift');
}
need(JSON.stringify(compile) === untouched, '512 interleaved evaluations are repeatable and leave input untouched');
need(api.inspectNativeEvidence(metadata).target_execution_proved === false, 'native tool evidence never proves target execution');
const after = pin(c.module.path);
need(after.sha256 === before.sha256, 'actual tested module unchanged');
const result = {task: c.task, ok: true, passed: true, assertions: checks.length, failed: 0,
  module: c.module, normal_categories: 4, stability_cycles: 512,
  test_only_evidence_data: true, native_commands_started: 0, checks};
fs.writeFileSync(c.result_path, JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({passed: true, assertions: checks.length, stability_cycles: 512}));
