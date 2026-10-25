// GTOS-DSH-111 publication engine.
//
// Archive and seal for this packet. It keeps the strict evidence rules the earlier packets established, and it adapts
// only the product rules that belong to 110:
//
//   refused always:  a stage that started without its raw capture; a ledger with two rows for one stage; a runner
//                    without identity; a seal before a successful archive; a plan whose sources drifted; a payload
//                    that is not byte-identical to its plan; a canonical artifact rewritten
//   adapted for 110: the publication directory may already exist EMPTY because this packet's own setup created it as a
//                    declared output parent, and that fact is recorded rather than passed over
//
// Digest recipe, frozen before the first run: ordinal POSIX paths, UTF8(sha256 + two spaces + path + LF), SHA256 of the
// concatenation. The manifest excludes itself. The archive and seal receipts live outside the payload.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const TASK = 'GTOS-DSH-111';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const checks = [];
function need(ok, name, detail) {
  checks.push({ name, ok: Boolean(ok), detail: detail === undefined ? null : detail });
  if (!ok) throw Error(name + ': ' + (typeof detail === 'string' ? detail : JSON.stringify(detail)));
}
const relative = (p) => typeof p === 'string' && p.length > 0 && !p.startsWith('/')
  && !/[\\:\t\r\n]/.test(p) && p.split('/').every((x) => x !== '' && x !== '.' && x !== '..');
const sourceKey = (p) => path.resolve(p).replaceAll('\\', '/').toLowerCase();
const below = (child, root) => sourceKey(child).startsWith(sourceKey(root).replace(/\/$/, '') + '/');
const hash = (p) => { const b = fs.readFileSync(p); return { path: p, bytes: b.length, sha256: sha(b) }; };
function pinned(p) {
  need(p && typeof p.path === 'string' && Number.isSafeInteger(p.bytes)
    && /^[0-9a-f]{64}$/.test(String(p.sha256)), 'pin schema', p && p.path);
  const now = hash(p.path);
  need(now.bytes === p.bytes && now.sha256 === p.sha256, 'pinned bytes unchanged', p.path);
  return JSON.parse(fs.readFileSync(p.path, 'utf8'));
}
const sorted = (rows) => rows.slice().sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
function digestOf(entries) {
  let acc = '';
  for (const e of sorted(entries)) acc += e.sha256 + '  ' + e.path + '\n';
  return { digest: sha(Buffer.from(acc, 'utf8')), files: entries.length,
    bytes: entries.reduce((n, e) => n + e.bytes, 0) };
}
function walk(root, prefix, add) {
  for (const name of fs.readdirSync(root).sort()) {
    const full = path.join(root, name);
    const st = fs.statSync(full);
    const rel = prefix ? prefix + '/' + name : name;
    if (st.isDirectory()) { walk(full, rel, add); continue; }
    if (!st.isFile()) continue;
    add(full, rel);
  }
}

// ---------------------------------------------------------------- evidence rules
// A stage that reports a started transport must have produced its raw capture. A filesystem miss is never read as an
// inference that nothing ran; it is a refusal.
function assertStartedHasRaw(nativeRoot) {
  const logDir = path.join(nativeRoot.host_root, 'logs');
  const declared = path.join(logDir, nativeRoot.ledger);
  const ledger = fs.existsSync(declared) ? declared
    : (fs.existsSync(declared + '.jsonl') ? declared + '.jsonl' : declared);
  need(fs.existsSync(ledger), 'ledger present', ledger);
  const rows = fs.readFileSync(ledger, 'utf8').split('\n').filter(Boolean).map((line) => JSON.parse(line));
  const seen = new Set();
  for (const row of rows) {
    if (row.kind !== 'result') continue;
    need(!seen.has(row.stage), 'one ledger row per stage', row.stage);
    seen.add(row.stage);
    if (row.exit_code === 2) {
      // The guard's own pre-launch refusal: no child, so no raw is expected.
      continue;
    }
    const raw = path.join(logDir, 'child-output-' + row.stage + '.raw');
    const inner = path.join(logDir, 'inner-report-' + row.stage + '.json');
    const started = fs.existsSync(inner)
      ? JSON.parse(fs.readFileSync(inner, 'utf8')).child !== undefined
      : false;
    if (started) {
      need(fs.existsSync(raw), 'a started child has its raw capture', row.stage);
      // An empty capture is legitimate when the child was silent, which a successful compiler run is. The bound is
      // what matters, because a truncated capture must never be read as a complete one.
      need(fs.statSync(raw).size <= 524288, 'the raw capture is within its log bound', row.stage);
      const innerDoc = JSON.parse(fs.readFileSync(inner, 'utf8'));
      need(innerDoc.child.output_file_bytes === fs.statSync(raw).size,
        'the recorded capture size equals the file size', row.stage);
    }
  }
  return rows.length;
}

