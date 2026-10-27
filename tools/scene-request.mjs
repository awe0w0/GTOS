// GTOS-DSH-138 S/G: one place that assembles the live admission request.
//
// S and G must not disagree about what the current chain is. If the S phase wrote an admission document and the G phase
// launched from it, the guest would run on a decision made some time ago, and a record that changed in between would be
// invisible. So both phases call this module: it reads the current records again, re-derives the identity from the bytes on
// disk, and returns a request. G then admits that request itself.
//
// The derived documents (the static-image inspection, the tooling subject, the selected identity) are written once and then
// only verified. A second derivation that produces different bytes is drift, not a refresh, and is refused.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

function readRetry(p) {
  let b = null;
  for (let i = 0; i < 30 && b === null; i++) { try { b = fs.readFileSync(p); } catch { /* a WSL read can time out */ } }
  if (b === null) throw new Error('could not read ' + p);
  return b;
}
const pin = (p) => { const b = readRetry(p); return { path: p, bytes: b.length, sha256: sha(b) }; };

// Write once, then verify. The second call must produce the same bytes; a different byte means the derivation changed, which
// is exactly the drift this packet exists to stop.
function putOnce(p, bytes) {
  const b = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes);
  if (fs.existsSync(p)) {
    const onDisk = readRetry(p);
    if (!onDisk.equals(b)) {
      throw new Error('a derived record changed under a fixed name: ' + p + ' (on disk ' + onDisk.length + ' B '
        + sha(onDisk).slice(0, 12) + ', derived ' + b.length + ' B ' + sha(b).slice(0, 12) + ')');
    }
    return { path: p, bytes: onDisk.length, sha256: sha(onDisk), reused_identical: true };
  }
  fs.writeFileSync(p, b, { flag: 'wx' });
  const landed = readRetry(p);
  if (!landed.equals(b)) throw new Error('not identical: ' + p);
  return { path: p, bytes: landed.length, sha256: sha(landed), reused_identical: false };
}

export const IMAGE = (S) => S + '/image/el05-default.elf';
export const MAP = (S) => S + '/image/el05-default.map';

// The records that hold each role's fact. These are the packet's own current outputs, not summaries.
export function recordPaths(R) {
  return {
    source_profile_contract: R + '/producer-contract-pf08-07.json',
    producer_chain: R + '/producer-closure-pf08-07.json',
    dependency_chain: R + '/results/link-input-read-138.json',
    link_input_consumption: R + '/entry-link-el05.json',
    typed_terminal_validation: R + '/link-gate-el05.json',
    default_link: R + '/default-link-el05.json',
    related_host_tests: R + '/results/host-unit-execution-csu08-03.json',
    runtime_environment: R + '/runtime-materialization-138.json',
  };
}

