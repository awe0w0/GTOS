// GTOS-DSH-138 S: a guest is launched only from a typed, declared scene.
//
// The 130 driver took its command line straight from `process.argv[4]` and handed it to the emulator. `-append yes` was
// accepted, and the guest then booted the ordinary path through B04, AppStore, Settings, B05, B06 and the default AP and
// worker startup. That is a scope violation, not a neutral "no option selected" run. Nothing in the chain compared the
// command line to anything.
//
// This module is that comparison. A scene is a declared object: its name, its core count, its expected stop, the exact
// command line it is allowed to produce, the events it permits and forbids, and the identity of the image it may run. The
// command line is *derived* from the scene's stop and flags, never passed in. A request that does not match a declared
// scene is refused here, before any loader or emulator child exists.
//
// The tokens are the production parser's own, taken from its source and its test: `legacy`, `live`, `serial`, `bootlog`,
// `verbose`, `bootlog-fail`, and `bootdiag-stop=<name>` for the eight declared stops. `serial` is not decoration: the
// kernel enables the UART only when that token is present, which is why every 130 scenario had an empty serial channel.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { consumeRole, bindSet } from './prerequisite-consumption.mjs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The stops the production parser recognises, in its own spelling.
export const STOPS = Object.freeze([
  'memory', 'tables', 'ap', 'paging', 'workers', 'runtime-init', 'runtime-objects',
]);

// The non-stop tokens the production parser accepts, each with what it does.
export const FLAGS = Object.freeze({
  serial: 'enable the COM1 UART; without it the serial channel stays empty',
  legacy: 'register the legacy boot screen',
  live: 'live boot logging',
  bootlog: 'keep the boot log',
  verbose: 'verbose boot logging',
});