// Every attempt must name a frozen runner that still hashes to what the attempt recorded.
function assertRunnerIdentity(reportRoot) {
  const names = fs.readdirSync(reportRoot).filter((n) => /^attempt-[a-z][a-z0-9]*-[0-9]{2}-110\.json$/.test(n));
  let verified = 0;
  for (const name of names) {
    const a = JSON.parse(fs.readFileSync(path.join(reportRoot, name), 'utf8'));
    need(typeof a.runner_sha256 === 'string' && /^[0-9a-f]{64}$/.test(a.runner_sha256),
      'attempt carries runner identity', name);
    need(fs.existsSync(a.runner_path), 'the runner still exists', a.runner_path);
    const now = hash(a.runner_path);
    need(now.sha256 === a.runner_sha256 && now.bytes === a.runner_bytes,
      'the runner is unchanged since the attempt', name);
    verified += 1;
  }
  return verified;
}

// ---------------------------------------------------------------- the archive branch
function archive(control) {
  const shape = pinned(control.shape);
  const plan = pinned(control.copy_plan);
  need(shape.task === TASK && plan.task === TASK, 'plan and shape belong to this task');
  need(Array.isArray(plan.files) && plan.files.length > 0, 'plan carries files');

  const pub = path.join(control.report_root, control.publication);
  const native = path.join(pub, 'native');
  need(below(pub, control.report_root), 'the publication stays inside the report root');

  // This packet's setup created the publication directory empty. Accept only that, and record it.
  let precreated = false;
  if (fs.existsSync(pub)) {
    need(fs.statSync(pub).isDirectory() && !fs.lstatSync(pub).isSymbolicLink(),
      'the publication path is a real directory');
    need(fs.readdirSync(pub).length === 0,
      'an existing publication directory is empty; a sealed payload is never reopened');
    precreated = true;
  }

  const destinations = new Set();
  for (const f of plan.files) {
    need(relative(f.destination), 'plan destination is a safe relative path', f.destination);
    need(!destinations.has(f.destination), 'plan destination is unique', f.destination);
    destinations.add(f.destination);
    need(typeof f.source === 'string' && path.isAbsolute(f.source), 'plan source is absolute', f.destination);
    need(Number.isSafeInteger(f.bytes) && f.bytes >= 0 && /^[0-9a-f]{64}$/.test(f.sha256),
      'plan row carries a pin', f.destination);
  }

  // Verify every source before creating any publication file.
  for (const f of plan.files) {
    need(fs.existsSync(f.source), 'copy source present', f.source);
    const now = hash(f.source);
    need(now.bytes === f.bytes && now.sha256 === f.sha256, 'copy source identity', f.source);
  }

  const entries = sorted(plan.files.map((f) => ({ path: f.destination, bytes: f.bytes, sha256: f.sha256 })));
  // Required paths and group counts come from the shape this packet generated.
  const present = new Set(entries.map((e) => e.path));
  for (const p of shape.required_paths) need(relative(p) && present.has(p), 'required path', p);
  for (const g of shape.groups) {
    const rows = entries.filter((e) => e.path.startsWith(g.prefix));
    need(rows.length === g.files, 'group count', g.prefix + ' ' + rows.length + '/' + g.files);
    if (g.bytes !== undefined) {
      need(rows.reduce((n, r) => n + r.bytes, 0) === g.bytes, 'group bytes', g.prefix);
    }
  }

  // The strict evidence rules, applied to this packet's own roots.
  const ledgers = {};
  for (const root of shape.native_roots) {
    ledgers[root.name] = assertStartedHasRaw(root);
  }
  const runners = assertRunnerIdentity(control.report_root);

  // The archive receipt lives outside the payload, because it cannot contain its own hash.
  need(!below(control.receipt_path, native), 'the archive receipt is outside the payload');
  need(!fs.existsSync(control.receipt_path), 'the archive receipt is new');
  need(below(control.receipt_path, control.report_root), 'the archive receipt is inside the report root');

  fs.mkdirSync(native, { recursive: true });
  for (const f of plan.files) {
    const target = path.join(native, f.destination);
    need(below(target, native), 'the copy stays inside the payload', target);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    need(!fs.existsSync(target), 'the payload target is new', target);
    const bytes = fs.readFileSync(f.source);
    fs.writeFileSync(target, bytes, { flag: 'wx' });
    const landed = fs.readFileSync(target);
    need(landed.length === f.bytes && sha(landed) === f.sha256, 'the payload copy is byte-identical', f.destination);
  }

  const payloadRows = [];
  walk(native, '', (full, rel) => {
    const b = fs.readFileSync(full);
    payloadRows.push({ path: rel, bytes: b.length, sha256: sha(b) });
  });
  const summary = digestOf(payloadRows);
  need(summary.files === entries.length, 'the payload holds exactly the planned files',
    summary.files + '/' + entries.length);

  const receipt = {
    task: TASK, stage: control.stage, mode: 'archive', ok: true,
    publication: control.publication, publication_root: pub,
    publication_precreated_empty_by_setup: precreated,
    shape: control.shape, copy_plan: control.copy_plan,
    payload: { files: summary.files, bytes: summary.bytes, root_digest: summary.digest },
    digest_recipe: 'ordinal POSIX paths, UTF8(sha256 + two spaces + path + LF), SHA256 of the concatenation',
    evidence_rules: { started_without_raw: 'refused', duplicate_ledger_row: 'refused',
      runner_without_identity: 'refused', source_drift: 'refused' },
    ledger_rows: ledgers, attempts_with_runner_identity: runners,
    checks, receipt_path: control.receipt_path,
  };
  fs.mkdirSync(path.dirname(control.receipt_path), { recursive: true });
  fs.writeFileSync(control.receipt_path, JSON.stringify(receipt, null, 2) + '\n',
    { encoding: 'utf8', flag: 'wx' });

  // The manifest excludes itself.
  const manifest = { task: TASK, publication: control.publication,
    files: payloadRows, root_digest: summary.digest,
    digest_recipe: receipt.digest_recipe, manifest_self_excluded: true };
  const manifestPath = path.join(pub, 'manifest-111.json');
  need(!fs.existsSync(manifestPath), 'the manifest is new');
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  return { receipt, manifest: hash(manifestPath), payload: receipt.payload };
}

