// GTOS-DSH-110 unified native request normaliser.
//
// One production place that owns three things the earlier packets each wrote differently:
//
//   1. path identity      every file carries host path, Linux path, logical identity, role and pin together
//   2. tool roles         parsed from the actual authority `tools[]`; a missing, duplicated or unknown role refuses
//   3. control shape      every file reference that crosses into Linux is emitted in Linux form, at any nesting depth
//
// The 109 packet failed because four different places each invented their own conversion: the bridge read a
// non-existent `authority.system_loader`, the stage control was hand-assembled per stage, the Python control carried
// `F:/` paths, and a control key named `require` shadowed the helper's assertion function. This module exists so that
// none of those four can recur, and the tests exercise the actual functions below rather than a model of them.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// Names a control key must never take, because the Linux helper binds them itself. A control object that carries any
// of these would shadow a function or module inside the helper, which is exactly the 109 failure.
export const RESERVED_CONTROL_KEYS = Object.freeze([
  'require', 'main', 'digest', 'hashlib', 'json', 'os', 'stat', 'sys', 'open', 'control',
]);

// ---------------------------------------------------------------- path identity

const WINDOWS_DRIVE = /^([A-Za-z]):[\\/](.*)$/;

// A Windows host path becomes the WSL mount form. This is the conversion 109 was missing inside the JSON control.
export function hostToLinux(value) {
  if (typeof value !== 'string' || value === '') throw new Error('PATH: host path must be a non-empty string');
  if (value.includes('\0')) throw new Error('PATH: host path carries a NUL');
  // An absolute POSIX path is already Linux, whether it is a drive mount or a real root path.
  if (value.startsWith('/') && !value.startsWith('//')) {
    if (value.includes('\\') || path.posix.normalize(value) !== value) {
      throw new Error('PATH: non-canonical Linux path ' + value);
    }
    return value;
  }
  if (value.startsWith('//wsl$/') || value.startsWith('\\\\wsl$\\')) {
    const rest = value.replace(/^\/\/wsl\$\/[^/]+/i, '').replace(/^\\\\wsl\\[^\\]+/i, '').replace(/\\/g, '/');
    if (!rest.startsWith('/')) throw new Error('PATH: malformed wsl path ' + value);
    return rest;
  }
  const m = WINDOWS_DRIVE.exec(value);
  if (!m) throw new Error('PATH: unrecognised path form ' + value);
  const drive = m[1].toLowerCase();
  const tail = m[2].replace(/\\/g, '/');
  if (tail.split('/').some((part) => part === '..')) throw new Error('PATH: path escapes its drive ' + value);
  return '/mnt/' + drive + '/' + tail;
}

// The inverse, so a host identity can always be recovered from the Linux form.
export function linuxToHost(value) {
  const linux = hostToLinux(value);
  const m = /^\/mnt\/([a-z])\/(.*)$/.exec(linux);
  if (!m) {
    if (linux.startsWith('/mnt/')) throw new Error('PATH: unsupported mount ' + linux);
    return '//wsl$/Ubuntu-20.04' + linux;
  }
  return m[1].toUpperCase() + ':/' + m[2];
}

// Every file the request touches is described once, with all five facts attached.
export function describeFile(entry) {
  if (!entry || typeof entry !== 'object') throw new Error('FILE: entry must be an object');
  const { logical, host, role, bytes, sha256: pin } = entry;
  if (typeof logical !== 'string' || logical === '') throw new Error('FILE: logical identity required');
  if (logical.startsWith('/') || logical.includes('\\') || logical.includes('\0')
    || logical.split('/').some((p) => p === '' || p === '.' || p === '..')) {
    throw new Error('FILE: logical identity must be a safe relative path: ' + logical);
  }
  if (typeof role !== 'string' || role === '') throw new Error('FILE: role required for ' + logical);
  if (!Number.isSafeInteger(bytes) || bytes < 0) throw new Error('FILE: byte count required for ' + logical);
  if (!/^[0-9a-f]{64}$/.test(String(pin))) throw new Error('FILE: sha256 required for ' + logical);
  const linux = hostToLinux(host);
  return { logical, role, bytes, sha256: pin,
    host: linuxToHost(linux), linux, pin: { bytes, sha256: pin } };
}

