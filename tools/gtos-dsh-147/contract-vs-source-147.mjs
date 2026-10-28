// The contract against the source.
//
// A scene contract that names a line the kernel cannot print is worse than no contract: every run fails for a reason that
// has nothing to do with the machine. And a contract that names a line printed *after* the scene's own stop would pass
// while the scene under test did nothing. So the two are cross-checked statically, before any image is built.
//
// The check cannot be a text search, because the kernel does not contain most of its own output as whole strings:
//
//   printf("BOOT "); printf(phase); printf(" "); printf(description);      -- "BOOT B01 HANDOFF"
//   printf("PANIC "); printf(code); printf(" PHASE "); printf(bootPhase);  -- "PANIC E99 PHASE B02 ..."
//   LogValue("CPU DETECTED ", cpu.DetectedLogicalProcessors());            -- "CPU DETECTED 00000001"
//   BootDiagnosticHalt(common::BootDiagnostic::Marker(StopRuntimeInit));   -- "BOOTDIAG RUNTIME INITIALIZED"
//
// so this module reconstructs the ordered list of lines the kernel is able to emit, by walking the print statements and
// tracking which boot stage is current. It then asks two questions:
//
//   * can the kernel emit this line at all (an obligation nobody prints, or a forbidden line nobody can print, is vacuous);
//   * is it emitted before this scene's own checkpoint (a line from a later phase is not this scene's observation).
//
// The kernel is a straight line of phases, each ending in `if (stop == X) BootDiagnosticHalt(Marker(X))`, so a scene that
// selects X observes exactly the prefix of that sequence up to X's halt -- not a brace-delimited "block", which would
// misattribute every line of the shared boot preamble.
//
// This is a static agreement check between two files. It is not evidence about a running machine and never claims to be.

import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { SCENES, COLD_REPEAT_SCENES, REPEATED_SCENES, EXPECTED_SCENE_COUNT, EXPECTED_TRIAL_COUNT } from './scene-contract-147.mjs';

const ROOT = path.resolve(import.meta.dirname, '../..');
const KERNEL = path.join(ROOT, 'src/kernel.cpp');
const DIAGNOSTIC = path.join(ROOT, 'src/common/boot_diagnostic.cpp');

