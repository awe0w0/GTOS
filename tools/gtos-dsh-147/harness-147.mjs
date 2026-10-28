// GTOS-DSH-147 -- the publication harness.
//
// This file is the measurement the packet's final results come from, and it is deliberately the *same* file before and after the
// seal. It is also deliberately self-contained: it imports nothing from the workbench, only Node's own modules and -- at run
// time -- the consumers it finds inside the extracted payload. That is what lets it run outside the workbench at all, from the
// sealed bytes alone, which is the only way a post-seal receipt can be about the seal rather than about the tree that made it.
//
// Verbs:
//   preseal  <stage> <payload> <plan> <receipt> <required-set>       measure a payload and write the pre-seal report
//   postseal <stage> <payload> <plan> <receipt> <required-set>       measure the sealed payload and write the post-seal receipt
//
// The two verbs run the same `measure`. What differs is the record each writes and the fact that the pre-seal one is produced
// before there is any archive to be talking about, so a report that claims the payload was measured cannot have been written
// after the fact.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const exists = (p) => fs.existsSync(p);
const toHost = (p) => (/^\/mnt\/([a-z])\//.test(p) ? p.replace(/^\/mnt\/([a-z])\//, (m, d) => d.toUpperCase() + ':/') : p);
// The two path domains. A record written on the Linux side names `/var/tmp/...`; the same file reached from Windows is
// `//wsl$/Ubuntu-20.04/var/tmp/...`. Both name one file, and a join between a depfile and a plan has to say so explicitly --
// normalising only one of the two directions is what makes such a join look like a missing prerequisite.
const toLinux = (p) => String(p)
  .replace(/^([A-Za-z]):\//, (m, d) => '/mnt/' + d.toLowerCase() + '/')
  .replace(/^\/\/wsl\$\/[^/]+\//i, '/');

// The roots the packet's records were written under. A record inside the payload names the absolute path it was produced at, and
// that name cannot be rewritten without falsifying the record, so it is mapped to the archived member instead. The map is the
// only translation there is; anything it does not cover is a read of the live tree and is reported as one.

// ---------------------------------------------------------------- reading a payload without its writer
// The ustar reader is written from the format. A payload that only its own writer can read has not been shown to be readable,
// so the reader here is a second implementation on purpose.
const readTar = (bytes) => {
  const out = [];
  let at = 0;
  while (at + 512 <= bytes.length) {
    const h = bytes.subarray(at, at + 512);
    if (h.every((b) => b === 0)) break;
    const field = (o, n) => {
      const raw = h.subarray(o, o + n);
      const z = raw.indexOf(0);
      return raw.subarray(0, z < 0 ? n : z).toString('utf8');
    };
    const name = field(0, 100);
    const size = parseInt(field(124, 12).trim() || '0', 8);
    const mode = parseInt(field(100, 8).trim() || '0', 8);
    const prefix = field(345, 155);
    const full = prefix ? prefix + '/' + name : name;
    if (!Number.isFinite(size) || size < 0) throw new Error('a member header states no usable size: ' + full);
    const start = at + 512;
    if (start + size > bytes.length) throw new Error('a member runs past the end of the payload: ' + full);
    const body = bytes.subarray(start, start + size);
    out.push({ name: full, size, mode, bytes: body, sha256: sha(body) });
    at = start + Math.ceil(size / 512) * 512;
  }
  return out;
};

export const measure = async ({ task, payloadPath, planPath, receiptPath, requiredSetPath, root, damagedRoot, verb = 'preseal' }) => {
  const problems = [];
  const payloadBytes = fs.readFileSync(payloadPath);
  const plan = JSON.parse(fs.readFileSync(planPath, 'utf8'));
  const receipt = JSON.parse(fs.readFileSync(receiptPath, 'utf8'));
  const required = JSON.parse(fs.readFileSync(requiredSetPath, 'utf8'));

  // 1. the payload against its own plan and receipt
  const members = readTar(payloadBytes);
  const inPlan = new Map(plan.members.map((m) => [m.path, m]));
  const onlyInPayload = members.map((m) => m.name).filter((n) => !inPlan.has(n));
  const onlyInPlan = [...inPlan.keys()].filter((n) => !members.some((m) => m.name === n));
  if (onlyInPayload.length) problems.push({ why: 'the payload carries members the plan does not name', members: onlyInPayload.slice(0, 8) });
  if (onlyInPlan.length) problems.push({ why: 'the plan names members the payload does not carry', members: onlyInPlan.slice(0, 8) });
  let sameBytes = 0;
  for (const m of members) {
    const want = inPlan.get(m.name);
    if (!want) continue;
    if (m.size !== want.bytes) problems.push({ why: 'a member is a different size than the plan states', member: m.name });
    else if (m.sha256 !== want.sha256) problems.push({ why: 'a member has different bytes at the same size', member: m.name });
    else sameBytes += 1;
  }
  if (receipt.payload.sha256 !== sha(payloadBytes)) problems.push({ why: 'the receipt names a different payload hash' });
  if (receipt.member_count !== members.length) problems.push({ why: 'the receipt names a different member count' });

  // 2. write the members out and hash them back
  const written = [];
  for (const m of members) {
    const dest = root + '/' + m.name;
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, m.bytes, { flag: 'wx' });
    const back = fs.readFileSync(dest);
    if (sha(back) !== m.sha256) problems.push({ why: 'the extracted member is not the member in the payload', member: m.name });
    else written.push({ name: m.name, bytes: back.length, sha256: sha(back), mode: m.mode });
  }
  const extractedNames = written.map((w) => w.name);
  const MAT = root + '/material';
  const materialNames = extractedNames.filter((n) => n.startsWith('material/')).map((n) => n.slice('material/'.length));
  const inMaterial = (rel) => materialNames.includes(rel);

  const materialNamesUnder = (matDir) => {
    const out = [];
    const walk = (dir, prefix) => {
      for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = prefix ? prefix + '/' + e.name : e.name;
        if (e.isDirectory()) walk(dir + '/' + e.name, rel);
        else out.push(rel);
      }
    };
    if (exists(matDir)) walk(matDir, '');
    return out.sort();
  };
  // A prerequisite is resolved through the plan's own record of where each member came from, not through a rule about how a
  // root is spelled. This matters because the payload carries two source views -- the 147 view the objects were built from and
  // the earlier view the reused objects' depfiles still name -- and a suffix rule would find the same relative path under both
  // and be unable to say which. The plan says: this member came from exactly this absolute path. That is a join, not a guess.
  const fromMap = new Map();
  for (const m of plan.members) {
    for (const k of [m.from, toLinux(m.from), toHost(m.from)]) {
      const n = String(k).replace(/\\/g, '/');
      if (!fromMap.has(n)) fromMap.set(n, m.path);
    }
  }
  const depfileClosureOver = (matDir) => {
    const names = materialNamesUnder(matDir);
    const deps = names.filter((n) => n.startsWith('producers/') && n.endsWith('.d'));
    const objs = names.filter((n) => n.startsWith('producers/') && n.endsWith('.o'));
    const out = { objects: objs.length, depfiles: deps.length, edges: 0, resolved: 0, missing_objects: [], problems: [] };
    for (const dep of deps) {
      const text = fs.readFileSync(matDir + '/' + dep, 'utf8').replace(/\\\r?\n/g, ' ');
      const list = [];
      for (const line of text.split(/\r?\n/)) {
        const at = line.indexOf(':');
        if (at < 0) continue;
        for (const tok of line.slice(at + 1).trim().split(/\s+/).filter(Boolean)) list.push(tok);
      }
      const head = text.split(':')[0].trim();
      const objRel = head.slice(head.lastIndexOf('/') + 1);
      if (!objs.includes('producers/' + objRel)) out.missing_objects.push({ depfile: dep, object: objRel });
      for (const tok of list) {
        out.edges += 1;
        const planned = fromMap.get(String(tok).replace(/\\/g, '/'));
        // `names` is relative to the material directory while a plan member is relative to the payload root, so the check that
        // the joined member is actually present is made on the material-relative name.
        const plannedRel = planned && planned.startsWith('material/') ? planned.slice('material/'.length) : planned;
        let hit = plannedRel && names.includes(plannedRel) ? 'material/' + plannedRel : null;
        if (!hit) {
          const wave = (/\/(wave-[0-9]+)\//.exec(tok) || [])[1];
          if (wave) {
            const rel = tok.slice(tok.indexOf('/' + wave + '/') + wave.length + 2);
            const any = names.filter((n) => n.endsWith('/' + wave + '/' + rel));
            if (any.length === 1) hit = any[0];
            else if (any.length > 1) out.ambiguous = (out.ambiguous || []).concat([{ dep: tok, candidates: any.length }]);
          } else {
            const cand = names.find((n) => n.endsWith('/' + tok) || n === tok);
            if (cand) hit = cand;
          }
        }
        if (hit) out.resolved += 1;
        else out.problems.push({ depfile: dep, dep: tok, planned: planned || null });
      }
    }
    const described = new Set(deps.map((d) => d.replace(/\.d$/, '.o')));
    out.objects_without_depfile = objs.filter((o) => !described.has(o));
    out.unexplained_objects = out.objects_without_depfile.filter((o) => !/\.asm\.o$/.test(o));
    out.complete = out.problems.length === 0 && out.missing_objects.length === 0 && out.unexplained_objects.length === 0;
    return out;
  };

  // 3. every required logical resolves inside the payload
  const EXTERNAL = new Set(['runtime_file']);
  const resolved = [];
  const unresolved = [];
  for (const r of required.required_physical_set.members) {
    if (EXTERNAL.has(r.role)) { resolved.push({ role: r.role, name: r.name, how: 'declared external' }); continue; }
    const hit = inMaterial(r.name) ? 'material/' + r.name
      : extractedNames.find((n) => n === r.name || n.endsWith('/' + r.name));
    if (hit) resolved.push({ role: r.role, name: r.name, archived: hit,
      how: hit === 'material/' + r.name ? 'under the material prefix' : 'suffix' });
    else unresolved.push({ role: r.role, name: r.name, why: r.why });
  }
  if (unresolved.length) {
    problems.push({ why: 'the payload does not carry every required logical', count: unresolved.length, first: unresolved.slice(0, 8) });
  }

  // 4. the depfile closure on the extracted tree
  const producerObjs = materialNames.filter((n) => n.startsWith('producers/') && n.endsWith('.o'));
  const depClosure = depfileClosureOver(MAT);
  if (!depClosure.complete) {
    problems.push({ why: 'a prerequisite a depfile names does not resolve inside the payload',
      count: depClosure.problems.length, missing_objects: depClosure.missing_objects.length,
      first: depClosure.problems.slice(0, 6) });
  }

  // 5. the host consumer, on the extracted bytes
  const alias = {};
  for (const m of plan.members) {
    alias[m.from] = root + '/' + m.path;
    const linux = toLinux(m.from);
    if (linux !== m.from) alias[linux] = root + '/' + m.path;
    const host = toHost(m.from);
    if (host !== m.from) alias[host] = root + '/' + m.path;
  }
  const buildRec = materialNames.find((n) => n === 'canonical/host-build-runtime.json');
  const contractRec = materialNames.find((n) => n === 'canonical/host-contract.json');
  if (!buildRec || !contractRec) throw new Error('the payload does not carry the host build record and contract');
  const packageModule = (rel) => {
    const abs = root + '/' + rel;
    if (!exists(abs)) throw new Error('the payload does not carry ' + rel);
    return pathToFileURL(abs).href;
  };
  const consumerURLs = {
    host: packageModule('engine/host-consume-147.mjs'),
    resource: packageModule('engine/references/resource-semantics-147.mjs'),
    package: packageModule('engine/h-package-147.mjs'),
  };
  const rootLinux = toLinux(root);
  const consumeHostChain = (await import(consumerURLs.host)).consumeHostChain;
  const hostArgs = (contractPath) => ({ task, build_record_path: MAT + '/' + buildRec, contract_path: contractPath,
    unit_elf_host_path: MAT + '/host/elf/runtime_test.elf', unit_elf_linux: rootLinux + '/material/host/elf/runtime_test.elf',
    results_dir: MAT + '/host/results', path_aliases: alias });
  const hostVerdict = consumeHostChain(hostArgs(MAT + '/' + contractRec));
  if (hostVerdict.accepted !== true) {
    problems.push({ why: 'the host consumer refused the payload\'s own bytes', problems: (hostVerdict.problems || []).slice(0, 6) });
  }

  // 6. the resource judge, on the extracted channels
  const RJ = await import(consumerURLs.resource);
  const matrixTrials = (matDir) => {
    const p = matDir + '/canonical/matrix.json';
    return exists(p) ? (JSON.parse(fs.readFileSync(p, 'utf8')).trials || []) : [];
  };
  const judgeOver = (matDir) => {
    const names = materialNamesUnder(matDir);
    const trials = matrixTrials(matDir);
    const expected = trials.filter((t) => RJ.RESOURCE_SCENES[t.scene]).map((t) => t.stage);
    const judged = [];
    const problemsHere = [];
    const e9s = names.filter((n) => /^guest\/.*\.e9\.log$/.test(n));
    for (const e9 of e9s) {
      const st = e9.replace(/^guest\//, '').replace(/\.e9\.log$/, '');
      const serial = 'guest/' + st + '.serial.log';
      if (!names.includes(serial)) { problemsHere.push({ why: 'a guest trial carries one channel but not the other', stage: st }); continue; }
      const scene = trials.find((t) => t.stage === st);
      if (!scene || !RJ.RESOURCE_SCENES[scene.scene]) continue;
      const v = RJ.judgeResourceSemantics(scene.scene, {
        e9: fs.readFileSync(matDir + '/' + e9, 'utf8'), serial: fs.readFileSync(matDir + '/' + serial, 'utf8') });
      judged.push({ stage: st, scene: scene.scene, accepted: v.accepted === true, checked: v.counts.checked });
      if (v.accepted !== true) problemsHere.push({ why: 'the resource judge refused the payload\'s own channels', stage: st,
        first_refusal: v.first_refusal });
    }
    const judgedStages = new Set(judged.map((j) => j.stage));
    const notJudged = expected.filter((s) => !judgedStages.has(s));
    for (const s of notJudged) problemsHere.push({ why: 'a resource-scene trial has no channel to judge', stage: s });
    return { judged, problems: problemsHere, channels: e9s.length, expected: expected.length, not_judged: notJudged,
      complete: problemsHere.length === 0 && expected.length > 0 && judged.length === expected.length
        && judged.every((j) => j.accepted) };
  };
  const guestE9 = materialNames.filter((n) => /^guest\/.*\.e9\.log$/.test(n));
  const resourceVerdict = judgeOver(MAT);
  for (const p of resourceVerdict.problems) problems.push(p);

  // 7. the seven mandatory roles, decided inside the package
  const HP = await import(consumerURLs.package);
  const packageAudit = await HP.auditPackage({ root, plan, externals: [], trace: true });
  if (packageAudit.ok !== true) {
    problems.push({ why: 'the package could not decide every mandatory role from its own modules',
      count: packageAudit.problems.length, first: packageAudit.problems.slice(0, 6) });
  }
  if (packageAudit.trace && packageAudit.trace.live > 0) {
    problems.push({ why: 'a packaged consumer read outside the package', first: packageAudit.trace.live_paths });
  }
  const urlsOutside = Object.entries(consumerURLs).filter(([, u]) => !u.startsWith(pathToFileURL(root + '/').href));
  if (urlsOutside.length) problems.push({ why: 'a consumer was imported from outside the extracted tree', which: urlsOutside });

  // 9. the pre-seal reports, when this is the post-seal measurement
  // A report cannot be a member of the payload it describes -- it would have to exist before the payload did -- so the
  // publication role is decided by the harness itself and the reports are carried as evidence. Carrying them is not enough:
  // what makes a report a *pre*-seal report is that it measured a different payload, the rehearsal, which is a fact this
  // measurement can check and a report written afterwards could not fake without also having measured that other payload.
  //
  // Every carried report is listed rather than the first one, because the rehearsal converged over several attempts and the
  // failed ones are kept: deleting a failed record to tidy a payload is the one thing this packet does not do. What is
  // required is that at least one of them passed and that none of them measured this payload.
  let presealReport = null;
  if (verb === 'postseal') {
    const names = extractedNames.filter((n) => /^records\/preseal-report-.*-147\.json$/.test(n)).sort();
    const carried = names.map((n) => {
      const r = JSON.parse(fs.readFileSync(root + '/' + n, 'utf8'));
      return { member: n, bytes: fs.statSync(root + '/' + n).size, kind: r.kind, harness: r.harness,
        ok: r.ok === true, measured_payload_sha256: r.payload && r.payload.sha256,
        measured_this_payload: !!(r.payload && r.payload.sha256 === sha(payloadBytes)) };
    });
    presealReport = { carried: carried.length, passed: carried.filter((c) => c.ok).length, reports: carried };
    if (names.length === 0) problems.push({ why: 'the sealed payload does not carry a pre-seal report' });
    for (const c of carried) {
      if (c.kind !== 'harness_preseal') problems.push({ why: 'a carried report is not a pre-seal report', member: c.member, kind: c.kind });
      if (c.harness !== 'harness-147.mjs') problems.push({ why: 'a carried report was not written by this harness', member: c.member });
      if (c.measured_this_payload) {
        problems.push({ why: 'a carried report measured this payload, so it was written after the seal rather than before it',
          member: c.member });
      }
    }
    if (carried.length && carried.filter((c) => c.ok).length === 0) {
      problems.push({ why: 'no carried pre-seal report passed, so nothing shows the harness worked before the seal' });
    }
  }

  // 8. three single-member removals, each aimed at a consumer that has to notice
  // The copy is made by this file rather than by `fs.cpSync`, because the extraction may sit on a UNC share and a copy whose
  // completeness is assumed is exactly the kind of scaffolding a negative would then fail for the wrong reason. Every member
  // written is read back and compared, so the damaged tree is known to differ from the good one in one place only.
  const copyTree = (from, to) => {
    fs.mkdirSync(to, { recursive: true });
    let n = 0;
    for (const e of fs.readdirSync(from, { withFileTypes: true })) {
      const a = from + '/' + e.name;
      const b = to + '/' + e.name;
      if (e.isDirectory()) { n += copyTree(a, b); continue; }
      fs.copyFileSync(a, b);
      if (sha(fs.readFileSync(b)) !== sha(fs.readFileSync(a))) throw new Error('the damaged copy is not a copy: ' + b);
      n += 1;
    }
    return n;
  };
  const copied = copyTree(root, damagedRoot);
  if (copied !== written.length) {
    problems.push({ why: 'the damaged copy does not hold every member of the good one', copied, expected: written.length });
  }
  const negatives = [];
  const dropObject = producerObjs[0];
  if (dropObject) {
    fs.unlinkSync(damagedRoot + '/material/' + dropObject);
    const after = depfileClosureOver(damagedRoot + '/material');
    negatives.push({ what: 'a producer object', removed: dropObject, consumer: 'depfile closure',
      refused: after.complete === false,
      named: after.missing_objects.some((m) => dropObject.endsWith('/' + m.object))
        || after.problems.some((p) => p.dep.endsWith(dropObject)),
      detail: { missing_objects: after.missing_objects.length, unresolved: after.problems.length } });
  }
  const resourceStages = new Set(matrixTrials(MAT).filter((t) => RJ.RESOURCE_SCENES[t.scene]).map((t) => t.stage));
  const dropChannel = guestE9.find((n) => resourceStages.has(n.replace(/^guest\//, '').replace(/\.e9\.log$/, '')));
  if (dropChannel) {
    fs.unlinkSync(damagedRoot + '/material/' + dropChannel);
    const after = judgeOver(damagedRoot + '/material');
    const st = dropChannel.replace(/^guest\//, '').replace(/\.e9\.log$/, '');
    negatives.push({ what: 'a guest channel', removed: dropChannel, consumer: 'resource judge',
      refused: after.complete === false, named: after.problems.some((p) => p.stage === st),
      detail: { trials_judged: after.judged.length, problems: after.problems.length } });
  }
  const damagedContract = damagedRoot + '/material/canonical/host-contract.json';
  if (exists(damagedContract)) {
    fs.unlinkSync(damagedContract);
    const v = consumeHostChain(hostArgs(damagedContract));
    negatives.push({ what: 'the contract the host consumer is given', removed: 'canonical/host-contract.json',
      consumer: 'host consumer', refused: v.accepted !== true,
      named: (v.problems || []).some((p) => /contract/i.test(JSON.stringify(p))),
      detail: { problems: (v.problems || []).length } });
  }

  // 8b. the two things a payload can carry that are not evidence but identity: the runner it executed and the context it
  // answers to. Both are checked on the good tree and then aimed at on the damaged one, because "the payload names the runner"
  // and "a payload without the runner is refused" are different claims. The check is a function of the tree, so the damaged
  // copy is judged by the same code rather than by a restatement of it.
  const identityOf = (dir) => {
    const names = materialNamesUnder(dir + '/material');
    void names;
    const all = [];
    const walk = (d, prefix) => {
      for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const rel = prefix ? prefix + '/' + e.name : e.name;
        if (e.isDirectory()) walk(d + '/' + e.name, rel);
        else all.push(rel);
      }
    };
    walk(dir, '');
    const out = { runner: all.find((n) => n === 'engine/runner/run-147-r01.mjs') || null,
      context: all.find((n) => n === 'control/contract-147.json') || null, problems: [] };
    if (!out.runner) out.problems.push('the payload does not carry the pinned runner');
    if (!out.context) out.problems.push('the payload does not carry the packet contract it answers to');
    if (out.context) {
      const ctx = JSON.parse(fs.readFileSync(dir + '/' + out.context, 'utf8'));
      out.task = ctx.task || null;
      if (ctx.task !== task) out.problems.push('the carried contract is for another packet');
    }
    return out;
  };
  const identity = identityOf(root);
  for (const p of identity.problems) problems.push({ why: p, at: 'the good tree' });

  const damagedRunner = damagedRoot + '/' + (identity.runner || 'engine/runner/run-147-r01.mjs');
  if (exists(damagedRunner)) {
    fs.unlinkSync(damagedRunner);
    const after = identityOf(damagedRoot);
    negatives.push({ what: 'the pinned runner', removed: identity.runner, consumer: 'package identity',
      refused: after.problems.includes('the payload does not carry the pinned runner'),
      named: after.runner === null, detail: { problems: after.problems } });
  }
  const damagedContext = damagedRoot + '/' + (identity.context || 'control/contract-147.json');
  if (exists(damagedContext)) {
    // The context is swapped for another packet's rather than removed: a payload whose contract names a different packet is
    // the "context swap" a reader has to be able to refuse, and removal alone would only prove that absence is noticed.
    const original = JSON.parse(fs.readFileSync(damagedContext, 'utf8'));
    fs.writeFileSync(damagedContext, JSON.stringify({ ...original, task: 'GTOS-DSH-146' }, null, 2) + '\n', 'utf8');
    const after = identityOf(damagedRoot);
    negatives.push({ what: 'the packet contract', removed: 'control/contract-147.json (swapped for another packet)',
      consumer: 'package identity', refused: after.problems.includes('the carried contract is for another packet'),
      named: after.task === 'GTOS-DSH-146', detail: { carried_task: after.task, expected: task } });
  }

  // Every path a consumer was handed has to be inside the extracted tree: that is the measurement of "no fallback".
  const aliasValues = Object.values(alias);
  const outside = aliasValues.filter((v) => !v.startsWith(root + '/'));
  if (outside.length) problems.push({ why: 'an alias points outside the extracted tree', count: outside.length, first: outside.slice(0, 6) });
  const readsOutside = (hostVerdict.path_aliases && hostVerdict.path_aliases.unresolved_required_reads) || 0;
  if (readsOutside > 0) problems.push({ why: 'the host consumer asked for a path the payload does not carry', count: readsOutside });

  const ok = problems.length === 0 && written.length === members.length && sameBytes === members.length
    && unresolved.length === 0 && hostVerdict.accepted === true && resourceVerdict.complete
    && packageAudit.ok === true && negatives.length === 5 && negatives.every((n) => n.refused && n.named);

  return {
    task, harness: 'harness-147.mjs',
    payload: { path: payloadPath, bytes: payloadBytes.length, sha256: sha(payloadBytes) },
    plan: { path: planPath, members: plan.members.length, ordinal_digest: plan.ordinal_digest },
    receipt: { path: receiptPath, member_count: receipt.member_count, ordinal_digest: receipt.ordinal_digest },
    extracted: { root: rootLinux, members: written.length, identical_to_payload: sameBytes },
    required: { total: required.required_physical_set.total, resolved: resolved.length, unresolved: unresolved.length,
      external: resolved.filter((r) => r.how === 'declared external').length, first_unresolved: unresolved.slice(0, 8) },
    depfile_closure: { objects: depClosure.objects, depfiles: depClosure.depfiles, edges: depClosure.edges,
      resolved: depClosure.resolved, complete: depClosure.complete, problems: depClosure.problems.length,
      missing_objects: depClosure.missing_objects.length, unexplained_objects: depClosure.unexplained_objects.length },
    host_consumer: { accepted: hostVerdict.accepted === true, counts: hostVerdict.counts,
      aliased_reads: hostVerdict.path_aliases && hostVerdict.path_aliases.aliased_reads, unresolved_reads: readsOutside },
    resource_judge: { trials: resourceVerdict.judged.length, accepted: resourceVerdict.judged.filter((j) => j.accepted).length,
      obligations: resourceVerdict.judged.reduce((n, j) => n + j.checked, 0), channels: resourceVerdict.channels },
    package_roles: { roles: packageAudit.roles.map((r) => ({ role: r.role, ok: r.ok,
      modules: r.modules.filter((m) => m.present).length, tools: r.tools.filter((t) => t.present).length,
      needs: r.needs.filter((n) => n.present).length })), all_ok: packageAudit.ok, trace: packageAudit.trace },
    consumer_urls: consumerURLs,
    negatives,
    preseal_report: presealReport,
    no_fallback: { alias_entries: aliasValues.length, values_outside_the_tree: outside.length,
      live_roots_read: packageAudit.trace ? packageAudit.trace.live_paths : [] },
    problems, problem_count: problems.length, ok,
    ok_means: 'the payload was read by a reader that is not its writer, every member was written out and hashed back, every '
      + 'required logical the packet may not treat as external was found inside it, the consumers were imported from the '
      + 'extracted tree and given only paths inside it, all seven mandatory roles were decided by modules the package '
      + 'carries, every filesystem call those modules made landed in the package, and five aimed defects -- a removed '
      + 'producer object, a removed guest channel, a removed host contract, a removed runner and a swapped packet contract -- '
      + 'were each refused by the consumer aimed at them',
  };
};

// ---------------------------------------------------------------- CLI
const SELF_PATH = fileURLToPath(import.meta.url);
const isMain = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH);
if (isMain) {
  const verb = process.argv[2];
  const stage = process.argv[3];
  const [payloadPath, planPath, receiptPath, requiredSetPath] = process.argv.slice(4, 8);
  const outPath = process.argv[8];
  if (!verb || !stage || !payloadPath || !planPath || !receiptPath || !requiredSetPath || !outPath) {
    console.error('usage: harness-147.mjs preseal|postseal <stage> <payload> <plan> <receipt> <required-set> <out.json>');
    process.exit(2);
  }
  if (!['preseal', 'postseal'].includes(verb)) { console.error('unknown verb: ' + verb); process.exit(2); }
  if (!/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage)) { console.error('bad stage: ' + stage); process.exit(2); }
  if (exists(outPath)) { console.error(JSON.stringify({ refused: true, why: 'the output already exists', path: outPath })); process.exit(1); }
  for (const p of [payloadPath, planPath, receiptPath, requiredSetPath]) {
    if (!exists(p)) { console.error(JSON.stringify({ refused: true, why: 'absent', path: p })); process.exit(1); }
  }
  // The two trees are named after the verb, so a pre-seal measurement and a post-seal measurement cannot overwrite each other and
  // a post-seal run cannot quietly reuse the tree a pre-seal run extracted. They are placed beside the payload rather than beside
  // the report, because a report root is a record root and an eighteen-megabyte extraction is not a record.
  const base = path.dirname(payloadPath) + '/harness-' + verb + '-' + stage;
  const root = base + '/extracted';
  const damagedRoot = base + '/damaged';
  for (const d of [root, damagedRoot]) {
    if (exists(d)) { console.error(JSON.stringify({ refused: true, why: 'the extraction root already exists', path: d })); process.exit(1); }
  }
  fs.mkdirSync(root, { recursive: true });
  const verdict = await measure({ task: 'GTOS-DSH-147', payloadPath, planPath, receiptPath, requiredSetPath, root, damagedRoot, verb });
  const record = { kind: 'harness_' + verb, stage, written_at_utc: new Date().toISOString(), ...verdict };
  fs.writeFileSync(outPath, JSON.stringify(record, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
  console.log(JSON.stringify({ verb, stage, ok: record.ok, problems: record.problem_count, extracted: record.extracted,
    required: record.required, host: record.host_consumer.accepted, resource: record.resource_judge,
    roles: record.package_roles.all_ok, negatives: record.negatives.map((n) => ({ n: n.what, refused: n.refused, named: n.named })),
    record: { path: outPath, bytes: fs.statSync(outPath).size, sha256: sha(fs.readFileSync(outPath)) } }, null, 2));
  process.exit(record.ok ? 0 : 1);
}
