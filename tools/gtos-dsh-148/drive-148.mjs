// GTOS-DSH-148 F: the driver. Phases, stages and the outer CLI, all over the same frozen engine bytes.
//
// The engine is `publish-148.mjs`. This file never reimplements any part of publication: every step it takes is a child
// process running that file, and the engine's hash is measured before and after each run. That is the property the
// taskbook asks for -- "生产driver、CLI、fixture必须调用同一实际引擎/同一被冻结bytes，不能影子函数测过而业务走另一份" --
// and it is enforced here rather than promised: a phase whose engine bytes changed between the first and the last step
// fails, and a phase that reads a field the engine did not write fails.
//
// THE STRICT READER. The 147 review's second finding was that a driver read a field that does not exist and still reported
// success, because the check it performed was an OR of old fields that happened to be true. A reader that accepts an
// absent field cannot gate anything. `readEngine` therefore takes a schema per verb: every named field must be present
// and of the declared type, and a missing field is a refusal that names it. There is no default, no `||`, and no
// fallback to an older field name anywhere in this file.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SELF = fileURLToPath(import.meta.url);
const HERE = path.dirname(SELF);
const ENGINE = HERE + '/publish-148.mjs';
const NODE = process.execPath;
const R = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const SCHEMA = '148.1';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };

// ---------------------------------------------------------------- the engine as a child
export const engineIdentity = () => {
  const b = fs.readFileSync(ENGINE);
  return { path: ENGINE, bytes: b.length, sha256: sha(b) };
};

export const runEngine = (verb, args, env = {}) => {
  const r = spawnSync(NODE, [ENGINE, verb, ...args], {
    encoding: 'utf8',
    env: { ...process.env, ...env },
    windowsHide: true,
    maxBuffer: 64 * 1024 * 1024,
  });
  return { verb, argv: [verb, ...args], status: r.status, stdout: r.stdout || '', stderr: r.stderr || '',
    signal: r.signal, ok: r.status === 0 };
};

// ---------------------------------------------------------------- the schemas
//
// One entry per verb: the fields the driver reads, with the type it requires. `'array'`, `'object'`, `'string'`,
// `'number'`, `'boolean'` and a literal are the only shapes. A field not listed is not read; a field listed and absent is
// a refusal.
const SCHEMAS = {
  state: {
    task: 'string', scope: 'string', present: 'boolean', archives: 'number', seals: 'number',
    terminal: 'boolean', ok: 'boolean',
  },
  plan: {
    verb: 'plan', plan: 'object', member_count: 'number', roles: 'object', total_bytes: 'number',
    ordinal_digest: 'string', evidence_pinned: 'number', required_set_pinned: 'boolean',
    mandatory_roles_pinned: 'number', mandatory_roles_required: 'number', schema_version: 'string',
  },
  archive: {
    verb: 'archive', payload: 'object', members: 'number', ordinal_digest: 'string', state: 'string',
    context_id: 'string', archives_after: 'number', schema_version: 'string',
  },
  seal: {
    verb: 'seal', receipt: 'object', payload: 'object', member_count: 'number', ordinal_digest: 'string',
    evidence_consumed: 'number', mandatory_roles_consumed: 'number', state: 'string', terminal: 'boolean',
    schema_version: 'string',
  },
  verify: {
    verb: 'verify', ok: 'boolean', schema_version: 'string', obligation_count: 'number',
    obligation_names: 'array', failed_obligations: 'array', schema_problems: 'object', member_count: 'number',
    ordinal_digest: 'string', out: 'string',
  },
};

const typeOf = (v) => (Array.isArray(v) ? 'array' : v === null ? 'null' : typeof v);

export class SchemaRefused extends Error {
  constructor(verb, problems) {
    super('the engine output for ' + verb + ' does not match the schema this driver reads: '
      + problems.map((p) => p.field + ' (' + p.why + ')').join(', '));
    this.name = 'SchemaRefused';
    this.verb = verb;
    this.problems = problems;
  }
}