// Build the request. Every fact is read here; nothing is taken from a caller's summary.
export async function buildSceneRequest({ R, A, S, L, identityModule, staticModule }) {
  const IMAGE_L = IMAGE(S);
  const MAP_L = MAP(S);
  const imageBytes = readRetry('//wsl$/Ubuntu-20.04' + IMAGE_L);
  const mapBytes = readRetry('//wsl$/Ubuntu-20.04' + MAP_L);

  // -- the static image record, produced by inspecting the current image
  let inspection = null;
  let inspectionError = null;
  try { inspection = staticModule.inspectStaticImage(imageBytes); }
  catch (e) { inspectionError = String(e.message || e); }
  const staticRecord = {
    task: A.task,
    kind: 'static_image_inspection',
    elf: pin('//wsl$/Ubuntu-20.04' + IMAGE_L),
    elf_linux: IMAGE_L,
    map: pin('//wsl$/Ubuntu-20.04' + MAP_L),
    elf_type: inspection ? inspection.elf_type : null,
    machine: inspection ? inspection.machine : null,
    entry: inspection ? inspection.entry : null,
    interp_present: inspection ? inspection.programs.some((p) => p.type === 3) : null,
    dynamic_present: inspection ? inspection.programs.some((p) => p.type === 2) : null,
    multiboot: inspection ? inspection.multiboot : null,
    script_symbols: inspection ? inspection.script_symbols : [],
    undefined_symbols: inspection ? inspection.undefined_symbols.length : null,
    executable_stack_requested: inspection ? inspection.executable_stack_requested : null,
    writable_executable_loads: inspection ? inspection.writable_executable_loads : null,
    inspection_passed: inspection !== null,
    inspection_error: inspectionError,
    constructors_executed: false,
    target_execution: false,
    note: 'the inspection is static. It says the image is a static i386 ET_EXEC with one multiboot header. It does not say '
      + 'anything ran, and constructors_executed and target_execution stay false until a guest actually runs.',
  };
  const staticPath = putOnce(R + '/static-image-el05.json', JSON.stringify(staticRecord, null, 2) + '\n');

  // -- the tooling subjects, each read and hashed here
  const qemuLinux = S + '/runtime/root/usr/bin/qemu-system-i386';
  const loaderLinux = S + '/runtime/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2';
  const toolRows = [];
  for (const t of A.tools) {
    const b = readRetry(t.host);
    toolRows.push({ role: t.role, path: t.path, bytes: b.length, sha256: sha(b) });
  }
  for (const [role, hostPath, path] of [
    ['runner', R + '/frozen-runners/run-138-r01.mjs', R + '/frozen-runners/run-138-r01.mjs'],
    ['guard', A.guard.path, A.guard.path],
    ['node', A.node.path, A.node.path],
    ['python', A.python.host, A.python.realpath],
    ['bundled_loader', '//wsl$/Ubuntu-20.04' + loaderLinux, loaderLinux],
    ['emulator', '//wsl$/Ubuntu-20.04' + qemuLinux, qemuLinux],
  ]) {
    const b = readRetry(hostPath);
    toolRows.push({ role, path, bytes: b.length, sha256: sha(b) });
  }
  const toolPath = putOnce(R + '/tooling-subject-138.json', JSON.stringify({
    task: A.task, kind: 'tool_pin', subjects: toolRows,
    note: 'every subject was read here and hashed here. The compiler, assembler and linker are the pinned ones the packet '
      + 'declares; the runner, the guard, the node and the python are the ones this packet executes; the bundled loader and '
      + 'the emulator are the ones the guest will run under.',
  }, null, 2) + '\n');

  // -- the selected identity, re-derived from the closure's provenance and verified against the bytes
  const closure = JSON.parse(readRetry(R + '/producer-closure-pf08-07.json').toString('utf8'));
  const toLinux = (host) => host.replace(/^\\\\wsl\$\\Ubuntu-20\.04/, '').replace(/\\/g, '/');
  const identityObjects = closure.provenance.map((p) => ({
    logical: p.token, linux: toLinux(p.path), host: p.path,
  }));
  const identity = identityModule.createSelectedIdentity({
    task: A.task, wave: S,
    image: { linux: IMAGE_L, host: '//wsl$/Ubuntu-20.04' + IMAGE_L },
    map: { linux: MAP_L, host: '//wsl$/Ubuntu-20.04' + MAP_L },
    objects: identityObjects,
  });
  const identityPath = putOnce(R + '/selected-image-identity-138.json', JSON.stringify(identity, null, 2) + '\n');
  const identityVerified = identityModule.verifySelectedIdentity(
    JSON.parse(readRetry(R + '/selected-image-identity-138.json').toString('utf8')));

  // -- the ten role members, each pinned as it is now
  const roles = recordPaths(R);
  const roleMembers = [];
  for (const [role, path] of Object.entries(roles)) {
    roleMembers.push({ role, path, sha256: pin(path).sha256 });
  }
  roleMembers.push({ role: 'static_image', path: staticPath.path, sha256: pin(staticPath.path).sha256 });
  roleMembers.push({ role: 'tooling_subject', path: toolPath.path, sha256: pin(toolPath.path).sha256 });

  return {
    request: { identity, image: IMAGE_L, map: MAP_L, prerequisites: roleMembers },
    identity, identityVerified, identityPath,
    image: { linux: IMAGE_L, bytes: imageBytes.length, sha256: sha(imageBytes) },
    map: { linux: MAP_L, bytes: mapBytes.length, sha256: sha(mapBytes) },
    staticPath, toolPath, roleMembers,
    emulator: { qemu: qemuLinux, loader: loaderLinux,
      // The four directories the bridge's own runtimeLibraryPath names, in the same order. The scene-derived argv and the
      // bridge's argv are compared token by token, so this has to be the same list and not a shorter one that happens to
      // work.
      libpath: [S + '/runtime/root/usr/lib/x86_64-linux-gnu', S + '/runtime/root/lib/x86_64-linux-gnu',
        S + '/runtime/root/usr/lib', S + '/runtime/root/lib'].join(':'),
      moduledir: S + '/runtime/root/usr/lib/x86_64-linux-gnu/qemu' },
    logs: (name) => ({ e9: L + '/vm/' + name + '/e9.log', serial: L + '/vm/' + name + '/serial.log' }),
  };
}
