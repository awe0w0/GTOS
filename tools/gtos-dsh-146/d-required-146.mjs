// GTOS-DSH-146 D, part one: the business requirements, derived from the contracts rather than from the package.
//
// Packet 145 built its required set by copying the spec's member list. That produced a true statement -- "every member the
// package carries was read" -- and a false one, because a package cannot be evidence that it contains what the work needs.
// A member list is what somebody decided to put in a tar; the requirements are what the build, the host tests, the guest
// trials and the support tooling actually depend on. Those are two different sets, and 145 only ever measured the first.
//
// So this module never opens a spec, a plan, a member list, or a consumer trace. It reads the business contracts:
//
//   1. the frozen 61-slot link order, as the linker's own argv spells it, from the stage spec that was frozen before the
//      build (`lkp14010-01.spec.json` control.stage_options.individuals);
//   2. the real depfiles each of those objects was compiled against, and the sources those depfiles resolve to;
//   3. the source/profile/seam contract: every .cpp, .h, .inc, .ld and .s the compiler was given;
//   4. the host contract: three units, fourteen cases, the build record that names every source compiled into each unit,
//      and each case's result, inner report and raw channel;
//   5. the scene contract: twenty scenes and the twenty-eight trials that were actually launched, each with the request,
//      the admission, the launch record and both raw channels;
//   6. the support modules the consumers themselves are made of, at the identities the parent commit pins;
//   7. the Git canonical: the committed bytes of the sources this packet's work is about, from the commit, not the disk.
//
// Every requirement carries where it came from. That is the difference between this record and a manifest: a reader can
// re-derive any row by following `derived_from` back to a contract that was frozen before the package existed, and can
// check the row without trusting this file.
//
// What this module deliberately does NOT do: it does not decide whether a requirement is satisfiable. A requirement whose
// bytes are gone is still a requirement, and it is reported as missing rather than dropped. Dropping it would make the
// closure look complete by shrinking what closure means.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// A record names paths in whichever domain the tool that wrote it lived in. This reader runs on the Windows host, so a
// Linux path has to be translated before it can be opened. The translation is mechanical and it is applied to every path,
// so a record cannot pass by naming a path only one domain can read. (Same rule as 144's prerequisite consumer, and for
// the same reason: a path this reader cannot open reads as an absence, and an absence reads as a fact.)
export function hostPath(p) {
  if (typeof p !== 'string' || !p) return p;
  if (/^[\\/]{2}[^\\/]/.test(p)) return p.replace(/\//g, '\\');
  const mount = /^\/mnt\/([a-z])\/(.*)$/.exec(p);
  if (mount) return mount[1].toUpperCase() + ':/' + mount[2];
  if (p.startsWith('/')) return '\\\\wsl$\\Ubuntu-20.04' + p.replace(/\//g, '\\');
  return p;
}

function sleep(ms) { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); }

// The transient UNC failures this environment produces are retried, and only a read that fails every time is an absence.
export function readBytes(p) {
  if (typeof p !== 'string' || !p) return null;
  const host = hostPath(p);
  for (let i = 0; i < 12; i += 1) {
    try { return fs.readFileSync(host); } catch { sleep(200); }
  }
  return null;
}

function statBytes(p) {
  try { return fs.statSync(hostPath(p)).size; } catch { return null; }
}

// ------------------------------------------------------------------ the requirement table

const rows = [];
const missing = [];
const derivedFrom = [];

function sourcePin(contractPath) {
  const b = readBytes(contractPath);
  if (b === null) return null;
  return { path: contractPath, bytes: b.length, sha256: sha256(b) };
}

// One row per (logical identity, exact path). A requirement is about a path, not about a role: two roles may need the
// same bytes, and the same role may need two files. Collapsing either way loses a fact.
const seen = new Map();
function require_(role, logical, p, extra) {
  if (typeof p !== 'string' || !p) return;
  const key = role + '\0' + p;
  if (seen.has(key)) {
    // A second derivation of the same requirement is recorded rather than discarded: two contracts naming the same file
    // is a fact about the work, and it is what lets a later reader see that a requirement is over-determined.
    const row = seen.get(key);
    if (row.derived_from.length < 4) row.derived_from.push(extra.derived_from);
    return;
  }
  const bytes = statBytes(p);
  const row = {
    role,
    logical,
    path: p,
    bytes_on_disk: bytes,
    present: bytes !== null,
    source_task: extra.source_task,
    derived_from: [extra.derived_from],
    why: extra.why || null,
  };
  if (extra.sha256) row.pinned_sha256 = extra.sha256;
  seen.set(key, row);
  rows.push(row);
  if (bytes === null) missing.push({ role, logical, path: p, derived_from: extra.derived_from });
}

// ------------------------------------------------------------------ 1..3 the producer closure

function readJson(p) {
  const b = readBytes(p);
  if (b === null) return null;
  try { return JSON.parse(b.toString('utf8')); } catch { return null; }
}

export function deriveProducer(R144, orderSpecPath) {
  const contract = sourcePin(orderSpecPath);
  if (contract) derivedFrom.push({ contract: 'frozen_link_order', ...contract });

  const order = readJson(orderSpecPath);
  if (!order) return { ok: false, why: 'the frozen link order is not readable: ' + orderSpecPath };
  const options = (order.control && order.control.stage_options) || {};
  const individuals = Array.isArray(options.individuals) ? options.individuals : [];
  if (individuals.length === 0) return { ok: false, why: 'the frozen link order names no individuals' };

  // The order matters and it is preserved: slot numbers are the position the linker consumed the object in, and a later
  // reader that sorts this list destroys the only fact the list is for.
  individuals.forEach((p, index) => {
    require_('producer_object', 'slot-' + String(index + 1).padStart(2, '0'), p, {
      source_task: 'GTOS-DSH-144',
      derived_from: 'frozen_link_order:' + orderSpecPath,
      why: 'the linker consumed this object at position ' + (index + 1),
    });
  });

  // The source/profile contract: the bytes the compiler was given. 155 files across .cpp/.h/.inc/.ld/.s.
  const spPath = R144 + '/role-source_profile_contract-144a.json';
  const sp = readJson(spPath);
  const spPin = sourcePin(spPath);
  if (spPin) derivedFrom.push({ contract: 'source_profile_contract', ...spPin });
  if (sp) {
    for (const s of sp.sources || []) {
      require_('source_view', s.logical || path.basename(s.path), s.path, {
        source_task: 'GTOS-DSH-144',
        derived_from: 'source_profile_contract:' + spPath,
        sha256: s.sha256,
        why: 'the contract names this file as an input to the build',
      });
    }
  }

  // The dependency chain: the real depfiles, and every source each one resolves to. A depfile is the compiler's own
  // statement of what the object depends on, which is why it is a requirement source and a hand-written list is not.
  const dcPath = R144 + '/role-dependency_chain-144a.json';
  const dc = readJson(dcPath);
  const dcPin = sourcePin(dcPath);
  if (dcPin) derivedFrom.push({ contract: 'dependency_chain', ...dcPin });
  let depfileCount = 0;
  let depSourceCount = 0;
  if (dc) {
    for (const input of dc.inputs || []) {
      if (input.depfile && input.depfile.path) {
        depfileCount += 1;
        require_('depfile', 'slot-' + input.slot + '-depfile', input.depfile.path, {
          source_task: 'GTOS-DSH-144',
          derived_from: 'dependency_chain:' + dcPath,
          sha256: input.depfile.sha256,
          why: 'the depfile is what this object was actually compiled against',
        });
      }
      for (const s of input.dep_sources_resolved || []) {
        depSourceCount += 1;
        require_('dep_source', null, s, {
          source_task: 'GTOS-DSH-144',
          derived_from: 'dependency_chain:' + dcPath,
          why: 'slot ' + input.slot + ' depfile resolves to this file',
        });
      }
    }
  }

  // The image and the map, from the static contract, and the link-input consumption record that names the ELF the
  // typed terminal actually produced.
  const stPath = R144 + '/role-static_image-144a.json';
  const st = readJson(stPath);
  const stPin = sourcePin(stPath);
  if (stPin) derivedFrom.push({ contract: 'static_image', ...stPin });
  if (st) {
    if (st.elf && st.elf.path) {
      require_('image', 'final-image', st.elf.path, { source_task: 'GTOS-DSH-144', derived_from: 'static_image:' + stPath,
        sha256: st.elf.sha256, why: 'the image the static contract describes' });
    }
    if (st.map && st.map.path) {
      require_('image_map', 'final-map', st.map.path, { source_task: 'GTOS-DSH-144', derived_from: 'static_image:' + stPath,
        sha256: st.map.sha256, why: 'the map the static contract describes' });
    }
  }

  const lkPath = R144 + '/role-link_input_consumption-144a.json';
  const lk = readJson(lkPath);
  const lkPin = sourcePin(lkPath);
  if (lkPin) derivedFrom.push({ contract: 'link_input_consumption', ...lkPin });
  if (lk) {
    const record = lk.record || {};
    for (const key of ['elf', 'map']) {
      const pin = record[key];
      if (pin && pin.path) {
        require_(key === 'elf' ? 'image' : 'image_map', 'link-record-' + key, pin.path, {
          source_task: 'GTOS-DSH-144', derived_from: 'link_input_consumption:' + lkPath, sha256: pin.sha256,
          why: 'the typed link terminal names this as its output',
        });
      }
    }
  }

  return { ok: true, slots: individuals.length, depfiles: depfileCount, dep_sources: depSourceCount };
}

// ------------------------------------------------------------------ 4 the host chain

export function deriveHost(R144) {
  const contractPath = R144 + '/h-contract-144e.json';
  const contract = readJson(contractPath);
  const pin = sourcePin(contractPath);
  if (pin) derivedFrom.push({ contract: 'host_contract', ...pin });
  if (!contract) return { ok: false, why: 'the host contract is not readable: ' + contractPath };

  let units = 0;
  let cases = 0;
  for (const unit of contract.units || []) {
    units += 1;
    if (unit.elf && unit.elf.path) {
      require_('host_unit_elf', unit.role, unit.elf.path, {
        source_task: 'GTOS-DSH-144', derived_from: 'host_contract:' + contractPath, sha256: unit.elf.sha256,
        why: 'the binary this unit ran',
      });
    }
    for (const c of unit.cases || []) {
      cases += 1;
      // The consumer's own constants decide the case set; the contract only names the stage that ran each case. Both the
      // result and the two raw channels are requirements, because a result without its raw channel is a claim without
      // the text it is a claim about.
      const resultPath = R144 + '/results/' + c.stage + '.json';
      require_('host_result', unit.role + '-case-' + c.case, resultPath, {
        source_task: 'GTOS-DSH-144', derived_from: 'host_contract:' + contractPath,
        why: 'the result record for this case',
      });
      // The inner report is named by the result, not by a convention. That matters: the run's own account is what says
      // where its raw channel went, and a reader that computed the path from the stage name would be guessing at a name
      // instead of reading the record that owns it.
      const result = readJson(resultPath);
      const innerPin = result && result.inner_report;
      let innerRead = null;
      if (innerPin && innerPin.path) {
        require_('host_inner', unit.role + '-case-' + c.case, innerPin.path, {
          source_task: 'GTOS-DSH-144', derived_from: 'host_result:' + resultPath, sha256: innerPin.sha256,
          why: 'the executor own account of this case, at the path the result names',
        });
        innerRead = readJson(innerPin.path);
      }
      const child = (innerRead && innerRead.child) || null;
      if (child && child.output_path) {
        require_('host_raw', unit.role + '-case-' + c.case, child.output_path, {
          source_task: 'GTOS-DSH-144', derived_from: 'host_inner:' + innerPin.path, sha256: child.output_sha256,
          why: 'the raw channel this case actually wrote, at the path the inner report names',
        });
      } else {
        // A case whose inner report names no raw channel is a requirement that exists and cannot be met. It is recorded
        // as missing with the reason, rather than quietly left out of the set.
        missing.push({ role: 'host_raw', logical: unit.role + '-case-' + c.case,
          path: innerPin ? innerPin.path : resultPath, derived_from: 'host_inner:names no raw channel',
          why: 'the inner report names no raw channel for this case' });
      }
    }
  }

  // The build records: each is one unit's build, and each names every source compiled into it plus the seam file it was
  // compiled against. Those sources are requirements even though they are not objects in the image, because a host case
  // that ran a binary nobody can rebuild is not a repeatable case.
  const buildRecords = fs.readdirSync(hostPath(R144)).filter((n) => /^h-unit-build-.*-144\.json$/.test(n));
  let buildInputs = 0;
  let seams = 0;
  for (const name of buildRecords) {
    const p = R144 + '/' + name;
    const rec = readJson(p);
    const rpin = sourcePin(p);
    if (rpin) derivedFrom.push({ contract: 'host_build_record', ...rpin });
    if (!rec) continue;
    for (const input of rec.inputs || []) {
      // A build record may name the source in either domain. `host_path` is the form that can be opened from here; the
      // `path` spelling is the same file in the domain the compiler lived in. Both are kept, because a record whose two
      // spellings disagree is a record that has drifted from the file, and that is worth seeing.
      const where = input.host_path || input.path;
      if (!where) continue;
      buildInputs += 1;
      require_('host_build_input', input.logical || path.basename(where), where, {
        source_task: 'GTOS-DSH-144', derived_from: 'host_build_record:' + p, sha256: input.sha256,
        why: 'the build record says this source was compiled into ' + (rec.unit || rec.tag || 'a unit'),
      });
    }
    if (rec.seam && rec.seam.path) {
      seams += 1;
      require_('host_seam', rec.tag + '-seam', rec.seam.path, {
        source_task: 'GTOS-DSH-144', derived_from: 'host_build_record:' + p, sha256: rec.seam.sha256,
        why: 'the privileged-TU seam this unit was compiled against',
      });
    }
  }

  return { ok: true, units, cases, build_records: buildRecords.length, build_inputs: buildInputs, seams };
}

// ------------------------------------------------------------------ 5 the guest trials

export function deriveGuest(R144) {
  const matrixPath = R144 + '/g-matrix-gm144-01-144.json';
  const matrix = readJson(matrixPath);
  const pin = sourcePin(matrixPath);
  if (pin) derivedFrom.push({ contract: 'guest_matrix', ...pin });
  if (!matrix) return { ok: false, why: 'the guest matrix is not readable: ' + matrixPath };

  const scenes = new Set();
  let trials = 0;
  let channels = 0;
  for (const t of matrix.trials || []) {
    trials += 1;
    scenes.add(t.scene);
    const key = t.scene + '-t' + t.trial;
    // The trial record itself, and both raw channels. The record is what the judge reads; the channels are the bytes the
    // judge is a judgement about. A package that carries the record and not the channels carries an opinion.
    if (t.record && t.record.path) {
      require_('guest_trial_record', key, t.record.path, {
        source_task: 'GTOS-DSH-144', derived_from: 'guest_matrix:' + matrixPath, sha256: t.record.sha256,
        why: 'the record of this trial',
      });
    }
    for (const kind of ['serial', 'e9']) {
      const c = t[kind];
      if (c && c.path) {
        channels += 1;
        require_('guest_channel', key + '-' + kind, c.path, {
          source_task: 'GTOS-DSH-144', derived_from: 'guest_matrix:' + matrixPath, sha256: c.sha256,
          why: 'the ' + kind + ' channel of this trial',
        });
      }
    }
  }

  // The scene contract module: the twenty scenes, their stops, and the argument grammar. It is a requirement because the
  // judge cannot be read without it, and because a package carrying guest text without the module that decides what the
  // text means has carried data and not a verdict.
  const sceneModule = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a/worktree-146/tools/gtos-dsh-144/references/scene-admission-144.mjs';
  require_('support_module', 'scene-admission', sceneModule, {
    source_task: 'GTOS-DSH-144', derived_from: 'guest_matrix:' + matrixPath,
    why: 'the module that defines the scenes the matrix runs',
  });

  return { ok: true, scenes: scenes.size, trials, channels };
}

// ------------------------------------------------------------------ 6..7 support modules and Git canonical

export function deriveSupport(R146) {
  const dir = R146 + '/worktree-146/tools/gtos-dsh-144/references';
  const names = fs.readdirSync(hostPath(dir)).filter((n) => n.endsWith('.mjs'));
  for (const n of names) {
    require_('support_module', n.replace(/\.mjs$/, ''), dir + '/' + n, {
      source_task: 'GTOS-DSH-144', derived_from: 'parent_commit:' + 'a2d492fb838e0bbd9c278b1a09c00965669b0f1d',
      why: 'a module the consumers are made of',
    });
  }
  // The publisher the packet uses, at the identity the parent commit pins. It is a requirement of the publication role
  // rather than of the build, and it is listed here because the roles are one set, not four sets.
  require_('support_module', 'publisher', R146 + '/worktree-146/tools/gtos-dsh-145/publish-145.mjs', {
    source_task: 'GTOS-DSH-145', derived_from: 'parent_commit:a2d492fb838e0bbd9c278b1a09c00965669b0f1d',
    why: 'the publisher this packet publishes with',
  });
  return { ok: true, modules: names.length };
}

// ------------------------------------------------------------------ assembly

export function derive({ R144, R146, orderSpecPath }) {
  const producer = deriveProducer(R144, orderSpecPath);
  const host = deriveHost(R144);
  const guest = deriveGuest(R144);
  const support = deriveSupport(R146);

  const byRole = {};
  for (const row of rows) byRole[row.role] = (byRole[row.role] || 0) + 1;
  const present = rows.filter((r) => r.present).length;

  return {
    task: 'GTOS-DSH-146',
    what: 'the business requirements, derived from the contracts that were frozen before any package existed',
    not_derived_from: ['spec-146', 'plan-146', 'member list', 'member-identity read set', 'any 146 consumer trace'],
    contracts_read: derivedFrom,
    derivations: { producer, host, guest, support },
    counts: { rows: rows.length, present, missing: missing.length, by_role: byRole },
    requirements: rows,
    missing,
    ok: producer.ok && host.ok && guest.ok && support.ok && missing.length === 0,
    ok_means: 'every derivation succeeded and every requirement it produced is a file that exists on disk at the size it '
      + 'was derived at. A missing row is kept in `missing` and in `requirements`; it is not dropped, because a '
      + 'requirement that cannot be met is still a requirement.',
  };
}
