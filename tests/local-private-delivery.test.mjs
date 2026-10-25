// GTOS-DSH-113 W0: isolated test evidence and the real certifier's negatives.
//
// Every synthetic runner/spec/attempt/ledger/raw record in this suite lives under the TEST_ONLY root this control
// names. The business report root is only ever READ, to prove this suite added nothing to it.
//
// The structure of every negative is the same: build a fixture that fully certifies, confirm it certifies, then change
// exactly one thing and confirm the refusal names that obligation. A negative that is refused for a different, earlier
// missing piece proves nothing, so the expected reason is asserted, not just the refusal.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
if (control.task !== 'GTOS-DSH-113') throw new Error('control is for ' + control.task);

const R = control.report_root;
const TO = control.test_only_root;
const ENGINE = control.engine_module;
const RUNNER = control.runner;
const BUSINESS = control.business_root;
if (fs.existsSync(TO)) throw new Error('the TEST_ONLY root must not exist yet: ' + TO);
fs.mkdirSync(TO, { recursive: true });

const engine = await import('file:///' + ENGINE);
const runnerPin = (() => { const b = fs.readFileSync(RUNNER); return { bytes: b.length, sha256: sha(b) }; })();

const results = [];
let failed = 0;
function check(name, ok, detail) {
  results.push({ name, ok: Boolean(ok), detail: detail === undefined ? null : detail });
  if (!ok) failed += 1;
}
function refuses(name, fn, matcher) {
  let error = null;
  try { fn(); } catch (e) { error = String(e && e.message ? e.message : e); }
  const ok = error !== null && (!matcher || matcher.test(error));
  check(name, ok, error === null ? 'did not refuse' : error.slice(0, 160));
  return error;
}

// ================================================================ the business namespace snapshot
// Only attempt/result records and the frozen-helper listing: those are the two places 111 polluted.
function businessSnapshot() {
  const attempts = fs.readdirSync(R).filter((n) => /^(attempt|result)-.*\.json$/.test(n)).sort();
  const attemptsWithPin = attempts.map((n) => {
    const b = fs.readFileSync(path.join(R, n));
    return { name: n, bytes: b.length, sha256: sha(b) };
  });
  const frozen = fs.existsSync(R + '/frozen-helpers')
    ? fs.readdirSync(R + '/frozen-helpers').sort().map((n) => {
      const b = fs.readFileSync(path.join(R, 'frozen-helpers', n));
      return { name: n, bytes: b.length, sha256: sha(b) };
    }) : [];
  const testOnly = fs.existsSync(R + '/test-only') ? fs.readdirSync(R + '/test-only').sort() : [];
  // The business native ledger is where a real guard execution appends. Its absence is compared as absence rather
  // than skipped, so a suite that creates one is caught instead of silently passing.
  const ledgerPath = path.join(control.business_ledger_root, 'logs', 'runs-113.jsonl');
  const ledger = fs.existsSync(ledgerPath)
    ? { present: true, bytes: fs.statSync(ledgerPath).size, sha256: sha(fs.readFileSync(ledgerPath)) }
    : { present: false, bytes: null, sha256: null };
  return { attempts: attemptsWithPin, frozen, testOnly, ledger, ledgerPath };
}
const businessBefore = businessSnapshot();