// The reader. `JSON.parse` of the child's stdout, then the schema, then the field types. An output that is not JSON, or
// that is JSON without the declared fields, is refused with the field names -- never treated as a pass.
export const readEngine = (result, verb) => {
  const schema = SCHEMAS[verb];
  if (!schema) throw new Error('no schema is declared for the verb ' + verb);
  let value;
  try { value = JSON.parse(result.stdout); }
  catch (e) { throw new SchemaRefused(verb, [{ field: '<stdout>', why: 'not JSON: ' + String(e.message).slice(0, 120) }]); }
  const problems = [];
  for (const [field, want] of Object.entries(schema)) {
    if (!Object.prototype.hasOwnProperty.call(value, field)) {
      problems.push({ field, why: 'absent from the engine output' });
      continue;
    }
    const got = typeOf(value[field]);
    if (typeof want === 'string' && (want === 'array' || want === 'object' || want === 'string' || want === 'number' || want === 'boolean')) {
      if (got !== want) problems.push({ field, why: 'declared ' + want + ', got ' + got });
    } else if (want === 'plan' || want === 'archive' || want === 'seal' || want === 'verify' || want === 'state') {
      if (value[field] !== want) problems.push({ field, why: 'declared the literal ' + want + ', got ' + JSON.stringify(value[field]) });
    }
  }
  // The verify record's own schema version is part of the schema, not a comment: a record from another generation is a
  // record whose obligations this driver does not know, and reading it would be reading fields it never declared.
  if (verb === 'verify' || verb === 'plan' || verb === 'archive' || verb === 'seal') {
    if (value.schema_version !== SCHEMA) {
      problems.push({ field: 'schema_version', why: 'declared ' + SCHEMA + ', got ' + JSON.stringify(value.schema_version) });
    }
  }
  if (problems.length) throw new SchemaRefused(verb, problems);
  return value;
};

// A verify record read from disk -- by the phase, the stage and the outer CLI -- goes through the same schema. The three
// readers are the same function, which is what "propagate the same current schema in all layers" means concretely.
export const readVerifyRecord = (p) => {
  if (!exists(p)) throw new SchemaRefused('verify', [{ field: '<file>', why: 'absent: ' + p }]);
  return readEngine({ stdout: fs.readFileSync(p, 'utf8') }, 'verify');
};

// ---------------------------------------------------------------- the transaction
//
// One phase is one transaction: state, plan, archive, seal, verify. Every step is the real engine. The phase records what
// each step returned and refuses to call itself accepted unless the verify record says so in its own named fields.
export const phase = ({ tag, contextPath, specPath, planPath, payloadPath, receiptPath, outPath, env = {} }) => {
  const steps = [];
  const engineBefore = engineIdentity();
  const say = (name, r) => {
    steps.push({ step: name, argv: r.argv, status: r.status, signal: r.signal,
      stdout_bytes: r.stdout.length, stderr_bytes: r.stderr.length,
      stdout: r.stdout.length > 4000 ? r.stdout.slice(0, 4000) + '\n[truncated]' : r.stdout,
      stderr: r.stderr.length > 4000 ? r.stderr.slice(0, 4000) + '\n[truncated]' : r.stderr });
    return r;
  };

  const stateRun = say('state', runEngine('state', [contextPath], env));
  const state = stateRun.ok ? readEngine(stateRun, 'state') : null;

  const planRun = say('plan', runEngine('plan', [contextPath, specPath, planPath], env));
  const plan = planRun.ok ? readEngine(planRun, 'plan') : null;

  const archiveRun = say('archive', runEngine('archive', [contextPath, planPath, payloadPath], env));
  const archive = archiveRun.ok ? readEngine(archiveRun, 'archive') : null;

  const sealRun = say('seal', runEngine('seal', [contextPath, payloadPath, receiptPath, planPath], env));
  const seal = sealRun.ok ? readEngine(sealRun, 'seal') : null;

  const verifyRun = say('verify', runEngine('verify', [contextPath, payloadPath, receiptPath, planPath], { ...env, GTOS_VERIFY_OUT: outPath }));
  const verify = verifyRun.ok || verifyRun.status === 1 ? readEngine(verifyRun, 'verify') : null;

  const engineAfter = engineIdentity();
  const engineUnchanged = engineBefore.sha256 === engineAfter.sha256 && engineBefore.bytes === engineAfter.bytes;

  // The acceptance condition. It is a conjunction of facts read from the records, and every conjunct is a field this file
  // declared above. There is no OR anywhere in it.
  const checks = {
    engine_unchanged_across_the_phase: engineUnchanged,
    state_ok: !!(state && state.ok === true && state.archives === 0 && state.seals === 0),
    plan_written: !!(plan && plan.plan && typeof plan.plan.sha256 === 'string' && plan.member_count > 0),
    plan_pins_every_mandatory_role: !!(plan && plan.mandatory_roles_pinned === plan.mandatory_roles_required
      && plan.mandatory_roles_required > 0),
    plan_pins_a_required_set: !!(plan && plan.required_set_pinned === true),
    archive_written: !!(archive && archive.payload && archive.archives_after === 1),
    archive_is_the_plan: !!(archive && plan && archive.ordinal_digest === plan.ordinal_digest
      && archive.members === plan.member_count),
    seal_written: !!(seal && seal.receipt && seal.terminal === true),
    seal_consumed_every_role: !!(seal && plan && seal.mandatory_roles_consumed === plan.mandatory_roles_required),
    seal_is_the_payload: !!(seal && archive && seal.ordinal_digest === archive.ordinal_digest
      && seal.member_count === archive.members),
    verify_ok: !!(verify && verify.ok === true),
    verify_counted_every_obligation: !!(verify && Array.isArray(verify.obligation_names)
      && verify.obligation_count === verify.obligation_names.length && verify.obligation_count > 0),
    verify_schema_is_complete: !!(verify && verify.schema_problems
      && Array.isArray(verify.schema_problems.missing) && Array.isArray(verify.schema_problems.extra)
      && verify.schema_problems.missing.length === 0 && verify.schema_problems.extra.length === 0),
    verify_failed_nothing: !!(verify && Array.isArray(verify.failed_obligations) && verify.failed_obligations.length === 0),
  };
  const failed = Object.entries(checks).filter(([, v]) => v !== true).map(([k]) => k);
  return {
    task: 'GTOS-DSH-148', kind: 'f_phase', tag,
    what: 'one publication transaction, taken by the frozen engine, with every step read through the schema this driver '
      + 'declares and the acceptance condition written as a conjunction of those fields',
    engine: { before: engineBefore, after: engineAfter, unchanged: engineUnchanged },
    context: contextPath, spec: specPath, plan: planPath, payload: payloadPath, receipt: receiptPath, verify_record: outPath,
    steps, state, plan, archive, seal, verify,
    checks, failed_checks: failed,
    accepted: failed.length === 0,
    ok: failed.length === 0,
    ok_means: 'every step of the transaction succeeded, the engine bytes did not change while it ran, and the verification '
      + 'record says ok in its own declared fields with no missing or extra obligation names',
  };
};

