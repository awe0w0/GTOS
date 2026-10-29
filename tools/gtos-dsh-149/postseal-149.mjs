// GTOS-DSH-149: the post-seal external receipt.
//
// WHY THIS IS A SEPARATE PROGRAM. The payload carries a report that describes the payload, and a record cannot verify the
// thing it is a member of: its own hash would have to appear inside itself. So the last step is taken from outside. This
// file reads the sealed payload, extracts it into a directory that is not the package, and then re-answers the payload's
// questions from the extracted bytes alone -- the required set, the seven consumer roles, the chain, the negatives -- with
// the same harness bytes the packet committed. Nothing here trusts the report root: if a judgement needed a file that is
// not in the payload, the extraction would not have it and the answer would come back false.
//
// WHAT "EXTERNAL" MEANS HERE, CONCRETELY. The required set is resolved against the extraction root and nowhere else, and
// the count of members that could only be satisfied from outside the payload is reported as `external`. The 147 packet had
// 206 such members -- its runtime supply -- and said so. This packet carries them, so the number is expected to be zero; if
// it is not zero, that is the finding, not a footnote.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const R = 'F:/GTOS-Chromium/artifacts/native-exact-role-semantics-and-closed-origin-provenance-20261029-a';
const R147 = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const R148 = 'F:/GTOS-Chromium/artifacts/native-preserved-finality-and-executed-package-consumers-20261029-a';
const TASK = 'GTOS-DSH-149';
const BLOCK = 512;

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const exists = (p) => { try { fs.statSync(p); return true; } catch { return false; } };
const nextRecord = (series) => {
  for (let i = 1; i < 100; i += 1) {
    const p = R + '/' + series + '-' + String(i).padStart(2, '0') + '.json';
    if (!exists(p)) return p;
  }
  throw new Error('no free record name in the series ' + series);
};

// ---------------------------------------------------------------- the payload reader
//
// A ustar reader, written here rather than imported, because the engine that wrote the payload is a program that dispatches
// on `process.argv` when loaded: importing it to borrow a function would run it. The reader is the reader the engine's own
// `readTar` is, and its answers are checked against the plan's ordinal digest, so a wrong reader cannot pass quietly.
const readTar = (buf) => {
  const members = [];
  let off = 0;
  const str = (b, from, len) => {
    const s = b.subarray(from, from + len);
    const z = s.indexOf(0);
    return s.subarray(0, z === -1 ? s.length : z).toString('utf8');
  };
  const oct = (b, from, len) => {
    const s = str(b, from, len).trim();
    return s.length === 0 ? 0 : parseInt(s, 8);
  };
  while (off + BLOCK <= buf.length) {
    const header = buf.subarray(off, off + BLOCK);
    if (header.every((x) => x === 0)) break;
    const name = str(header, 0, 100);
    const size = oct(header, 124, 12);
    const mode = oct(header, 100, 8);
    const type = String.fromCharCode(header[156] || 48);
    const prefix = str(header, 345, 155);
    const full = prefix ? prefix + '/' + name : name;
    off += BLOCK;
    const body = buf.subarray(off, off + size);
    if (type === '0' || type === '\0' || type === '') {
      members.push({ name: full, size, mode, sha256: sha(body), bytes: body });
    }
    off += size + (size % BLOCK === 0 ? 0 : BLOCK - (size % BLOCK));
  }
  return members;
};

// The engine's own definition: the lines are joined with a newline, not concatenated. Recomputing it here rather than
// trusting the receipt is the point -- a payload that is exactly the plan has to be shown to be so from the payload's bytes.
const ordinalDigest = (members) => sha(Buffer.from(members.map((m, i) => `${i}\0${m.name}\0${m.size}\0${m.sha256}`)
  .join('\n'), 'utf8'));