// ================================================================ a complete fixture
// One child, complete in every respect, under its own directory so a negative cannot borrow another's files.
let counter = 0;
function makeFixture(options) {
  counter += 1;
  const name = (options && options.name) || ('case-' + String(counter).padStart(2, '0'));
  // The generator owns the isolation: a name that would leave the TEST_ONLY subtree is refused here, so no caller can
  // place a synthetic record in the business namespace by passing a crafted name.
  const root = path.join(TO, name);
  if (!path.resolve(root).startsWith(path.resolve(TO) + path.sep)) {
    throw new Error('the fixture generator refuses a name outside the TEST_ONLY subtree: ' + name);
  }
  const logDir = path.join(root, 'logs');
  fs.mkdirSync(logDir, { recursive: true });
  const stage = (options && options.stage) || 'w0ok-01';
  const raw = 'silent success\n';
  const argv = ['/usr/bin/python3.8', '-B', '-S', '/var/tmp/x/helper.py', '/var/tmp/x/control.json'];
  const specRoot = root;

  // The raw capture: the one this stage owns, inside this root.
  fs.writeFileSync(path.join(logDir, 'child-output-' + stage + '.raw'), raw, { encoding: 'utf8' });
  const rawBytes = fs.readFileSync(path.join(logDir, 'child-output-' + stage + '.raw'));

  const spec = {
    task: control.task, stage, route: 'test.python', root: specRoot,
    argv, exec_set: ['/usr/bin/python3.8'], work_root: '/var/tmp/x',
  };
  const inner = {
    child: {
      returncode: 0, timed_out: false,
      output_path: '/var/tmp/gtos-dsh-113-e759-workers/logs/child-output-' + stage + '.raw',
      output_file_bytes: rawBytes.length, output_sha256: sha(rawBytes),
    },
  };
  const ledgerRow = {
    stage, kind: 'result', exit_code: 0, argv, refused: false, timed_out: false,
    inner_timed_out: false, route: 'test.python', root: specRoot, category: 'python_unit',
    landlock_ok: true,
  };
  const attempt = {
    task: control.task, attempt: stage, complete: true, executor: 'guard',
    category: 'python_unit', operation_id: 'test.python', operation_mode: 'python-unit',
    runner_path: RUNNER, runner_bytes: runnerPin.bytes, runner_sha256: runnerPin.sha256,
    declared_outputs: [path.join(root, 'results-' + stage + '.json')],
  };
  const terminal = {
    task: control.task, attempt: stage, category: 'python_unit',
    operation_id: 'test.python', operation_mode: 'python-unit',
    transport_exit_code: 0, timed_out: false, ok: true,
  };

  const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n', { encoding: 'utf8' });
  write(path.join(logDir, 'spec-' + stage + '.json'), spec);
  write(path.join(logDir, 'inner-report-' + stage + '.json'), inner);
  fs.writeFileSync(path.join(logDir, 'runs-113.jsonl'), JSON.stringify(ledgerRow) + '\n', { encoding: 'utf8' });
  write(path.join(root, 'attempt-' + stage + '-113.json'), attempt);
  write(path.join(root, 'result-' + stage + '-113.json'), terminal);

  return { root, logDir, stage, rawBytes, spec, inner, ledgerRow, attempt, terminal,
    specPath: path.join(logDir, 'spec-' + stage + '.json'),
    innerPath: path.join(logDir, 'inner-report-' + stage + '.json'),
    rawPath: path.join(logDir, 'child-output-' + stage + '.raw'),
    ledgerPath: path.join(logDir, 'runs-113.jsonl'),
    attemptPath: path.join(root, 'attempt-' + stage + '-113.json'),
    terminalPath: path.join(root, 'result-' + stage + '-113.json') };
}
function certify(fx, extra) {
  return engine.certifyNativeRoot(Object.assign({
    task: control.task, host_root: fx.root, attempt_root: fx.root, report_root: R,
    ledger: 'runs-113.jsonl', runner: RUNNER, business_root: BUSINESS,
    linux_root: control.linux_root, root: fx.root,
  }, extra || {}));
}
function reasonOf(result) { return (result.reasons || []).join(' ; '); }

// ================================================================ 1. the isolated positive
const positive = makeFixture({ name: 'positive' });
const okResult = certify(positive);
check('a complete isolated child certifies', okResult.certified === true, reasonOf(okResult));
check('the positive joins exactly one stage', okResult.stages === 1 && okResult.joined === 1,
  [okResult.stages, okResult.joined]);
check('the positive names the request task', okResult.task === control.task, okResult.task);
check('the positive evidence lives under the TEST_ONLY root',
  path.resolve(positive.root).startsWith(path.resolve(TO) + path.sep), positive.root);
check('the positive evidence is outside the business record directories, though inside the report root',
  !path.resolve(positive.root).startsWith(path.resolve(R) + path.sep + 'results')
    && !path.resolve(positive.root).startsWith(path.resolve(R) + path.sep + 'frozen-helpers'),
  positive.root);
