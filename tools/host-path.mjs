// GTOS-DSH-136 E: whose path is it, and who is allowed to write it.
//
// In 135 the link entry converted a host path to a Linux path and handed it to a Windows child. Windows Node resolved
// `/mnt/f/...` against the current drive and created `F:/mnt/f/...`, a tree outside the packet root. The review found the
// file there, byte for byte. Nothing in the chain checked which platform would consume the path, and nothing checked that
// the resulting location was inside a root the caller was authorised to write.
//
// The rule this module enforces is that a path is converted *for its consumer*, and only at a boundary where the consumer
// is known:
//
//   a Windows Node child reads and writes host paths
//   the bridge, the guard, GNU ld and QEMU read Linux paths
//
// A Linux path handed to a Windows consumer is refused rather than silently reinterpreted. A conversion returns the path
// for the named platform, and the caller must have said which platform that is.
//
// It also separates two questions that 135 merged. Two paths can denote the same *identity* while not being the same
// *writable name*: the same file on ext4 under two casings is one identity, but on Windows the two spellings do reach the
// same file while on ext4 they do not. Identity comparison is for checking; it is never used to build a path to open.

import path from 'node:path';

// ---------------------------------------------------------------- platforms

export const PLATFORMS = Object.freeze(['win32', 'linux']);

// A drive-letter path, its /mnt alias, and the UNC path of the same distribution are three spellings of one host file.
// A different distribution's UNC path is a different host, and is not that file.
function splitHost(p) {
  const s = p.replace(/\\/g, '/');
  const unc = /^\/\/wsl\$\/([^/]+)\/(.*)$/i.exec(s);
  if (unc) return { kind: 'unc', distro: unc[1], rest: unc[2] };
  const drive = /^([A-Za-z]):\/(.*)$/.exec(s);
  if (drive) return { kind: 'drive', drive: drive[1].toLowerCase(), rest: drive[2] };
  const mnt = /^\/mnt\/([a-z])\/(.*)$/.exec(s);
  if (mnt) return { kind: 'mnt', drive: mnt[1], rest: mnt[2] };
  if (s.startsWith('/')) return { kind: 'linux', rest: s };
  return { kind: 'relative', rest: s };
}

// The identity of a host file, for comparison only. Two spellings that name the same file on this host share an identity.
// The distribution is part of the identity of a UNC path, so two distributions never collapse into one.
export function identityOf(p) {
  if (typeof p !== 'string' || !p) return null;
  const parts = splitHost(p);
  switch (parts.kind) {
    case 'unc': {
      // A UNC path is the distribution's view of the same host. When its rest is a /mnt alias or a drive spelling, it names
      // the same file; otherwise it is a distribution-local path and the distribution is part of its identity.
      const inner = /^mnt\/([a-z])\/(.*)$/.exec(parts.rest);
      if (inner) return 'drive:' + inner[1] + ':' + inner[2].toLowerCase();
      const direct = /^([A-Za-z]):\/(.*)$/.exec(parts.rest);
      if (direct) return 'drive:' + direct[1].toLowerCase() + ':' + direct[2].toLowerCase();
      return 'unc:' + parts.distro.toLowerCase() + ':' + parts.rest;
    }
    case 'drive': return 'drive:' + parts.drive + ':' + parts.rest.toLowerCase();
    case 'mnt': return 'drive:' + parts.drive + ':' + parts.rest.toLowerCase();
    case 'linux': return 'linux:' + parts.rest;
    default: return 'relative:' + parts.rest;
  }
}

// Whether two paths name the same file on this host. This is for checking a declaration, not for opening anything.
export function sameHostFile(a, b) {
  const ia = identityOf(a);
  return ia !== null && ia === identityOf(b);
}

// ---------------------------------------------------------------- conversion at a boundary