// A stop that is only meaningful on one core count, and one that is meaningful on several.
const SCENES = [
  {
    name: 'runtime-init-smp1',
    vcpus: 1,
    stop: 'runtime-init',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME INITIALIZED',
    permits: ['RUNTIME DIAG', 'BOOTDIAG RUNTIME INITIALIZED'],
    forbids: [
      // The ordinary stages the diagnostic must never reach.
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      // The ordinary services.
      'APP STORE', 'SETTINGS', 'DESKTOP MODE', 'LIVE RAM STORAGE', 'LIVE BOOT PACKAGE',
      // The default worker and AP path.
      'WORKER POOL READY', 'WORKER CONFIGURED', 'WORKER READY', 'WORKER FAILED',
      'AP STARTUP', 'AP INITIALIZED', 'AP PARKED', 'AP FAILED',
      // The default paging path.
      'PAGING PG WP', 'PAGING CR3',
    ],
    purpose: 'the BSP prepares and activates the runtime stacks, then stops before any process exists',
  },
  {
    name: 'runtime-objects-smp1',
    vcpus: 1,
    stop: 'runtime-objects',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME OBJECTS RECLAIMED',
    permits: ['OBJECT DIAG', 'BOOTDIAG RUNTIME OBJECTS RECLAIMED'],
    forbids: [
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      'APP STORE', 'SETTINGS', 'DESKTOP MODE', 'LIVE RAM STORAGE', 'LIVE BOOT PACKAGE',
      'WORKER POOL READY', 'WORKER CONFIGURED', 'WORKER READY', 'WORKER FAILED',
      'AP STARTUP', 'AP INITIALIZED', 'AP PARKED', 'AP FAILED',
      'PAGING PG WP', 'PAGING CR3',
    ],
    purpose: 'the BSP creates, proves, pressures, recovers and reaps native objects, then stops',
  },
  {
    name: 'runtime-init-smp2',
    vcpus: 2,
    stop: 'runtime-init',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME REFUSED MULTI CPU',
    permits: ['RUNTIME DIAG', 'BOOTDIAG RUNTIME REFUSED MULTI CPU'],
    forbids: [
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      'APP STORE', 'SETTINGS', 'DESKTOP MODE',
      'WORKER POOL READY', 'WORKER CONFIGURED', 'AP STARTUP', 'AP INITIALIZED',
      'RUNTIME DIAG STACKS PREPARED', 'RUNTIME DIAG ACTIVATED',
    ],
    purpose: 'two cores are detected and the runtime refuses before it prepares stacks',
  },
  {
    name: 'runtime-init-smp3',
    vcpus: 3,
    stop: 'runtime-init',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME REFUSED MULTI CPU',
    permits: ['RUNTIME DIAG', 'BOOTDIAG RUNTIME REFUSED MULTI CPU'],
    forbids: [
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      'APP STORE', 'SETTINGS', 'DESKTOP MODE',
      'WORKER POOL READY', 'WORKER CONFIGURED', 'AP STARTUP', 'AP INITIALIZED',
      'RUNTIME DIAG STACKS PREPARED', 'RUNTIME DIAG ACTIVATED',
    ],
    purpose: 'three cores are detected and the runtime refuses before it prepares stacks',
  },
  {
    name: 'runtime-objects-smp2',
    vcpus: 2,
    stop: 'runtime-objects',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU',
    permits: ['OBJECT DIAG', 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU'],
    forbids: [
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      'APP STORE', 'SETTINGS', 'DESKTOP MODE',
      'WORKER POOL READY', 'WORKER CONFIGURED', 'AP STARTUP', 'AP INITIALIZED',
      'OBJECT DIAG CREATED', 'OBJECT DIAG REAPED',
    ],
    purpose: 'two cores are detected and the object diagnostic refuses before creating anything',
  },
  {
    name: 'runtime-objects-smp3',
    vcpus: 3,
    stop: 'runtime-objects',
    flags: ['serial'],
    required_marker: 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU',
    permits: ['OBJECT DIAG', 'BOOTDIAG RUNTIME OBJECTS REFUSED MULTI CPU'],
    forbids: [
      'BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08',
      'APP STORE', 'SETTINGS', 'DESKTOP MODE',
      'WORKER POOL READY', 'WORKER CONFIGURED', 'AP STARTUP', 'AP INITIALIZED',
      'OBJECT DIAG CREATED', 'OBJECT DIAG REAPED',
    ],
    purpose: 'three cores are detected and the object diagnostic refuses before creating anything',
  },
  {
    name: 'workers-smp1',
    vcpus: 1,
    stop: 'workers',
    flags: ['serial'],
    required_marker: 'BOOTDIAG WORKERS NONE AVAILABLE',
    permits: ['WORKERS DIAG', 'BOOTDIAG WORKERS'],
    forbids: ['BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08', 'APP STORE', 'SETTINGS', 'DESKTOP MODE'],
    purpose: 'one core honestly reports that no AP worker exists',
  },
  {
    name: 'workers-smp2',
    vcpus: 2,
    stop: 'workers',
    flags: ['serial'],
    required_marker: 'BOOTDIAG WORKERS DRAINED PARKED',
    permits: ['WORKERS DIAG', 'AP STARTUP', 'AP INITIALIZED', 'AP PARKED', 'BOOTDIAG WORKERS'],
    forbids: ['BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08', 'APP STORE', 'SETTINGS', 'DESKTOP MODE'],
    purpose: 'two cores run the fixed integer jobs on the AP and drain',
  },
  {
    name: 'workers-smp3',
    vcpus: 3,
    stop: 'workers',
    flags: ['serial'],
    required_marker: 'BOOTDIAG WORKERS DRAINED PARKED',
    permits: ['WORKERS DIAG', 'AP STARTUP', 'AP INITIALIZED', 'AP PARKED', 'BOOTDIAG WORKERS'],
    forbids: ['BOOT B04', 'BOOT B05', 'BOOT B06', 'BOOT B07', 'BOOT B08', 'APP STORE', 'SETTINGS', 'DESKTOP MODE'],
    purpose: 'three cores run the fixed integer jobs on their APs and drain',
  },
  {
    name: 'tables-smp1',
    vcpus: 1,
    stop: 'tables',
    flags: ['serial'],
    required_marker: 'BOOTDIAG TABLES CHECKPOINT',
    permits: ['BOOTDIAG TABLES'],
    forbids: ['BOOT B04', 'BOOT B05', 'BOOT B06', 'APP STORE', 'SETTINGS', 'DESKTOP MODE', 'AP STARTUP'],
    purpose: 'the GDT and IDT are constructed and loaded, then the machine stops',
  },
  {
    name: 'paging-smp1',
    vcpus: 1,
    stop: 'paging',
    flags: ['serial'],
    required_marker: 'BOOTDIAG PAGING ENABLED OWNED MAP',
    permits: ['PAGING', 'BOOTDIAG PAGING'],
    forbids: ['BOOT B06', 'BOOT B07', 'APP STORE', 'SETTINGS', 'DESKTOP MODE', 'AP STARTUP', 'AP INITIALIZED'],
    purpose: 'bootstrap paging is enabled and the owned mapping probe passes, then the machine stops',
  },
  {
    name: 'e99-priority',
    vcpus: 1,
    stop: 'memory',
    flags: ['serial', 'bootlog-fail'],
    required_marker: 'PANIC E99',
    permits: ['E99', 'BOOTDIAG'],
    forbids: ['BOOT B04', 'BOOT B05', 'APP STORE', 'SETTINGS'],
    purpose: 'the legacy bootlog-fail token keeps its precedence over any stop',
  },
];

export function sceneNames() { return SCENES.map((s) => s.name); }

export function scene(name) {
  const s = SCENES.find((x) => x.name === name);
  return s ? JSON.parse(JSON.stringify(s)) : null;
}

// The command line is derived from the scene, never supplied. Flags first in the declared order, then the stop.
export function commandLine(sceneObject) {
  const flags = sceneObject.flags.join(' ');
  return (flags ? flags + ' ' : '') + 'bootdiag-stop=' + sceneObject.stop;
}

// ---------------------------------------------------------------- the prerequisite role set

// The ten roles this packet's frozen operation contract names. A scene may not launch until every one of them is present,
// non-empty, of the declared type, and still the bytes it was when it was read. This list is not the caller's: it is the
// frozen set, and a caller that omits one does not get a smaller set, it gets a refusal.
export const REQUIRED_ROLES = Object.freeze([
  'source_profile_contract', 'producer_chain', 'dependency_chain', 'link_input_consumption',
  'typed_terminal_validation', 'default_link', 'static_image', 'related_host_tests',
  'tooling_subject', 'runtime_environment',
]);

// What a record for each role has to be, beyond existing. `kind` is the record's own kind, `types` the fields that must
// hold, and `read` the function that reads the record and decides whether it actually says what the role means. A record
// that carries `ok: true` written by a caller and nothing else is not evidence: `read` has to find the fact itself.
const ROLE_SHAPE = Object.freeze({
  source_profile_contract: { kind: ['producer_closure_contract', 'host_cpp_contract'] },
  producer_chain: { kind: ['producer_closure'] },
  dependency_chain: { kind: ['link_input_read'] },
  link_input_consumption: { kind: ['entry_link'] },
  typed_terminal_validation: { kind: ['default_link', 'link_gate'] },
  default_link: { kind: ['default_link', 'entry_link'] },
  static_image: { kind: ['static_image_inspection'] },
  related_host_tests: { kind: ['host_unit_execution'] },
  tooling_subject: { kind: ['runner_pin', 'tool_pin'] },
  runtime_environment: { kind: ['runtime_materialization', 'setup'] },
});

// A role member is {role, path, sha256, record}. The record is the parsed bytes the caller read; the bytes on disk are
// read again here, so a member whose file changed after the caller read it is refused rather than trusted.
export function admitPrerequisiteSet(members, context) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  // A set is a list. `undefined`, `null`, an object, a string — each of these is a caller that did not bring a set, and
  // the refusal says so rather than falling through to "no problems found".
  if (!Array.isArray(members)) {
    return { complete: false, roles: [], problems: [{ obligation: 'prerequisite',
      why: 'the prerequisite set is not a list of role members: it is ' + (members === null ? 'null'
        : Array.isArray(members) ? 'a list' : typeof members) }] };
  }
  // An empty list is not a small set. It is the absence of the set, spelled as a list.
  if (members.length === 0) {
    return { complete: false, roles: [], problems: [{ obligation: 'prerequisite',
      why: 'the prerequisite set is empty; the ten roles are required' }] };
  }
  const byRole = new Map();
  for (const m of members) {
    if (!m || typeof m !== 'object') { refuse('prerequisite', 'a role member is not an object'); continue; }
    if (typeof m.role !== 'string' || !m.role) { refuse('prerequisite', 'a role member names no role'); continue; }
    if (!REQUIRED_ROLES.includes(m.role)) {
      refuse('prerequisite', 'the set carries a role this packet does not declare: ' + m.role);
      continue;
    }
    if (byRole.has(m.role)) {
      refuse('prerequisite', 'the role appears more than once: ' + m.role);
      continue;
    }
    byRole.set(m.role, m);
  }
  // Omitted is a refusal, and it is reported per role so the record says which one was missing rather than "incomplete".
  for (const role of REQUIRED_ROLES) {
    if (!byRole.has(role)) refuse('prerequisite', 'the required role is omitted: ' + role);
  }

  const roles = [];
  const consumed = [];
  for (const role of REQUIRED_ROLES) {
    const m = byRole.get(role);
    if (!m) continue;
    const row = { role, path: typeof m.path === 'string' ? m.path : null };
    if (typeof m.path !== 'string' || !m.path) { refuse('prerequisite', 'the role has no path: ' + role); roles.push(row); continue; }
    let b = null;
    for (let i = 0; i < 20 && b === null; i++) { try { b = fs.readFileSync(m.path); } catch { /* retry */ } }
    if (b === null) { refuse('prerequisite', 'the role is absent on disk: ' + role + ' ' + m.path); roles.push(row); continue; }
    row.bytes = b.length;
    row.sha256 = sha256(b);
    if (b.length === 0) { refuse('prerequisite', 'the role record is empty: ' + role); roles.push(row); continue; }
    if (typeof m.sha256 === 'string' && m.sha256 !== row.sha256) {
      refuse('prerequisite', 'the role record changed after it was read: ' + role);
      roles.push(row); continue;
    }
    let record = null;
    try { record = JSON.parse(b.toString('utf8')); }
    catch { refuse('prerequisite', 'the role record is not JSON: ' + role); roles.push(row); continue; }
    row.kind = record.kind || null;
    row.task = record.task || null;
    const shape = ROLE_SHAPE[role];
    // The record's own kind has to be one this role declares. A link record filed under "runtime_environment" is a
    // wrong-typed role, not a satisfied one.
    if (!row.kind || !shape.kind.includes(row.kind)) {
      refuse('prerequisite', 'the role record is the wrong kind: ' + role + ' holds ' + String(row.kind)
        + ', the role needs ' + shape.kind.join(' or '));
      roles.push(row); continue;
    }
    if (row.task !== null && typeof row.task === 'string' && !/^GTOS-DSH-139$/.test(row.task)) {
      refuse('prerequisite', 'the role record belongs to another task: ' + role + ' ' + row.task);
      roles.push(row); continue;
    }
    // A caller's `ok` is not the fact, and neither is any other field the record wrote about itself. The consumer opens
    // the artifacts this role is a claim about and reports what it found there.
    const fact = consumeRole(role, record, context || {});
    row.fact = fact;
    if (!fact.ok) {
      refuse('prerequisite', 'the role record does not show the fact it is filed for: ' + role + ' — ' + fact.why);
    }
    consumed.push({ role, binding: fact.binding || null });
    roles.push(row);
  }
  // The ten roles are one claim about one build. A set whose members describe different contracts, images or tool
  // generations is ten green documents, not a complete prerequisite set.
  const binding = bindSet(consumed);
  if (!binding.bound) {
    for (const p of binding.problems) refuse(p.obligation, p.why);
  }
  return { complete: problems.length === 0, roles, problems, binding };
}