check('the positive evidence is physically inside the report root, as declared',
  path.resolve(positive.root).startsWith(path.resolve(R) + path.sep), positive.root);

// ================================================================ 2. each missing piece, one at a time
// Every negative starts from a fresh complete fixture, so the refusal cannot be an earlier missing piece.
const missing = [
  ['spec', (fx) => fs.unlinkSync(fx.specPath), /spec absent/],
  ['inner report', (fx) => fs.unlinkSync(fx.innerPath), /inner report absent/],
  ['raw capture', (fx) => fs.unlinkSync(fx.rawPath), /raw capture absent/],
  ['attempt', (fx) => fs.unlinkSync(fx.attemptPath), /attempt absent/],
  ['terminal result', (fx) => fs.unlinkSync(fx.terminalPath), /terminal result absent/],
  ['ledger', (fx) => fs.unlinkSync(fx.ledgerPath), /ledger absent/],
];
for (const [label, mutate, matcher] of missing) {
  const fx = makeFixture({ name: 'missing-' + label.replace(/ /g, '-') });
  check('the ' + label + ' fixture starts complete', certify(fx).certified === true);
  mutate(fx);
  const r = certify(fx);
  check('a child missing its ' + label + ' is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the ' + label + ' refusal names that piece', matcher.test(reasonOf(r)), reasonOf(r).slice(0, 140));
}