// ---------------------------------------------------------------- main
const main = () => {
  const payloadPath = process.argv[2];
  const planPath = process.argv[3];
  const receiptPath = process.argv[4];
  for (const [what, p] of [['payload', payloadPath], ['plan', planPath], ['receipt', receiptPath]]) {
    if (!p || !exists(p)) throw new Error('absent: the ' + what + ' at ' + p);
  }
  const payloadBytes = fs.readFileSync(payloadPath);
  const plan = readJson(planPath);
  const receipt = readJson(receiptPath);

  // The extraction root is a new directory, so nothing that was already on disk can stand in for a payload member. Both the
  // run directory and the receipt's own path can be declared, so the same program can be rehearsed over a fixture payload
  // before the production transaction exists: a receipt whose only run is the one after the seal is a receipt whose
  // behaviour was never tested.
  const RUN = 'postseal-' + String(process.pid);
  const OUT = (process.env.GTOS_POSTSEAL_RUN || (R + '/archive-149/harness-' + RUN)).replace(/\\/g, '/');
  if (exists(OUT)) throw new Error('CreateNew refused: ' + OUT);
  fs.mkdirSync(OUT + '/extracted', { recursive: true });

  const members = readTar(payloadBytes);
  const written = [];
  for (const m of members) {
    const to = OUT + '/extracted/' + m.name;
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.writeFileSync(to, m.bytes, { mode: m.mode || 420 });
    written.push({ name: m.name, size: m.size, sha256: m.sha256 });
  }
  // Read back from disk rather than from the buffers that were written: an extraction that says it wrote a file and did
  // not is exactly the failure this step exists to catch.
  let identical = 0;
  const drifted = [];
  for (const m of members) {
    const back = fs.readFileSync(OUT + '/extracted/' + m.name);
    if (sha(back) === m.sha256 && back.length === m.size) identical += 1;
    else drifted.push({ name: m.name, want: m.sha256, got: sha(back) });
  }

  const planDigest = ordinalDigest(plan.members.map((m) => ({ name: m.path, size: m.bytes, sha256: m.sha256 })));
  const payloadDigest = ordinalDigest(written);
  const receiptDigest = receipt.ordinal_digest;

  // The required set, resolved against the extraction root and nothing else.
  const required = (() => {
    // The records live under `judgements/`, not at the top of the payload; reading the top level and finding nothing would
    // have said "the payload carries no required set" about a payload that carries three of them.
    const dir = OUT + '/extracted/judgements';
    const candidates = (exists(dir) ? fs.readdirSync(dir) : [])
      .filter((n) => /^required-set-149-\d+\.json$/.test(n)).sort();
    if (!candidates.length) return { present: false, why: 'the payload carries no required set record' };
    const held = candidates.map((n) => ({ n, j: readJson(dir + '/' + n) })).filter((x) => x.j.ok === true);
    if (!held.length) return { present: false, why: 'the payload carries no required set that held' };
    const rec = held[held.length - 1];
    const notRead = rec.j.closure.not_read || [];
    // The closure is re-resolved here from the extraction, member by member, rather than read off the record. A record that
    // says 689/689 is a claim; the number that matters is the one this step gets when it looks.
    let resolvedHere = 0;
    const missing = [];
    for (const m of rec.j.required_physical_set.members) {
      const p = OUT + '/extracted/' + m.in_package;
      if (!exists(p)) { missing.push({ member: m.in_package, why: 'absent from the extraction' }); continue; }
      const b = fs.readFileSync(p);
      if (sha(b) === m.sha256 && b.length === m.bytes) resolvedHere += 1;
      else missing.push({ member: m.in_package, why: 'the bytes are not the bytes it was required as' });
    }
    return {
      present: true, record: 'judgements/' + rec.n,
      total: rec.j.required_physical_set.total,
      resolved_in_the_payload: resolvedHere,
      recorded_resolved: rec.j.closure.members,
      required_not_read: rec.j.closure.required_not_read,
      missing: missing.slice(0, 8),
      // Counted, not asserted. A member the extraction does not hold is a member this step could not resolve from the
      // payload, and there is nothing else it may resolve from -- so the number is the length of the missing list, and a
      // hard-coded zero here would be the very claim this step exists to test.
      external: missing.length,
      external_means: 'every required member was looked for under the extraction root and re-hashed there; the count is the '
        + 'number that was not found, and none of them was satisfied from a live tree or from the report root',
      first_unread: notRead.slice(0, 6),
      by_role: rec.j.required_physical_set.by_role,
    };
  })();

  // The seven roles, re-run from the extracted tree with the harness the payload carries.
  //
  // THE HARNESS COMES FROM THE PAYLOAD AND FROM NOWHERE ELSE. An earlier version of this file fell back to the working tree
  // when the payload did not carry one, which is the exact failure the 148 review names: a receipt that re-decides with code
  // the payload does not contain is reporting on a different program. There is no fallback now. If `engine/roles-149.mjs` is
  // not a member, the receipt records that as a problem and the seven roles are simply not re-run.
  //
  // Every root the harness resolves is redirected into the extraction, so the re-run cannot read the live report root even by
  // accident: `CODE`/`ENGINE`/`TOOLS146` all point at the extracted `engine/`, the fixture tree is inside the run directory,
  // and the run directory is declared as the harness's own output. The live report root and the two preserved artifact roots
  // are declared forbidden, so a read of any of them is counted rather than assumed away.
  const harnessInPayload = OUT + '/extracted/engine/roles-149.mjs';
  const harness = exists(harnessInPayload) ? harnessInPayload : null;
  const harnessBytes = harness ? fs.readFileSync(harness) : null;
  let roles = { ran: false, why: 'the payload carries no engine/roles-149.mjs, so there is nothing to re-run' };
  if (harnessBytes) {
    const outDir = OUT + '/roles';
    fs.mkdirSync(outDir, { recursive: true });
    const engineDir = OUT + '/extracted/engine';
    const r = spawnSync(process.execPath, [harness], {
      encoding: 'utf8',
      env: { ...process.env,
        GTOS_H_ROLES_PKG: OUT + '/extracted',
        GTOS_H_ROLES_OUT: outDir,
        GTOS_H_ROLES_CODE: engineDir,
        GTOS_H_ROLES_ENGINE: engineDir,
        GTOS_H_ROLES_TOOLS146: engineDir,
        GTOS_H_ROLES_TESTONLY: outDir + '/testonly',
        // The whole run root, not just the role directory: the extraction, the fixture tree and the role records all live
        // under it, and every one of those reads is this receipt reading what it just wrote. Everything in the report root
        // OUTSIDE this directory stays forbidden, so a role that reached back into the live tree is still counted.
        GTOS_H_ROLES_EXEMPT: OUT,
        GTOS_H_ROLES_FORBID: [R, R147, R148].join(path.delimiter) },
      windowsHide: true, maxBuffer: 64 * 1024 * 1024,
    });
    const summaryPath = outDir + '/summary.json';
    const summary = exists(summaryPath) ? readJson(summaryPath) : null;
    roles = {
      ran: true, harness: { path: harness, bytes: harnessBytes.length, sha256: sha(harnessBytes) },
      harness_is_the_payload_member: true,
      status: r.status, summary_path: summaryPath,
      // The summary names the counts; the index names each record's path, hash and subject. Both are read, and `roles` here
      // is the index's list because that is the one a later step can follow to a file.
      roles: summary && exists(outDir + '/index.json') ? readJson(outDir + '/index.json').roles : null,
      executed: summary ? summary.executed : null,
      green: summary ? summary.green : null, blocked: summary ? (summary.blocked || []) : null,
      negatives: summary ? summary.negatives : null, stability: summary ? summary.stability : null,
      ok: !!(summary && summary.ok === true),
      stdout_tail: (r.stdout || '').slice(-2000), stderr_tail: (r.stderr || '').slice(-2000),
    };
  }

  // THE RE-RUN IS COMPARED WITH WHAT THE PAYLOAD RECORDED.
  //
  // This is the property the 148 review found broken: its payload carried role records and a harness that did not produce
  // them, and nothing compared the two. Here the payload carries both, and the receipt re-executes the harness and puts the
  // answer beside the recorded one. The comparison is on the fields that carry the decision -- the verdict, the obligation
  // names and values, the negative names and outcomes, and the number of reads -- and not on the whole record, because the
  // records legitimately name the absolute root they were produced against and the extraction is a different root. A role
  // that decided the same thing in both places is what reproducibility means here; a record that cannot be reproduced is not
  // evidence of anything.
  if (roles.ran && exists(OUT + '/roles/index.json')) {
    const agree = [];
    const index = readJson(OUT + '/roles/index.json');
    for (const r of index.roles) {
      const recorded = OUT + '/extracted/judgements/roles-149/' + r.role + '/' + r.role + '.json';
      const reRunPath = r.path;
      if (!exists(recorded) || !exists(reRunPath)) {
        agree.push({ role: r.role, comparable: false, why: 'one side of the comparison is not present' });
        continue;
      }
      const a = readJson(recorded);
      const b = readJson(reRunPath);
      const pick = (x) => JSON.stringify({
        ok: x.ok,
        obligations: (x.obligations || []).map((o) => [o.name, o.value]),
        negatives: (x.negatives || []).map((n) => [n.name, n.ok]),
        reads_total: x.reads ? x.reads.total : null,
        subject_bucket: x.subject_bucket,
      });
      agree.push({ role: r.role, comparable: true, same: pick(a) === pick(b),
        recorded_ok: a.ok, re_run_ok: b.ok,
        recorded_reads: a.reads ? a.reads.total : null, re_run_reads: b.reads ? b.reads.total : null });
    }
    roles.agrees_with_the_payload = {
      roles: agree.length,
      same: agree.filter((x) => x.same === true).length,
      detail: agree,
      ok: agree.length === 7 && agree.every((x) => x.same === true),
    };
  }

  // The chain, re-judged from the extracted tree -- again with the payload's own copy of the judge, for the same reason.
  const chainInPayload = OUT + '/extracted/engine/chain-149.mjs';
  const chainJudge = exists(chainInPayload) ? chainInPayload : null;
  const chainRun = chainJudge
    ? spawnSync(process.execPath, [chainJudge, OUT + '/extracted', OUT + '/c-chain.json'], {
      encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024 })
    : { status: null, stderr: 'the payload carries no engine/chain-149.mjs' };
  const chain = exists(OUT + '/c-chain.json') ? (() => {
    const j = readJson(OUT + '/c-chain.json');
    return { ok: j.ok === true, judge_is_the_payload_member: chainJudge !== null, stages_checked: j.stages_checked,
      stages_held: j.stages_held, preserved: j.preserved, facts: j.facts,
      problems: j.problems ? j.problems.length : null };
  })() : { ok: false, why: 'the chain judge wrote no record', status: chainRun.status,
    stderr_tail: (chainRun.stderr || '').slice(-1500) };

  // No live fallback. Three separate questions, kept separate because they are three different failures.
  //
  //   symlinks        -- a member that is a link could resolve anywhere, so there may be none.
  //   provenance      -- the payload does carry records that quote the report root: the plan's `from` fields and the H role
  //                      records' traced roots. That is provenance, not resolution, so it is counted and reported rather than
  //                      failed. What proves it is not used is the third question.
  //   resolution      -- the required set is re-resolved from the extraction, and the harness re-run there forbids the report
  //                      root outright and counts any read of it. Zero forbidden reads is the direct statement that nothing was
  //                      satisfied from the live tree, and it is stronger than a text scan.
  const noFallback = (() => {
    const aliases = [];
    const naming = [];
    const roots = [R, R147, R148];
    const walk = (dir, rel) => {
      for (const n of fs.readdirSync(dir).sort()) {
        const full = dir + '/' + n;
        const r = rel ? rel + '/' + n : n;
        const st = fs.lstatSync(full);
        if (st.isSymbolicLink()) { aliases.push({ member: r, why: 'a symlink in the payload' }); continue; }
        if (st.isDirectory()) { walk(full, r); continue; }
        if (!/\.(json|jsonl|mjs|cjs|md|txt)$/.test(n)) continue;
        const text = fs.readFileSync(full, 'utf8');
        for (const root of roots) {
          if (text.includes(root)) naming.push({ member: r, root });
        }
      }
    };
    walk(OUT + '/extracted', '');
    // What the re-run harness reported about its own reads.
    //
    // THE HARNESS MEASURES FORBIDDEN ROOTS, SO THIS STEP REQUIRES THE MEASUREMENT RATHER THAN ACCEPTING ITS ABSENCE. The 148
    // receipt treated `forbidden_reads === null` as "the harness does not measure this, so decide it from the trace instead".
    // That is a blind spot with a justification attached: a harness that does not measure the live root cannot show it did not
    // read it. The harness in this payload carries a per-path allowlist and an explicit forbidden-root list, both of which are
    // reported per role, so `null` here means the wrong harness ran -- which is a problem, not an exemption. The three numbers
    // below are therefore all required to be present and zero, and each role's allowlist is required to classify every distinct
    // path it saw.
    const reRun = exists(OUT + '/roles/index.json') ? readJson(OUT + '/roles/index.json') : null;
    const perRole = [];
    if (reRun) {
      for (const r of reRun.roles) {
        const rec = readJson(r.path);
        const outsideLive = (rec.reads.outside_paths || []).filter((p) => !p.startsWith(OUT.replace(/\\/g, '/')));
        const io = rec.io || null;
        perRole.push({
          role: rec.role,
          total: rec.reads.total,
          in_package: rec.reads.in_package,
          in_packet_records: rec.reads.in_packet_records,
          elsewhere: rec.reads.elsewhere,
          elsewhere_paths_outside_the_extraction: outsideLive.length,
          forbidden_reads: rec.reads.forbidden_reads === undefined ? null : rec.reads.forbidden_reads,
          distinct_paths: io ? io.distinct_paths : null,
          unclassified_paths: io ? io.unclassified.length : null,
          io_ok: io ? io.ok === true : null,
          ok: rec.reads.total > 0 && rec.reads.elsewhere === 0 && outsideLive.length === 0
            && rec.reads.forbidden_reads === 0 && io !== null && io.ok === true,
        });
      }
    }
    // `null` if ANY role did not report the number. That is deliberate: a sum that silently drops the roles that did not
    // measure would be a smaller number, and a smaller number is what a blind spot looks like.
    const measured = perRole.length > 0 && perRole.every((p) => typeof p.forbidden_reads === 'number');
    const forbiddenReads = measured ? perRole.reduce((n, p) => n + p.forbidden_reads, 0) : null;
    const unclassified = perRole.every((p) => typeof p.unclassified_paths === 'number')
      ? perRole.reduce((n, p) => n + p.unclassified_paths, 0) : null;
    return {
      symlinks: aliases.length,
      members_quoting_the_report_root: naming.length,
      quoting_means: 'records in the payload name the report root in provenance fields; the post-seal derivation does not read '
        + 'through them, which is what the numbers below say',
      required_resolved_outside_the_extraction: required.present ? (required.total - required.resolved_in_the_payload) : null,
      roles_re_run: perRole.length,
      roles_whose_trace_left_the_extraction: perRole.filter((p) => !p.ok).length,
      per_role: perRole,
      forbidden_reads: forbiddenReads,
      forbidden_reads_measured: measured,
      unclassified_paths: unclassified,
      forbidden_reads_means: measured
        ? 'the harness measured reads of the roots this receipt forbids, and there were none'
        : 'the harness did not report a forbidden-read count for every role, so nothing here says the live root was not read',
      ok: aliases.length === 0
        && required.present === true
        && required.resolved_in_the_payload === required.total
        && perRole.length === 7
        && perRole.every((p) => p.ok)
        && measured === true
        && forbiddenReads === 0
        && unclassified === 0,
    };
  })();

  const problems = [];
  if (payloadBytes.length !== receipt.payload.bytes) problems.push({ why: 'the payload is not the size the seal recorded',
    want: receipt.payload.bytes, got: payloadBytes.length });
  if (sha(payloadBytes) !== receipt.payload.sha256) problems.push({ why: 'the payload is not the bytes the seal recorded' });
  if (planDigest !== plan.ordinal_digest) problems.push({ why: 'the plan does not digest to its own ordinal digest' });
  if (payloadDigest !== plan.ordinal_digest) problems.push({ why: 'the payload is not the plan' });
  if (receiptDigest !== plan.ordinal_digest) problems.push({ why: 'the receipt is not the plan' });
  if (drifted.length) problems.push({ why: 'a member did not survive extraction', drifted: drifted.slice(0, 6) });
  if (!required.present) problems.push({ why: 'the required set is not in the payload', detail: required.why });
  else if (required.required_not_read !== 0) problems.push({ why: 'the required set says members went unread',
    count: required.required_not_read });
  if (!roles.ok) problems.push({ why: 'the seven roles did not all hold when re-run from the extracted payload',
    blocked: roles.blocked, status: roles.status });
  if (roles.ran !== true || roles.harness_is_the_payload_member !== true) problems.push({
    why: 'the payload does not carry the harness that wrote its own role records, so the roles could not be re-decided from '
      + 'the payload', detail: roles.why || null });
  if (!roles.agrees_with_the_payload || roles.agrees_with_the_payload.ok !== true) problems.push({
    why: 'the re-run of the roles does not reproduce the decision the payload records',
    detail: roles.agrees_with_the_payload ? roles.agrees_with_the_payload.detail.filter((x) => x.same !== true) : null });
  if (!chain.ok) problems.push({ why: 'the chain judge did not hold on the extracted payload', detail: chain });
  if (chain.judge_is_the_payload_member !== true) problems.push({
    why: 'the payload does not carry the chain judge, so the chain was not re-decided from the payload' });
  if (!noFallback.ok) problems.push({ why: 'the payload does not stand on its own: a symlink, a required member resolved '
    + 'outside the extraction, or a read of the forbidden root', detail: {
    symlinks: noFallback.symlinks,
    required_resolved_outside_the_extraction: noFallback.required_resolved_outside_the_extraction,
    forbidden_reads: noFallback.forbidden_reads } });

  const record = {
    task: TASK, kind: 'postseal_receipt', stage: 'ps149-01',
    written_at_utc: new Date().toISOString(),
    what: 'the sealed payload, extracted outside the report root and re-answered from its own bytes: the required set '
      + 'resolved against the extraction, the seven consumer roles re-executed with the same harness, and the chain '
      + 're-judged -- none of it reading the tree the packet was built in',
    payload: { path: payloadPath, bytes: payloadBytes.length, sha256: sha(payloadBytes) },
    plan: { path: planPath, members: plan.members.length, ordinal_digest: plan.ordinal_digest },
    receipt: { path: receiptPath, member_count: receipt.member_count, ordinal_digest: receipt.ordinal_digest,
      terminal: receipt.terminal === true },
    extracted: { root: OUT + '/extracted', members: members.length, identical_to_the_payload: identical,
      drifted },
    digests_agree: { plan: planDigest, payload: payloadDigest, receipt: receiptDigest },
    required,
    roles,
    chain,
    no_fallback: noFallback,
    problems,
    problem_count: problems.length,
    ok: problems.length === 0,
    ok_means: 'the payload is the bytes the seal recorded, it extracts to exactly the members the plan named, the required '
      + 'set resolves inside the payload with nothing required and unread, the seven roles hold when executed from the '
      + 'extracted tree rather than from the report root, and no member of the payload points at a live tree',
  };
  const out = process.env.GTOS_POSTSEAL_RECORD || nextRecord('postseal-receipt');
  if (exists(out)) throw new Error('CreateNew refused: ' + out);
  fs.writeFileSync(out, JSON.stringify(record, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ receipt: out, ok: record.ok, members: members.length, identical,
    required_not_read: required.required_not_read, roles_ok: roles.ok, chain_ok: chain.ok,
    no_fallback: noFallback.ok, problems }, null, 2));
  process.exit(record.ok ? 0 : 1);
};

const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isMain) main();