const decode = (raw) => raw.replace(/\\(n|t|r|0|\\|"|')/g, (_, e) => ({ n: '\n', t: '\t', r: '\r', 0: '\0', '\\': '\\', '"': '"', "'": "'" }[e]));
const STRING = '"((?:[^"\\\\\\n]|\\\\.)*)"';

// Every call the kernel can print from, in source order, with the lines each one produces.
const emissions = (kernel, diagnostic) => {
  // the diagnostic markers, resolved through Marker()
  const markers = new Map();
  for (const m of diagnostic.matchAll(/case\s+Stop(\w+)\s*:\s*return\s+"([^"]*)"/g)) markers.set('Stop' + m[1], decode(m[2]));
  // the reject reasons, resolved through RejectName()
  const rejects = [];
  for (const m of diagnostic.matchAll(/return\s+"(empty-value|unknown-value|duplicate|unterminated|oversize-span|none)"/g)) rejects.push(m[1]);

  const calls = [];
  const push = (at, text, kind) => calls.push({ at, text, kind });

  // BootStage("B01", "HANDOFF") -- also the current phase for any Panic that follows.
  // The description may be a conditional (`liveBoot ? "RAM STORAGE" : "STORAGE"`), which is two possible lines rather than
  // one, so every literal in the description position is emitted.
  const stages = [];
  for (const m of kernel.matchAll(/BootStage\s*\(\s*"((?:[^"\\\n]|\\.)*)"\s*,\s*([^;]*?)\)\s*;/g)) {
    const phase = decode(m[1]);
    stages.push({ at: m.index, phase });
    for (const lit of m[2].matchAll(new RegExp(STRING, 'g'))) push(m.index, 'BOOT ' + phase + ' ' + decode(lit[1]), 'line');
  }
  const phaseAt = (index) => {
    let cur = '';
    for (const s of stages) if (s.at < index) cur = s.phase;
    return cur;
  };
  // Panic(message, code) -- printed as "PANIC " code " PHASE " bootPhase " " message
  for (const m of kernel.matchAll(new RegExp('Panic\\s*\\(\\s*' + STRING + '\\s*,\\s*' + STRING + '\\s*\\)', 'g'))) {
    push(m.index, 'PANIC ' + decode(m[2]) + ' PHASE ' + phaseAt(m.index) + ' ' + decode(m[1]), 'line');
  }
  for (const m of kernel.matchAll(new RegExp('Panic\\s*\\(\\s*' + STRING + '\\s*\\)', 'g'))) {
    push(m.index, 'PANIC E00 PHASE ' + phaseAt(m.index) + ' ' + decode(m[1]), 'line');
  }
  // BootDiagnosticHalt("TEXT") and BootDiagnosticHalt(Marker(StopX))
  for (const m of kernel.matchAll(new RegExp('BootDiagnosticHalt\\s*\\(\\s*' + STRING + '\\s*\\)', 'g'))) push(m.index, decode(m[1]), 'line');
  for (const m of kernel.matchAll(/BootDiagnosticHalt\s*\(\s*common::BootDiagnostic::Marker\s*\(\s*common::BootDiagnostic::(Stop\w+)\s*\)\s*\)/g)) {
    const text = markers.get(m[1]);
    if (text) push(m.index, text, 'line');
    // the reject marker is followed by the reject name on the same line, printed by two more printf calls
    if (m[1] === 'StopReject') for (const r of rejects) push(m.index, text + ' ' + r, 'line');
  }
  // printf(...) with literal arguments, including the `cond ? "A\n" : "B\n"` form
  for (const m of kernel.matchAll(/printf\s*\(([^;]{0,500}?)\)\s*;/g)) {
    for (const lit of m[1].matchAll(new RegExp(STRING, 'g'))) {
      const text = decode(lit[1]).replace(/\n+$/, '');
      if (text.length) push(m.index, text, 'line');
    }
  }
  // LogValue("LABEL ", value) -- the number is chosen at run time, so the label is a prefix
  for (const m of kernel.matchAll(new RegExp('LogValue\\s*\\(\\s*' + STRING + '\\s*,', 'g'))) {
    push(m.index, decode(m[1]).replace(/\s+$/, ''), 'prefix');
  }
  // `printf("BOOTDIAG REJECT reason=")` is completed by `printf(RejectName(...))`; the pair is one line
  for (const r of rejects) push(kernel.indexOf('BOOTDIAG REJECT reason='), 'BOOTDIAG REJECT reason=' + r, 'line');

  calls.sort((a, b) => a.at - b.at);
  // where each stop's checkpoint halts
  const halts = new Map();
  for (const m of kernel.matchAll(/BootDiagnosticHalt\s*\(\s*common::BootDiagnostic::Marker\s*\(\s*common::BootDiagnostic::(Stop\w+)\s*\)\s*\)/g)) {
    if (!halts.has(m[1])) halts.set(m[1], m.index);
  }
  return { calls, markers, halts };
};

const STOP_FOR_SCENE = {
  'runtime-init': 'StopRuntimeInit',
  'runtime-objects': 'StopRuntimeObjects',
  'runtime-recovery': 'StopRuntimeRecovery',
  'runtime-frame-recovery': 'StopRuntimeFrameRecovery',
  workers: 'StopWorkers',
  ap: 'StopAp',
  paging: 'StopPaging',
  tables: 'StopTables',
  memory: 'StopMemory',
  reject: 'StopReject',
};

export const check = () => {
  const kernel = fs.readFileSync(KERNEL, 'utf8');
  const diagnostic = fs.readFileSync(DIAGNOSTIC, 'utf8');
  const { calls, markers, halts } = emissions(kernel, diagnostic);
  const problems = [];
  const notes = [];

  if (SCENES.length !== EXPECTED_SCENE_COUNT) problems.push({ id: 'scene-count', detail: `${SCENES.length} scenes, ${EXPECTED_SCENE_COUNT} required` });
  if (COLD_REPEAT_SCENES.length !== 6) problems.push({ id: 'cold-repeat-count', detail: `${COLD_REPEAT_SCENES.length} repeats, 6 required` });
  if (EXPECTED_TRIAL_COUNT !== SCENES.length + REPEATED_SCENES.length) {
    problems.push({ id: 'trial-arithmetic', detail: `${EXPECTED_TRIAL_COUNT} != ${SCENES.length} + ${REPEATED_SCENES.length}` });
  }

  // can the kernel emit this text at all?  A prefix line is `<label> <hex8>`.
  const anywhere = (text) => calls.some((c) => c.text === text || (c.kind === 'prefix' && c.text === text) || c.text.startsWith(text + ' '));
  // is it emitted before `limit`?
  const before = (text, limit) => calls.some((c) => c.at <= limit && (c.text === text || c.text.startsWith(text + ' ')));

  for (const scene of SCENES) {
    const stopKey = scene.stop || 'reject';
    const stopName = STOP_FOR_SCENE[stopKey];
    if (!stopName) { problems.push({ id: 'stop-name:' + scene.name, detail: `no checkpoint mapped for '${stopKey}'` }); continue; }
    const halt = halts.get(stopName);
    if (halt === undefined) { problems.push({ id: 'stop-exists:' + scene.name, detail: `${stopName} has no halt in the kernel` }); continue; }

    for (const ob of scene.obligations) {
      if (ob.kind === 'number') {
        if (!anywhere(ob.label)) { problems.push({ id: 'observes:' + scene.name, detail: `no LogValue prints '${ob.label}'` }); continue; }
        if (!before(ob.label, halt)) problems.push({ id: 'observes-late:' + scene.name, detail: `'${ob.label}' is printed after ${stopName}'s halt` });
        continue;
      }
      if (ob.kind === 'line') {
        if (!anywhere(ob.text)) { problems.push({ id: 'observes:' + scene.name, detail: `no print statement emits '${ob.text}'` }); continue; }
        if (!before(ob.text, halt)) problems.push({ id: 'observes-late:' + scene.name, detail: `'${ob.text}' is printed after ${stopName}'s halt` });
        continue;
      }
      if (ob.kind === 'family') {
        const any = calls.some((c) => c.text.startsWith(ob.prefix));
        if (!any) problems.push({ id: 'family:' + scene.name, detail: `nothing is printed under '${ob.prefix}'` });
      }
    }

    // a forbidden line nobody can print is not a check: it would pass on every run, including a broken one
    for (const bad of scene.forbidden) {
      if (!anywhere(bad)) problems.push({ id: 'forbids:' + scene.name, detail: `'${bad}' cannot be emitted, so forbidding it checks nothing` });
    }

    if (!calls.some((c) => c.text === scene.terminal)) {
      problems.push({ id: 'terminal:' + scene.name, detail: `'${scene.terminal}' is not an emitted marker` });
    }
    notes.push({ scene: scene.name, stop: stopName, obligations: scene.obligations.length, forbidden: scene.forbidden.length });
  }

  return { ok: problems.length === 0, scenes: SCENES.length, emissions: calls.length, markers: markers.size, problems, notes };
};

const SELF_PATH = fileURLToPath(import.meta.url);
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(SELF_PATH)) {
  const r = check();
  for (const n of r.notes) console.log(`  ${n.scene.padEnd(30)} ${n.stop.padEnd(28)} ${String(n.obligations).padStart(3)} obligations, ${String(n.forbidden).padStart(2)} forbidden`);
  if (!r.ok) {
    console.log('');
    const byId = new Map();
    for (const p of r.problems) byId.set(p.id, (byId.get(p.id) || 0) + 1);
    for (const [id, n] of [...byId].sort()) console.log(`  FAIL ${id} (${n})`);
    console.log('');
    for (const p of r.problems.slice(0, 40)) console.log('    ' + p.id + ' -- ' + p.detail);
    if (r.problems.length > 40) console.log(`    ... ${r.problems.length - 40} more`);
  }
  console.log('\n' + (r.ok ? `contract and source agree: ${r.scenes} scenes, ${r.emissions} emitted lines, ${r.markers} markers`
    : `contract/source disagreement: ${r.problems.length} problem(s)`));
  process.exit(r.ok ? 0 : 1);
}