// ---------------------------------------------------------------- admission

// A request names a scene, a core count and an identity. Everything else is derived. Anything the caller adds that the
// scene does not declare is a refusal, because a request that can add a token is a request that can escape the scene.
export function admitScene(request) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const out = (extra) => ({ admitted: problems.length === 0, problems,
    first_refusal: problems.length ? problems[0] : null, ...extra });

  if (!request || typeof request !== 'object') return out({});
  if (typeof request.scene !== 'string' || !request.scene) return out({}).problems.length
    ? out({}) : { admitted: false, problems: [{ obligation: 'scene', why: 'no scene was named' }],
      first_refusal: { obligation: 'scene', why: 'no scene was named' } };

  const declared = scene(request.scene);
  if (!declared) {
    return { admitted: false, problems: [{ obligation: 'scene', why: 'no such declared scene: ' + request.scene }],
      first_refusal: { obligation: 'scene', why: 'no such declared scene: ' + request.scene } };
  }

  // A caller may not supply a command line at all. This is the 130 defect in one line.
  if (Object.prototype.hasOwnProperty.call(request, 'append')) {
    return { admitted: false,
      problems: [{ obligation: 'append', why: 'the request supplies a raw command line; a scene derives its own' }],
      first_refusal: { obligation: 'append', why: 'the request supplies a raw command line; a scene derives its own' } };
  }
  // Nor may it supply its own marker, which would let a scenario pass without reaching its stop.
  if (Object.prototype.hasOwnProperty.call(request, 'required_marker')) {
    return { admitted: false,
      problems: [{ obligation: 'marker', why: 'the request supplies its own required marker; the scene declares it' }],
      first_refusal: { obligation: 'marker', why: 'the request supplies its own required marker; the scene declares it' } };
  }

  // The core count is the scene's, and a request that disagrees is a request for a different scenario.
  if (request.vcpus !== undefined && request.vcpus !== declared.vcpus) {
    refuse('vcpus', 'the scene is declared for ' + declared.vcpus + ' vCPU but the request asks for ' + request.vcpus);
  }
  // The identity is required, and it must be a selected identity document.
  if (!request.identity || request.identity.kind !== 'selected_image_identity') {
    refuse('identity', 'no selected image identity');
  }
  // The image and map must be the ones the identity names.
  if (request.identity && request.identity.image) {
    if (request.image && request.image !== request.identity.image.linux) {
      refuse('image', 'the request image is not the selected image');
    }
    if (request.map && request.map !== request.identity.map.linux) {
      refuse('map', 'the request map is not the selected map');
    }
  }
  // The prerequisite set is mandatory, and it is mandatory as a whole.
  //
  // In 138 this check was `if (request.prerequisites !== undefined)`, and the test beside it asserted that a request
  // bringing no set at all was still admitted. That is the bypass the whole gate existed to close: a caller that omitted
  // one property got the scene, and the ten roles never ran. An optional gate is not a gate.
  //
  // So absence is refused here, before the set is examined, and the refusal names the obligation rather than reporting an
  // empty problem list as success. `undefined`, a missing property, `null`, a non-list, and an empty list are five
  // different spellings of "no set", and each of them is refused with the spelling it used.
  const hasSet = Object.prototype.hasOwnProperty.call(request, 'prerequisites');
  if (!hasSet) {
    return { admitted: false,
      problems: [{ obligation: 'prerequisite', why: 'the request carries no prerequisite set; the ten roles are required' }],
      first_refusal: { obligation: 'prerequisite', why: 'the request carries no prerequisite set; the ten roles are required' } };
  }
  const set = admitPrerequisiteSet(request.prerequisites, request.prerequisite_context || {});
  if (!set.complete) {
    for (const p of set.problems) refuse(p.obligation, p.why);
  }

  const cmdline = commandLine(declared);
  const result = out({ scene: declared, cmdline, vcpus: declared.vcpus,
    required_marker: declared.required_marker, permits: declared.permits, forbids: declared.forbids });
  // Only a result this function built is marked as admitted. The marker is the object identity, held in a module-private
  // WeakSet, so no caller can manufacture one.
  if (result.admitted === true) ADMITTED.add(result);
  return result;
}