// ---------------------------------------------------------------- a refused step
//
// For a negative, the driver takes one step and requires it to be refused. It reports the exit code, the refusal message
// the engine wrote, and whether that message is the one the case expected. A refusal for a different reason is a failure
// of the case, not a pass: "refused" is not enough, the refusal has to be about the obligation under test.
export const step = ({ verb, args, env = {}, expect }) => {
  const r = runEngine(verb, args, env);
  const text = (r.stderr || '') + '\n' + (r.stdout || '');
  let refusal = null;
  try {
    const j = JSON.parse(r.stderr);
    refusal = typeof j.refused === 'string' ? j.refused : null;
  } catch { refusal = null; }
  const refused = r.status === 2 && refusal !== null;
  const names = expect ? new RegExp(expect, 'i').test(text) : true;
  return { verb, argv: r.argv, status: r.status, refused, refusal, names_the_expected_obligation: names,
    expect: expect || null, stderr_bytes: r.stderr.length, stdout_bytes: r.stdout.length,
    stdout: r.stdout.length > 2000 ? r.stdout.slice(0, 2000) : r.stdout,
    stderr: r.stderr.length > 2000 ? r.stderr.slice(0, 2000) : r.stderr,
    ok: refused && names };
};

// ---------------------------------------------------------------- fixtures
//
// A fixture is a TEST_ONLY context under the production root's `test-only/` prefix. The engine refuses a TEST_ONLY context
// that claims the production task, and refuses one that lives outside the prefix, so a fixture cannot become the
// production transaction by being named well.
export const makeFixture = ({ num, at }) => {
  // A fixture directory is named for the run that made it, so re-running the suite never has to delete or overwrite a
  // fixture a previous run left behind: the earlier run's directories stay exactly as they were.
  const runTag = process.env.GTOS_FIXTURE_RUN || 'run';
  const dir = at || (R + '/test-only/' + runTag + '/fixture-' + num);
  if (exists(dir)) throw new Error('the fixture already exists: ' + dir);
  // The task name has to be of the form the engine's `tagFor` accepts -- GTOS-DSH-<digits> -- and it must not be the
  // production task. A fixture numbered 9148 is therefore a real task identity of its own, which is what makes the
  // transaction it takes a real one rather than a rehearsal.
  const task = 'GTOS-DSH-' + num;
  if (!/^\d+$/.test(String(num))) throw new Error('a fixture number must be digits: ' + num);
  if (task === 'GTOS-DSH-148') throw new Error('a fixture may not take the production task');
  fs.mkdirSync(dir, { recursive: true });
  const tag = String(num);
  const context = {
    task,
    scope: 'test_only',
    authorized_report_root: dir,
    fixed_state_path: dir + '/publication-state-' + tag + '.jsonl',
    production_destination: dir + '/archive-' + tag,
    what: 'a TEST_ONLY context: the same engine, the same verbs, a transaction that belongs to this directory',
  };
  const contextPath = dir + '/publication-context.json';
  fs.writeFileSync(contextPath, JSON.stringify(context, null, 2) + '\n', { flag: 'wx' });
  fs.mkdirSync(context.production_destination, { recursive: true });
  return { dir, task, tag, contextPath, context, destDir: context.production_destination, statePath: context.fixed_state_path };
};