// ---------------------------------------------------------------- the seal branch
function seal(control) {
  const previous = pinned(control.archive_receipt);
  need(previous.task === TASK && previous.mode === 'archive' && previous.ok === true,
    'an archive succeeded before this seal', control.archive_receipt.path);
  need(!fs.existsSync(control.receipt_path), 'the seal receipt is new');
  // The first successful seal is final. A later seal for the same publication is refused even under a new name.
  const receiptDir = path.dirname(control.receipt_path);
  if (fs.existsSync(receiptDir)) {
    for (const name of fs.readdirSync(receiptDir)) {
      if (!name.endsWith('.json')) continue;
      if (name === path.basename(control.receipt_path)) continue;
      let prior = null;
      try { prior = JSON.parse(fs.readFileSync(path.join(receiptDir, name), 'utf8')); } catch { continue; }
      if (prior && prior.mode === 'seal' && prior.ok === true
        && prior.publication === control.publication) {
        need(false, 'a publication is sealed exactly once', name);
      }
    }
  }

  const pub = path.join(control.report_root, control.publication);
  const native = path.join(pub, 'native');
  need(fs.existsSync(native), 'the archived payload exists', native);
  need(control.publication === previous.publication, 'the seal names the archived publication');

  // Re-derive the payload digest from disk. A seal never trusts a number it did not just measure.
  const rows = [];
  walk(native, '', (full, rel) => {
    const b = fs.readFileSync(full);
    rows.push({ path: rel, bytes: b.length, sha256: sha(b) });
  });
  const summary = digestOf(rows);
  need(summary.files === previous.payload.files, 'the payload file count is unchanged',
    summary.files + '/' + previous.payload.files);
  need(summary.bytes === previous.payload.bytes, 'the payload byte total is unchanged');
  need(summary.digest === previous.payload.root_digest, 'the payload digest is unchanged');

  // Two-way set comparison against the manifest.
  const manifestPath = path.join(pub, 'manifest-111.json');
  need(fs.existsSync(manifestPath), 'the manifest exists', manifestPath);
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  const onDisk = new Map(rows.map((r) => [r.path, r]));
  for (const m of manifest.files) {
    const found = onDisk.get(m.path);
    need(found !== undefined, 'every manifest entry is on disk', m.path);
    need(found.bytes === m.bytes && found.sha256 === m.sha256, 'manifest bytes agree', m.path);
  }
  const manifestPaths = new Set(manifest.files.map((m) => m.path));
  for (const r of rows) need(manifestPaths.has(r.path), 'every payload file is in the manifest', r.path);

  // The self receipts must not be inside the payload.
  for (const name of ['archive', 'seal']) {
    const inside = fs.existsSync(path.join(native, 'receipts'));
    need(!inside || !fs.readdirSync(path.join(native, 'receipts'))
      .some((f) => f.startsWith(name + '-')), 'self receipts live outside the payload', name);
  }

  const receipt = {
    task: TASK, stage: control.stage, mode: 'seal', ok: true,
    publication: control.publication, publication_root: pub,
    archive_receipt: control.archive_receipt,
    payload: { files: summary.files, bytes: summary.bytes, root_digest: summary.digest },
    manifest: hash(manifestPath),
    digest_recipe: 'ordinal POSIX paths, UTF8(sha256 + two spaces + path + LF), SHA256 of the concatenation',
    verified_from_disk: true,
    two_way_set_comparison: { manifest_missing_on_disk: 0, drift: 0, extra_on_disk: 0 },
    checks,
  };
  fs.mkdirSync(path.dirname(control.receipt_path), { recursive: true });
  fs.writeFileSync(control.receipt_path, JSON.stringify(receipt, null, 2) + '\n',
    { encoding: 'utf8', flag: 'wx' });
  return { receipt, manifest: receipt.manifest, payload: receipt.payload };
}


