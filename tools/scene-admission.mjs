// GTOS-DSH-131 S: a guest is launched only from a typed, declared scene.
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
  // The prerequisite results this packet must have produced, named by path. A missing one is a refusal, because the
  // scene is not allowed to run on an image whose current chain was never admitted.
  if (Array.isArray(request.prerequisites)) {
    for (const p of request.prerequisites) {
      if (!p || typeof p.path !== 'string') { refuse('prerequisite', 'a prerequisite has no path'); continue; }
      let b = null;
      for (let i = 0; i < 20 && b === null; i++) { try { b = fs.readFileSync(p.path); } catch { /* retry */ } }
      if (b === null) { refuse('prerequisite', 'the prerequisite is absent: ' + p.path); continue; }
      if (typeof p.sha256 === 'string' && sha256(b) !== p.sha256) {
        refuse('prerequisite', 'the prerequisite bytes changed: ' + p.path);
      }
      if (typeof p.ok === 'boolean' && p.ok !== true) {
        refuse('prerequisite', 'the prerequisite did not pass: ' + p.path);
      }
    }
  }

  const cmdline = commandLine(declared);
  return out({ scene: declared, cmdline, vcpus: declared.vcpus,
    required_marker: declared.required_marker, permits: declared.permits, forbids: declared.forbids });
}

// The argv the emulator will be given, built only from the admitted scene.
export function sceneArgv(admitted, options) {
  if (!admitted || admitted.admitted !== true) throw new Error('SCENE: an unadmitted scene has no argv');
  const { identity, logs, limit } = options;
  const base = ['-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'qemu32',
    '-smp', String(admitted.vcpus), '-m', '128M', '-nodefaults', '-no-user-config',
    '-display', 'none', '-monitor', 'none', '-nic', 'none', '-vga', 'none',
    '-serial', 'file:' + logs.serial,
    '-chardev', 'file,id=e9,path=' + logs.e9,
    '-device', 'isa-debugcon,iobase=0xe9,chardev=e9',
    '-no-reboot', '-no-shutdown',
    '-kernel', identity.image.linux,
    '-append', admitted.cmdline];
  return { argv: base, append: admitted.cmdline, vcpus: admitted.vcpus,
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