// ---------------------------------------------------------------- tool roles

// Parse the authority's tools[] into a role map. A missing role, a duplicate role or an entry with no role refuses.
// This is what replaces the 109 bridge reading `authority.system_loader` and silently continuing when it was absent.
export function parseToolRoles(authority) {
  if (!authority || !Array.isArray(authority.tools) || authority.tools.length === 0) {
    throw new Error('TOOLS: the authority carries no tools[] array');
  }
  const byRole = new Map();
  for (const tool of authority.tools) {
    if (!tool || typeof tool !== 'object') throw new Error('TOOLS: a tool entry is not an object');
    if (typeof tool.role !== 'string' || tool.role === '') {
      throw new Error('TOOLS: a tool entry carries no role');
    }
    if (typeof tool.path !== 'string' || !tool.path.startsWith('/')) {
      throw new Error('TOOLS: role ' + tool.role + ' has no absolute Linux path');
    }
    if (byRole.has(tool.role)) throw new Error('TOOLS: duplicate role ' + tool.role);
    if (!Number.isSafeInteger(tool.bytes) || !/^[0-9a-f]{64}$/.test(String(tool.sha256))) {
      throw new Error('TOOLS: role ' + tool.role + ' has no pin');
    }
    byRole.set(tool.role, { role: tool.role, linux: tool.path,
      host: linuxToHost(tool.path), bytes: tool.bytes, sha256: tool.sha256,
      pin: { bytes: tool.bytes, sha256: tool.sha256 } });
  }
  return byRole;
}

// A required role, or a refusal. `system_loader` is an accepted alias for `loader`, because the 110 authority
// declares it that way, but the alias resolves THROUGH the tools[] table rather than being assumed.
export function requireRole(roles, name) {
  const alias = name === 'system-loader' || name === 'system_loader' ? 'loader' : name;
  const found = roles.get(alias);
  if (!found) throw new Error('TOOLS: required role ' + alias + ' is absent from tools[]');
  return found;
}

// The exact file-level EXEC set for a route. No directory grant, no shell, no duplicate.
export function execSetFor(kind, authority, roles, context) {
  const py = authority.python.realpath;
  if (typeof py !== 'string' || !py.startsWith('/')) throw new Error('EXEC: python realpath required');
  const loader = requireRole(roles, 'system-loader').linux;
  const rt = context.runtime_root;
  const qemuLoader = rt + '/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2';
  const qemuElf = rt + '/root/usr/bin/qemu-system-i386';
  const sets = {
    python: [py, loader],
    cxx: [py, loader, requireRole(roles, 'g++').linux, requireRole(roles, 'cc1plus').linux,
      requireRole(roles, 'as').linux],
    as: [py, loader, requireRole(roles, 'as').linux],
    ld: [py, loader, requireRole(roles, 'ld.bfd').linux],
    qemu: [py, loader, qemuLoader, qemuElf],
  };
  if (kind === 'hostunit') {
    const elf = context.test_elf_linux;
    if (typeof elf !== 'string' || !elf.startsWith('/')) {
      throw new Error('EXEC: a host unit needs an absolute gated test ELF');
    }
    return [py, loader, elf];
  }
  const set = sets[kind];
  if (!set) throw new Error('EXEC: unknown execution kind ' + kind);
  if (new Set(set).size !== set.length) throw new Error('EXEC: duplicate file in the EXEC set for ' + kind);
  for (const one of set) {
    if (!path.posix.isAbsolute(one) || one.endsWith('/')) {
      throw new Error('EXEC: every grant must be an absolute file, got ' + one);
    }
  }
  return set;
}

// ---------------------------------------------------------------- control shape

// A control object must not carry a key that would shadow something inside the Linux helper, at any depth.
export function assertNoShadowing(value, trail = 'control') {
  if (value === null || typeof value !== 'object') return value;
  if (Array.isArray(value)) {
    value.forEach((item, index) => assertNoShadowing(item, trail + '[' + index + ']'));
    return value;
  }
  for (const key of Object.keys(value)) {
    if (RESERVED_CONTROL_KEYS.includes(key)) {
      throw new Error('CONTROL: key ' + trail + '.' + key
        + ' would shadow an internal name inside the Linux helper');
    }
    assertNoShadowing(value[key], trail + '.' + key);
  }
  return value;
}