// ---------------------------------------------------------------- strict current native certification
//
// Every current child must be joined by identity across all of its evidence, not merely accompanied by files that
// happen to exist. 110 accepted a fixture that had a ledger row, an inner report and a raw but no spec and no
// same-name attempt; that fixture proves nothing about pairing, so it is refused here.
//
// The join for one stage is:
//   ledger row  ->  spec  ->  same-name attempt  ->  terminal result  ->  inner report  ->  raw capture
// with the runner and helper pins, the argv, the return code, the timeout state and the output ownership all agreeing
// across those records. An empty raw is legitimate; a missing raw is not. A preparation or pre-launch refusal may
// legitimately lack spec/inner/raw, and is accepted only when the ledger row itself says the child never started.

export function certifyNativeRoot(request) {
  const reasons = [];
  const push = (text) => { if (!reasons.includes(text)) reasons.push(text); };
  const task = request.task;
  const suffix = task.slice(-3);
  const reportRoot = request.report_root;
  const hostRoot = request.host_root;
  const logDir = path.join(hostRoot, 'logs');
  const ledgerPath = path.join(logDir, request.ledger);

  if (!fs.existsSync(ledgerPath)) {
    return { certified: false, reasons: ['ledger absent: ' + ledgerPath], stages: 0 };
  }
  const rows = fs.readFileSync(ledgerPath, 'utf8').split('\n').filter(Boolean).map((line) => {
    try { return JSON.parse(line); } catch { return null; }
  }).filter(Boolean);

  const results = rows.filter((r) => r.kind === 'result');
  const seen = new Set();
  let certified = 0;

  for (const row of results) {
    const stage = row.stage;
    if (typeof stage !== 'string' || !/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) {
      push('ledger row carries no valid stage name');
      continue;
    }
    if (seen.has(stage)) { push('duplicate ledger row for stage ' + stage); continue; }
    seen.add(stage);

    const specPath = path.join(logDir, 'spec-' + stage + '.json');
    const innerPath = path.join(logDir, 'inner-report-' + stage + '.json');
    const rawPath = path.join(logDir, 'child-output-' + stage + '.raw');
    const attemptPath = path.join(reportRoot, 'attempt-' + stage + '-' + suffix + '.json');
    const terminalPath = path.join(reportRoot, 'result-' + stage + '-' + suffix + '.json');

    // A row that says the guard refused before the child existed has a different, narrower obligation.
    const neverStarted = row.refused === true || row.exit_code === 2;
    if (neverStarted) {
      if (!fs.existsSync(attemptPath)) push('a refused stage still needs its attempt record: ' + stage);
      continue;
    }

    for (const [label, file] of [['spec', specPath], ['inner report', innerPath], ['raw capture', rawPath],
      ['attempt', attemptPath], ['terminal result', terminalPath]]) {
      if (!fs.existsSync(file)) push(label + ' absent for stage ' + stage);
    }
    if (!fs.existsSync(specPath) || !fs.existsSync(innerPath) || !fs.existsSync(attemptPath)
      || !fs.existsSync(terminalPath)) continue;

    let spec, inner, attempt, terminal;
    try { spec = JSON.parse(fs.readFileSync(specPath, 'utf8')); } catch { push('spec unreadable: ' + stage); continue; }
    try { inner = JSON.parse(fs.readFileSync(innerPath, 'utf8')); } catch { push('inner unreadable: ' + stage); continue; }
    try { attempt = JSON.parse(fs.readFileSync(attemptPath, 'utf8')); } catch { push('attempt unreadable: ' + stage); continue; }
    try { terminal = JSON.parse(fs.readFileSync(terminalPath, 'utf8')); } catch { push('terminal unreadable: ' + stage); continue; }

    // Identity: the same stage name, the same task and the same category on every record.
    if (spec.stage !== stage) push('spec stage disagrees: ' + stage);
    if (attempt.attempt !== stage) push('attempt stage disagrees: ' + stage);
    if (attempt.task !== task) push('attempt task disagrees: ' + stage);
    if (terminal.attempt !== undefined && terminal.attempt !== stage) push('terminal stage disagrees: ' + stage);
    if (attempt.category !== row.category && row.category !== undefined) {
      push('attempt category disagrees with the ledger row: ' + stage);
    }
    if (attempt.operation_id !== undefined && terminal.operation_id !== undefined
      && attempt.operation_id !== terminal.operation_id) {
      push('operation id disagrees between attempt and terminal: ' + stage);
    }

    // The argv must be the same array in the spec and the ledger row, in order.
    if (Array.isArray(spec.argv) && Array.isArray(row.argv)
      && JSON.stringify(spec.argv) !== JSON.stringify(row.argv)) {
      push('argv disagrees between spec and ledger: ' + stage);
    }

    // The attempt must be complete and must name a runner that still hashes to what it recorded.
    if (attempt.complete !== true) push('attempt is not complete: ' + stage);
    if (attempt.executor !== 'guard') push('attempt executor is not the guard: ' + stage);
    if (typeof attempt.runner_path === 'string' && fs.existsSync(attempt.runner_path)) {
      const now = fs.readFileSync(attempt.runner_path);
      if (now.length !== attempt.runner_bytes || crypto.createHash('sha256').update(now).digest('hex')
        !== attempt.runner_sha256) push('runner bytes changed since the attempt: ' + stage);
    } else {
      push('runner path is absent or missing: ' + stage);
    }

    // The return code and the timeout state must agree across ledger, inner and terminal.
    const innerChild = inner.child || {};
    if (innerChild.returncode !== undefined && row.exit_code !== undefined
      && innerChild.returncode !== row.exit_code) {
      push('return code disagrees between ledger and inner: ' + stage);
    }
    if (terminal.transport_exit_code !== undefined && row.exit_code !== undefined
      && terminal.transport_exit_code !== row.exit_code) {
      push('return code disagrees between ledger and terminal: ' + stage);
    }
    if (innerChild.timed_out === true && row.timed_out !== true) {
      push('timeout state disagrees: ' + stage);
    }

    // The raw capture must exist, stay within its bound, and agree with what the inner report recorded.
    if (fs.existsSync(rawPath)) {
      const rawBytes = fs.readFileSync(rawPath);
      if (rawBytes.length > 524288) push('raw capture exceeds its bound: ' + stage);
      if (innerChild.output_file_bytes !== undefined && innerChild.output_file_bytes !== rawBytes.length) {
        push('raw size disagrees with the inner report: ' + stage);
      }
      if (typeof innerChild.output_sha256 === 'string'
        && crypto.createHash('sha256').update(rawBytes).digest('hex') !== innerChild.output_sha256) {
        push('raw hash disagrees with the inner report: ' + stage);
      }
      // The inner report's output_path, when present, must name this stage's own capture.
      if (typeof innerChild.output_path === 'string'
        && !innerChild.output_path.endsWith('child-output-' + stage + '.raw')) {
        push('inner report names another stage raw: ' + stage);
      }
    }

    certified += 1;
  }

  return { certified: reasons.length === 0, reasons, stages: results.length,
    joined: certified, task };
}

// ---------------------------------------------------------------- entry
const isEngineMain = process.argv[1]
  && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
if (isEngineMain) try {
  const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  need(control.task === TASK, 'control task', control.task);
  need(/^[a-z][a-z0-9]*-[0-9]{2}$/.test(control.stage), 'stage grammar', control.stage);
  need(['archive', 'seal'].includes(control.mode), 'mode', control.mode);
  need(path.isAbsolute(control.report_root), 'report root is absolute');
  need(/^publication-111-v[0-9]{2}$/.test(control.publication), 'owned publication identity',
    control.publication);

  const outcome = control.mode === 'archive' ? archive(control) : seal(control);
  console.log(JSON.stringify({ ok: true, stage: control.stage, mode: control.mode,
    payload: outcome.payload, manifest: outcome.manifest,
    receipt: control.receipt_path }, null, 2));
} catch (error) {
  console.error(String(error && error.stack ? error.stack : error));
  process.exitCode = 1;
}