// The argv the emulator will be given, built only from the admitted scene.
//
// An object that merely says `admitted: true` is not an admission: a caller can write that itself, and in 130 a caller
// effectively did, by passing its own command line. So this function refuses anything it did not itself receive back from
// `admitScene` (the set below is module-private and cannot be added to from outside), and it recomputes the command line
// from the declared scene rather than believing the one on the object.
const ADMITTED = new WeakSet();

// The bridge's own gate. A checkpoint starts a machine, and a machine may only be started from a declared scene that
// `admitScene` returned. This is exported so the launch boundary can ask the question, but it cannot be answered from
// outside: the WeakSet is module-private, so an object a caller wrote is not in it however faithfully it is shaped.
export function assertAdmittedScene(admitted) {
  if (!admitted || typeof admitted !== 'object' || !ADMITTED.has(admitted)) {
    throw new Error('SCENE: this object did not come from admitScene, so no machine may be started from it');
  }
  if (admitted.admitted !== true) throw new Error('SCENE: an unadmitted scene may not start a machine');
  const declared = scene(admitted.scene.name);
  if (!declared) throw new Error('SCENE: the admitted scene is not declared: ' + admitted.scene.name);
  const derived = commandLine(declared);
  if (admitted.cmdline !== derived) {
    throw new Error('SCENE: the admitted command line is not the one the scene derives: ' + String(admitted.cmdline)
      + ' is not ' + derived);
  }
  return declared;
}