// Rewrite every host-looking path inside a nested structure to its Linux form, leaving pins, roles and logical
// identities untouched. This is the recursive conversion that 109 only did for the top-level argv.
export function linuxiseDeep(value, trail = 'control') {
  if (typeof value === 'string') {
    if (WINDOWS_DRIVE.test(value) || value.startsWith('//wsl$/') || value.startsWith('\\\\wsl$\\')) {
      return hostToLinux(value);
    }
    return value;
  }
  if (value === null || typeof value !== 'object') return value;
  if (Array.isArray(value)) return value.map((item, i) => linuxiseDeep(item, trail + '[' + i + ']'));
  const out = {};
  for (const [key, item] of Object.entries(value)) out[key] = linuxiseDeep(item, trail + '.' + key);
  return out;
}

// Any host-looking path that survived linuxiseDeep is a defect: catch it rather than shipping it to Linux.
export function assertNoHostPaths(value, trail = 'control') {
  if (typeof value === 'string') {
    if (WINDOWS_DRIVE.test(value)) {
      throw new Error('CONTROL: ' + trail + ' still carries a Windows path: ' + value);
    }
    return value;
  }
  if (value === null || typeof value !== 'object') return value;
  if (Array.isArray(value)) {
    value.forEach((item, i) => assertNoHostPaths(item, trail + '[' + i + ']'));
    return value;
  }
  for (const [key, item] of Object.entries(value)) assertNoHostPaths(item, trail + '.' + key);
  return value;
}

// Build a Linux control from a host control: normalise, then prove both properties.
export function buildLinuxControl(hostControl) {
  assertNoShadowing(hostControl);
  const linux = linuxiseDeep(hostControl);
  assertNoShadowing(linux);
  assertNoHostPaths(linux);
  return linux;
}

// ---------------------------------------------------------------- wave ownership

// setup creates stable containers only. A wave directory belongs to exactly one stage, which creates it.
// An existing wave refuses and is left untouched; a fresh wave is created exclusively.
export function claimWave(context, wave) {
  if (typeof wave !== 'string' || !/^wave-[0-9]{2}$/.test(wave)) {
    throw new Error('WAVE: wave id must be wave-NN, got ' + wave);
  }
  const linux = context.source_root + '/' + wave;
  const host = '//wsl$/Ubuntu-20.04' + linux;
  if (fs.existsSync(host)) {
    const entries = fs.readdirSync(host);
    throw new Error('WAVE: ' + linux + ' already exists with ' + entries.length
      + ' entr(ies); a wave belongs to exactly one stage and is never reused or cleared');
  }
  fs.mkdirSync(host);
  const st = fs.lstatSync(host);
  if (!st.isDirectory() || st.isSymbolicLink()) throw new Error('WAVE: ' + linux + ' is not a real directory');
  return { wave, linux, host, created: true, pre_existing: false };
}

// The containers setup may create. A wave is deliberately absent from this list.
export const SETUP_CONTAINERS = Object.freeze([
  'logs', 'evidence', 'inputs', 'frozen-helpers', 'staging', 'objects', 'test-build', 'image',
]);

export function isSetupContainer(name) {
  return SETUP_CONTAINERS.includes(name);
}

// ---------------------------------------------------------------- subject pinning

// A production subject is pinned before and after a call, so a test cannot fix the script and leave the imported
// object unpinned.
export function subjectPin(hostPath) {
  const bytes = fs.readFileSync(hostPath);
  return { path: hostPath, bytes: bytes.length, sha256: sha(bytes) };
}

export function assertSubjectUnchanged(pin) {
  const now = subjectPin(pin.path);
  if (now.bytes !== pin.bytes || now.sha256 !== pin.sha256) {
    throw new Error('SUBJECT: ' + pin.path + ' changed during the call: now ' + now.bytes + ' B '
      + now.sha256 + ', pinned ' + pin.bytes + ' B ' + pin.sha256);
  }
  return { unchanged: true, ...now };
}