// The path a consumer on `platform` must be given. The conversion is explicit about which platform, and a path that is
// already in the wrong domain is refused rather than reinterpreted: that refusal is the 135 defect.
export function forConsumer(p, platform) {
  if (typeof p !== 'string' || !p) {
    const e = new Error('PATH: a path is required');
    e.obligation = 'path';
    throw e;
  }
  if (!PLATFORMS.includes(platform)) {
    const e = new Error('PATH: unknown consumer platform: ' + String(platform));
    e.obligation = 'platform';
    throw e;
  }
  const parts = splitHost(p);

  if (platform === 'linux') {
    // GNU, the bridge and QEMU read Linux paths.
    if (parts.kind === 'drive') return '/mnt/' + parts.drive + '/' + parts.rest;
    if (parts.kind === 'unc') return '/' + parts.rest;
    if (parts.kind === 'linux' || parts.kind === 'mnt') return p.replace(/\\/g, '/');
    const e = new Error('PATH: a relative path cannot be handed to a Linux consumer: ' + p);
    e.obligation = 'domain';
    throw e;
  }

  // platform === 'win32'. A Windows child reads and writes host paths.
  if (parts.kind === 'drive') return parts.drive.toUpperCase() + ':/' + parts.rest;
  if (parts.kind === 'unc') return '//wsl$/' + parts.distro + '/' + parts.rest;
  if (parts.kind === 'linux' || parts.kind === 'mnt') {
    // A POSIX path handed to a Windows consumer is exactly what created F:/mnt/f. It is refused, not converted, because a
    // conversion here would be guessing which host file the caller meant.
    const e = new Error('PATH: a POSIX path cannot be handed to a Windows consumer: ' + p
      + '; convert it at the boundary where the producer knew the host location');
    e.obligation = 'domain';
    e.refused_path = p;
    throw e;
  }
  const e = new Error('PATH: a relative path cannot be handed to a Windows consumer: ' + p);
  e.obligation = 'domain';
  throw e;
}

// ---------------------------------------------------------------- the authorisation gate

// Every write is checked against the roots the caller was given, on the platform that will perform it. This is a
// declaration and flow check: it stops this pipeline from opening a file outside its roots, and it does not claim to stop
// arbitrary code from writing anywhere it likes.
export function assertInsideRoots(target, roots, platform) {
  if (typeof target !== 'string' || !target) {
    const e = new Error('ROOT GATE: a write target is required');
    e.obligation = 'target';
    throw e;
  }
  if (!Array.isArray(roots) || roots.length === 0) {
    const e = new Error('ROOT GATE: no authorised roots were declared');
    e.obligation = 'roots';
    throw e;
  }
  const consumerPath = forConsumer(target, platform);

  // The comparison is on the consumer's own spelling, with the platform's own separator rules, and the boundary is a
  // directory boundary: a root must be a prefix ending at a separator, so /a/bc is not inside /a/b.
  const normalise = (q) => (platform === 'win32'
    ? q.replace(/\\/g, '/').replace(/\/{2,}/g, '/').toLowerCase()
    : q.replace(/\/{2,}/g, '/'));
  const targetNorm = normalise(consumerPath);
  const resolved = platform === 'win32' ? targetNorm : path.posix.normalize(targetNorm);
  // A target that walks out with .. is refused before the prefix test, because normalisation would otherwise move it back
  // inside a root it never belonged to.
  if (/(^|\/)\.\.(\/|$)/.test(targetNorm)) {
    const e = new Error('ROOT GATE: the target walks out with ..: ' + target);
    e.obligation = 'traversal';
    throw e;
  }
  for (const root of roots) {
    const rootNorm = normalise(forConsumer(root, platform));
    const rootResolved = platform === 'win32' ? rootNorm : path.posix.normalize(rootNorm);
    const withSlash = rootResolved.endsWith('/') ? rootResolved : rootResolved + '/';
    if (resolved === rootResolved || resolved.startsWith(withSlash)) {
      return { ok: true, target: consumerPath, root, platform };
    }
  }
  const e = new Error('ROOT GATE: the target is outside every authorised root: ' + target
    + ' (' + consumerPath + ' on ' + platform + ')');
  e.obligation = 'root';
  e.target = target;
  e.consumer_path = consumerPath;
  e.roots = roots;
  throw e;
}

// A single place that decides what a stage may write, so no caller assembles the list by hand.
export function stageRoots(stateRoot, platform) {
  return {
    platform,
    roots: [stateRoot],
    note: 'a stage writes inside the root it was given, and nowhere else',
  };
}