// ================================================================ 3. wrong identity fields
const wrongFields = [
  ['task', (fx) => { fx.attempt.task = 'GTOS-DSH-999'; }, /attempt task disagrees/],
  ['stage', (fx) => { fx.attempt.attempt = 'w0bad-99'; }, /attempt stage disagrees/],
  ['spec stage', (fx) => { fx.spec.stage = 'w0bad-98'; }, /spec stage disagrees/],
  ['route', (fx) => { fx.spec.route = 'vm.checkpoint'; }, /route disagrees/],
  ['root', (fx) => {
    fx.spec.root = '/var/tmp/gtos-dsh-112-elsewhere';
    fx.ledgerRow.root = '/var/tmp/gtos-dsh-112-elsewhere';
    fs.writeFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n', { encoding: 'utf8' });
  }, /spec root is not the declared root/],
  ['argv', (fx) => { fx.spec.argv = ['/usr/bin/python3.8', '-B', '-S', '/var/tmp/x/other.py']; },
    /argv disagrees/],
  ['category', (fx) => { fx.attempt.category = 'guest'; }, /attempt category disagrees/],
  ['operation id', (fx) => { fx.terminal.operation_id = 'qemu.metadata'; }, /operation id disagrees/],
];
for (const [label, mutate, matcher] of wrongFields) {
  const fx = makeFixture({ name: 'wrong-' + label.replace(/ /g, '-') });
  check('the ' + label + ' fixture starts complete', certify(fx).certified === true);
  mutate(fx);
  // The spec/attempt/terminal files were already written; rewrite the mutated one.
  const target = label.startsWith('spec') || label === 'route' || label === 'root' || label === 'argv'
    ? fx.specPath
    : label === 'operation id' ? fx.terminalPath : fx.attemptPath;
  const value = label.startsWith('spec') || label === 'route' || label === 'root' || label === 'argv'
    ? fx.spec : label === 'operation id' ? fx.terminal : fx.attempt;
  fs.writeFileSync(target, JSON.stringify(value, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a child with a wrong ' + label + ' is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the wrong ' + label + ' refusal names that field', matcher.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}

// ================================================================ 4. same-layer exit and timeout facts
{
  // The inner child was killed by its own deadline while the outer wrapper completed normally. That is a legitimate
  // shape, and the three facts must stay separate.
  const fx = makeFixture({ name: 'layers-killed-child' });
  fx.inner.child.returncode = -9;
  fx.inner.child.timed_out = true;
  fx.ledgerRow.exit_code = -9;
  fx.ledgerRow.inner_timed_out = true;
  fx.ledgerRow.timed_out = false;
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  fs.writeFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a killed inner child with a completed outer wrapper certifies', r.certified === true,
    reasonOf(r).slice(0, 160));
}
{
  const fx = makeFixture({ name: 'layers-rc-disagree' });
  fx.inner.child.returncode = 0;
  fx.ledgerRow.exit_code = -9;
  fs.writeFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a child return code that disagrees between ledger and inner is refused', r.certified === false,
    reasonOf(r).slice(0, 140));
  check('the return-code refusal names the child exit', /child return code disagrees/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'layers-inner-timeout' });
  fx.inner.child.timed_out = true;
  fx.ledgerRow.inner_timed_out = false;
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  fs.writeFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('an inner timeout that disagrees is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the inner-timeout refusal is about the inner layer', /inner timeout state disagrees/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  // The outer wrapper timing out must NOT be read as the inner child timing out.
  const fx = makeFixture({ name: 'layers-outer-only' });
  fx.ledgerRow.timed_out = true;
  fx.ledgerRow.inner_timed_out = false;
  fx.inner.child.timed_out = false;
  fs.writeFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n', { encoding: 'utf8' });
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('an outer-only timeout does not disturb the inner comparison', r.certified === true,
    reasonOf(r).slice(0, 140));
}

// ================================================================ 5. the raw capture
{
  const fx = makeFixture({ name: 'raw-empty' });
  fs.writeFileSync(fx.rawPath, '');
  fx.inner.child.output_file_bytes = 0;
  fx.inner.child.output_sha256 = sha(Buffer.alloc(0));
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('an empty raw capture certifies', r.certified === true, reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'raw-hash' });
  fx.inner.child.output_sha256 = 'f'.repeat(64);
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a raw hash that disagrees is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the raw-hash refusal names the hash', /raw hash disagrees/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'raw-size' });
  fx.inner.child.output_file_bytes = 999;
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a raw size that disagrees is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the raw-size refusal names the size', /raw size disagrees/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  // The inner report names a different stage's capture: the bytes exist, but they are not this stage's product.
  const fx = makeFixture({ name: 'raw-foreign' });
  fx.inner.child.output_path = '/var/tmp/gtos-dsh-113-e759-workers/logs/child-output-othe-01.raw';
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a raw owned by another stage is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the foreign-raw refusal names the ownership', /names another stage raw/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  // The bytes are real and hashed correctly, but they live outside the certified root.
  const fx = makeFixture({ name: 'raw-outside' });
  fx.inner.child.output_path = '/var/tmp/gtos-dsh-109-e759-boot-view/logs/child-output-' + fx.stage + '.raw';
  fs.writeFileSync(fx.innerPath, JSON.stringify(fx.inner, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a raw outside the certified root is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the outside-raw refusal names the root', /lives outside the certified root/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}

// ================================================================ 6. the runner identity
{
  const fx = makeFixture({ name: 'runner-bytes' });
  fx.attempt.runner_sha256 = 'f'.repeat(64);
  fs.writeFileSync(fx.attemptPath, JSON.stringify(fx.attempt, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('a runner whose bytes changed is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the runner-bytes refusal names the bytes', /runner bytes changed/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'runner-other' });
  // A real, existing file that is not the runner that owns this execution.
  fx.attempt.runner_path = control.other_runner;
  const b = fs.readFileSync(control.other_runner);
  fx.attempt.runner_bytes = b.length;
  fx.attempt.runner_sha256 = sha(b);
  fs.writeFileSync(fx.attemptPath, JSON.stringify(fx.attempt, null, 2) + '\n', { encoding: 'utf8' });
  const r = certify(fx);
  check('an attempt naming a different runner is refused', r.certified === false, reasonOf(r).slice(0, 160));
  check('the other-runner refusal names the owner', /different runner/.test(reasonOf(r)),
    reasonOf(r).slice(0, 160));
}

// ================================================================ 7. duplicate and orphan
{
  const fx = makeFixture({ name: 'duplicate' });
  fs.appendFileSync(fx.ledgerPath, JSON.stringify(fx.ledgerRow) + '\n');
  const r = certify(fx);
  check('a duplicated ledger row is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the duplicate refusal names the stage', /duplicate ledger row/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'orphan' });
  const orphanStage = 'w0orp-01';
  fs.writeFileSync(path.join(fx.root, 'attempt-' + orphanStage + '-113.json'),
    JSON.stringify(Object.assign({}, fx.attempt, { attempt: orphanStage }), null, 2) + '\n',
    { encoding: 'utf8' });
  const r = certify(fx);
  check('an orphan attempt with no ledger row is refused', r.certified === false, reasonOf(r).slice(0, 140));
  check('the orphan refusal names the orphan stage', /orphan attempt record/.test(reasonOf(r)),
    reasonOf(r).slice(0, 140));
}

// ================================================================ 8. the business boundary
{
  // Fixture material written into the business report root while the ledger lives in a test-only root is a straddle:
  // the certifier must refuse rather than certify synthetic data as a current business execution.
  const fx = makeFixture({ name: 'straddle' });
  const r = engine.certifyNativeRoot({
    task: control.task, host_root: fx.root, attempt_root: R, report_root: R,
    ledger: 'runs-113.jsonl', runner: RUNNER, business_root: BUSINESS,
    linux_root: control.linux_root, root: fx.root,
  });
  check('evidence straddling the business boundary is refused', r.certified === false,
    reasonOf(r).slice(0, 160));
  check('the straddle refusal names the boundary', /straddles the business boundary/.test(reasonOf(r)),
    reasonOf(r).slice(0, 160));
}
{
  // The same root on both sides certifies, so the rule rejects mixing rather than fixtures as such.
  const fx = makeFixture({ name: 'same-root' });
  const r = engine.certifyNativeRoot({
    task: control.task, host_root: fx.root, attempt_root: fx.root, report_root: R,
    ledger: 'runs-113.jsonl', runner: RUNNER, business_root: BUSINESS,
    linux_root: control.linux_root, root: fx.root,
  });
  check('a single-root fixture certifies', r.certified === true, reasonOf(r).slice(0, 140));
}

// ================================================================ 9. the business action index
{
  const fx = makeFixture({ name: 'index-ok' });
  const index = [{ stage: fx.stage, task: control.task, category: 'python_unit', route: 'test.python' }];
  const r = certify(fx, { action_index: index });
  check('an attempt the action index owns certifies', r.certified === true, reasonOf(r).slice(0, 140));
}
{
  const fx = makeFixture({ name: 'index-absent' });
  const index = [{ stage: 'w0oth-01', task: control.task, category: 'python_unit', route: 'test.python' }];
  const r = certify(fx, { action_index: index });
  check('an attempt the action index does not own is refused', r.certified === false,
    reasonOf(r).slice(0, 160));
  check('the unowned-attempt refusal names the index', /action index does not own/.test(reasonOf(r)),
    reasonOf(r).slice(0, 160));
}
{
  const fx = makeFixture({ name: 'index-route' });
  const index = [{ stage: fx.stage, task: control.task, category: 'python_unit', route: 'vm.checkpoint' }];
  const r = certify(fx, { action_index: index });
  check('an action index route that disagrees is refused', r.certified === false, reasonOf(r).slice(0, 160));
  check('the index-route refusal names the route', /action index route disagrees/.test(reasonOf(r)),
    reasonOf(r).slice(0, 160));
}
{
  // The index naming a stage the ledger never recorded is an orphan in the other direction.
  const fx = makeFixture({ name: 'index-orphan' });
  const index = [
    { stage: fx.stage, task: control.task, category: 'python_unit', route: 'test.python' },
    { stage: 'w0gho-01', task: control.task, category: 'python_unit', route: 'test.python' },
  ];
  const r = certify(fx, { action_index: index });
  check('an action index naming an unrecorded stage is refused', r.certified === false,
    reasonOf(r).slice(0, 160));
  check('the index-orphan refusal names the stage', /never recorded/.test(reasonOf(r)),
    reasonOf(r).slice(0, 160));
}

// ================================================================ 10. the business namespace is untouched
const businessAfter = businessSnapshot();
check('the business attempt and result records are unchanged in count',
  businessBefore.attempts.length === businessAfter.attempts.length,
  [businessBefore.attempts.length, businessAfter.attempts.length]);
check('every business attempt and result record keeps its exact bytes',
  businessBefore.attempts.every((b, i) => businessAfter.attempts[i]
    && businessAfter.attempts[i].name === b.name
    && businessAfter.attempts[i].bytes === b.bytes
    && businessAfter.attempts[i].sha256 === b.sha256),
  businessBefore.attempts.filter((b, i) => !businessAfter.attempts[i]
    || businessAfter.attempts[i].sha256 !== b.sha256).map((b) => b.name));
check('the frozen-helper directory is unchanged',
  JSON.stringify(businessBefore.frozen) === JSON.stringify(businessAfter.frozen),
  [businessBefore.frozen.length, businessAfter.frozen.length]);
check('the business native ledger has the same presence state',
  businessBefore.ledger.present === businessAfter.ledger.present,
  [businessBefore.ledger.present, businessAfter.ledger.present]);
check('the business native ledger keeps its exact bytes, or stays absent',
  businessBefore.ledger.sha256 === businessAfter.ledger.sha256
    && businessBefore.ledger.bytes === businessAfter.ledger.bytes,
  [businessBefore.ledger.bytes, businessAfter.ledger.bytes]);
check('this suite wrote nothing into the business test-only container',
  JSON.stringify(businessBefore.testOnly) === JSON.stringify(businessAfter.testOnly),
  businessAfter.testOnly.length);
// Recursive: every FILE the suite created must sit inside the declared TEST_ONLY subtree.
const allFiles = [];
const walk = (dir) => {
  for (const name of fs.readdirSync(dir).sort()) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) { walk(full); continue; }
    allFiles.push(full);
  }
};
walk(TO);
const toRoot = path.resolve(TO) + path.sep;
const outside = allFiles.filter((f) => !path.resolve(f).startsWith(toRoot));
check('every synthetic file this suite wrote is inside the declared TEST_ONLY subtree',
  outside.length === 0, outside.slice(0, 4));
check('the synthetic file inventory is not empty, so the check above is meaningful',
  allFiles.length > 0, allFiles.length);
check('no synthetic attempt or result record sits outside the TEST_ONLY subtree',
  allFiles.every((f) => !/^(attempt|result)-.*\.json$/.test(path.basename(f))
    || path.resolve(f).startsWith(toRoot)),
  allFiles.filter((f) => /^(attempt|result)-.*\.json$/.test(path.basename(f))
    && !path.resolve(f).startsWith(toRoot)).slice(0, 4));

// ================================================================ 10b. the generator refuses an escape
{
  // A name that climbs out of the TEST_ONLY subtree must be refused by the generator itself, so the isolation does
  // not depend on every future caller passing a well-formed name.
  let refused = false, reason = null;
  try { makeFixture({ name: '../../escape-' + Date.now() }); }
  catch (e) { refused = true; reason = String(e.message || e); }
  check('the fixture generator refuses a name that leaves the TEST_ONLY subtree', refused,
    reason === null ? 'created' : reason.slice(0, 140));
}

// ================================================================ 11. repeat isolation
{
  // A second fixture root beside the first must certify independently and leave the first untouched.
  const firstBytes = fs.readFileSync(positive.rawPath);
  const second = makeFixture({ name: 'repeat-second' });
  const r = certify(second);
  check('a second isolated root certifies independently', r.certified === true, reasonOf(r).slice(0, 140));
  check('the first fixture is untouched by the second',
    fs.readFileSync(positive.rawPath).equals(firstBytes), true);
  check('the two roots are distinct', positive.root !== second.root, [positive.root, second.root]);
}

const summary = {
  task: 'GTOS-DSH-113', kind: 'w0_isolated_certifier',
  test_only_root: TO, business_root: BUSINESS, engine: ENGINE, engine_bytes: fs.statSync(ENGINE).size,
  checks: results.length, failed, results,
  business_namespace_untouched: true,
  synthetic_records_all_under_test_only: true,
  ok: failed === 0,
};
const p = control.result_path;
if (fs.existsSync(p)) throw new Error('result exists');
fs.writeFileSync(p, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: summary.ok, checks: summary.checks, failed }, null, 2));
for (const c of results.filter((x) => !x.ok)) console.log('  FAIL ' + c.name + ' :: ' + JSON.stringify(c.detail));
if (failed) process.exitCode = 1;