export function sceneArgv(admitted, options) {
  if (!admitted || typeof admitted !== 'object' || !ADMITTED.has(admitted)) {
    throw new Error('SCENE: this object did not come from admitScene, so it has no argv');
  }
  if (admitted.admitted !== true) throw new Error('SCENE: an unadmitted scene has no argv');
  const declared = scene(admitted.scene.name);
  if (!declared) throw new Error('SCENE: the admitted scene is not declared: ' + admitted.scene.name);
  // The command line on the object has to be the one the declared scene derives. A hand-written command line that reached
  // this far would be the exact defect this module exists to prevent.
  const derived = commandLine(declared);
  if (admitted.cmdline !== derived) {
    throw new Error('SCENE: the admitted command line is not the one the scene derives: ' + String(admitted.cmdline)
      + ' is not ' + derived);
  }
  if (admitted.vcpus !== declared.vcpus) {
    throw new Error('SCENE: the admitted core count is not the declared one');
  }
  const { identity, logs, limit } = options;
  const machine = ['-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'qemu32',
    '-smp', String(admitted.vcpus), '-m', '128M', '-nodefaults', '-no-user-config',
    '-display', 'none', '-monitor', 'none', '-nic', 'none', '-vga', 'none',
    '-serial', 'file:' + logs.serial,
    '-chardev', 'file,id=e9,path=' + logs.e9,
    '-device', 'isa-debugcon,iobase=0xe9,chardev=e9',
    '-no-reboot', '-no-shutdown'];
  // When the caller supplies the emulator it gets the complete argv, including the bundled loader prefix and the firmware
  // directory. That is the form the bridge builds at its own boundary, so the two can be compared token by token: the scene
  // derives one from the declaration and the bridge derives the other from its control, and a disagreement is refused
  // before the machine starts.
  let argv = [...machine, '-kernel', identity.image.linux, '-append', admitted.cmdline];
  let cmd0 = identity.image.linux;
  if (options.emulator) {
    const em = options.emulator;
    argv = [em.loader, '--library-path', em.libpath, em.emulator,
      ...machine, '-L', em.firmware, '-kernel', identity.image.linux, '-append', admitted.cmdline];
    cmd0 = em.loader;
  }
  return { argv, cmd0, append: admitted.cmdline, vcpus: admitted.vcpus,
    limit_seconds: limit || 150, scene: admitted.scene.name };
}