// The role records a fixture's plan consumes. Their content is fixture-scoped -- what they pin is a fixture subject and a
// fixture version -- and that is the point: F tests what the engine does with a qualification record, not whether the
// package's consumers pass. The records the production seal consumes are the ones H writes from real business calls, and
// the engine consumes both through the same function.
export const roleRecordsFor = ({ roles, subject, consumerVersion, obligations, outDir, okValue = true }) => {
  fs.mkdirSync(outDir, { recursive: true });
  const out = [];
  for (const role of roles) {
    const record = {
      task: 'GTOS-DSH-148',
      role,
      subject,
      consumer_version: consumerVersion,
      entry: 'roles-148.mjs#' + role,
      business_called: true,
      obligations: (obligations || {})[role] || [{ name: 'the fixture role decided its obligation', value: okValue }],
      ok: okValue,
      what: 'a fixture role record: the entry it names, the subject it judged, and the obligations it decided',
    };
    const p = outDir + '/role-' + role + '.json';
    fs.writeFileSync(p, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
    const b = fs.readFileSync(p);
    out.push({ role, path: p, bytes: b.length, sha256: sha(b), ok_field: 'ok', subject, consumer_version: consumerVersion,
      entry: record.entry });
  }
  return out;
};

// ---------------------------------------------------------------- CLI
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF);
if (isMain) {
  const mode = process.argv[2];
  const arg = process.argv[3];
  const outPath = process.argv[4];
  if (!mode) { console.error('usage: drive-148.mjs phase|step|identity <arg> [out.json]'); process.exit(2); }
  if (mode === 'identity') {
    console.log(JSON.stringify(engineIdentity(), null, 2));
    process.exit(0);
  }
  if (mode === 'step') {
    // `step <verb> <context> <args...>` with an optional expected message as the last argument prefixed by `expect=`.
    const rest = process.argv.slice(4);
    const expectIdx = rest.findIndex((a) => a.startsWith('expect='));
    const expect = expectIdx >= 0 ? rest.splice(expectIdx, 1)[0].slice('expect='.length) : null;
    const r = step({ verb: arg, args: rest, expect });
    const text = JSON.stringify(r, null, 2) + '\n';
    if (outPath) { if (exists(outPath)) throw new Error('REFUSED: ' + outPath); fs.writeFileSync(outPath, text, { flag: 'wx' }); }
    console.log(text);
    process.exit(r.ok ? 0 : 1);
  }
  if (mode === 'phase') {
    const cfg = readJson(arg);
    const r = phase(cfg);
    const text = JSON.stringify(r, null, 2) + '\n';
    if (outPath) { if (exists(outPath)) throw new Error('REFUSED: ' + outPath); fs.writeFileSync(outPath, text, { flag: 'wx' }); }
    console.log(JSON.stringify({ tag: r.tag, accepted: r.accepted, failed: r.failed_checks,
      engine: r.engine.before, verify: r.verify ? { ok: r.verify.ok, obligation_count: r.verify.obligation_count } : null }, null, 2));
    process.exit(r.ok ? 0 : 1);
  }
  console.error('unknown mode: ' + mode);
  process.exit(2);
}