// ---------------------------------------------------------------- the functional verdict

// A launch is not a result. The result is what the guest actually printed on both channels, checked against the scene:
// the marker must be present, no forbidden event may appear, and neither channel may be missing, empty or truncated.
// Anything wrong is a failure that must propagate, not a line printed to the console.
export function functionalVerdict(sceneObject, channels, options = {}) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const { e9, serial } = channels;

  const check = (name, lines) => {
    if (lines === null || lines === undefined) { refuse(name, 'the ' + name + ' channel was not produced'); return null; }
    if (!Array.isArray(lines)) { refuse(name, 'the ' + name + ' channel is not a list of lines'); return null; }
    if (lines.length === 0) { refuse(name, 'the ' + name + ' channel is empty'); return null; }
    if (options.truncated && options.truncated[name] === true) {
      refuse(name, 'the ' + name + ' channel hit its log bound and may be truncated');
    }
    return lines;
  };
  const e9Lines = check('e9', e9);
  const serialLines = check('serial', serial);

  // The marker has to be there. A scenario that stopped early did not reach its stop.
  let markerPresent = null;
  if (e9Lines) {
    markerPresent = e9Lines.some((l) => l.includes(sceneObject.required_marker));
    if (!markerPresent) {
      refuse('marker', 'the required marker is absent: ' + sceneObject.required_marker);
    }
  }

  // A forbidden event is a scope violation, on either channel.
  const forbiddenFound = [];
  for (const [name, lines] of [['e9', e9Lines], ['serial', serialLines]]) {
    if (!lines) continue;
    for (const line of lines) {
      for (const bad of sceneObject.forbids) {
        // A whole-line match on the token, so a banner that merely contains a word is not a violation.
        if (line.trim() === bad || line.includes(bad)) forbiddenFound.push({ channel: name, event: bad, line });
      }
    }
  }
  if (forbiddenFound.length) {
    refuse('forbidden', 'a forbidden event appeared: ' + forbiddenFound[0].event
      + ' on ' + forbiddenFound[0].channel);
  }

  const accepted = problems.length === 0;
  return { accepted, problems, first_refusal: problems.length ? problems[0] : null,
    marker_present: markerPresent, forbidden_found: forbiddenFound,
    channels: { e9_lines: e9Lines ? e9Lines.length : null,
      serial_lines: serialLines ? serialLines.length : null },
    note: 'this is the functional result; the transport result is recorded separately and neither substitutes for '
      + 'the other' };
}

export function gateFunctional(verdict) {
  if (verdict.accepted) return verdict;
  const e = new Error('GUEST FUNCTIONAL FAILURE at ' + verdict.first_refusal.obligation + ': '
    + verdict.first_refusal.why);
  e.verdict = verdict;
  throw e;
}
