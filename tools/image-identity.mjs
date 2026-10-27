// GTOS-DSH-138 T0: one selected image identity, bound from the current materials through to the guest request.
//
// The 113 failure this module exists to prevent: the static gate inspected one image, the guest request froze a
// *pathname*, and that pathname resolved to a different image with the same size. Nothing in the chain compared the
// bytes the gate approved against the bytes the emulator was handed, so "approved A, ran B" was possible and happened.
//
// The rule here is that an identity is a set of byte hashes, not a name. A path is only a way to find bytes; the hash
// is what identifies them. Two different pathnames may carry identical bytes and are then the same identity; the same
// pathname may carry different bytes at different times and is then a different identity.
//
// This module never loads or executes an image. It reads bytes and compares them.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

function readPinned(what, member) {
  if (!member || typeof member !== 'object') throw new Error('IDENTITY: ' + what + ' member required');
  const linux = member.linux;
  const host = member.host;
  if (typeof linux !== 'string' || !linux.startsWith('/')) {
    throw new Error('IDENTITY: ' + what + ' needs an absolute Linux path');
  }
  if (typeof host !== 'string' || !host) throw new Error('IDENTITY: ' + what + ' needs a host path');
  // A UNC read can fail transiently, so the existence test is retried before it is treated as absence.
  let present = false;
  for (let attempt = 0; attempt < 10 && !present; attempt++) present = fs.existsSync(host);
  if (!present) throw new Error('IDENTITY: ' + what + ' absent: ' + host);
  const stat = fs.statSync(host);
  if (!stat.isFile()) throw new Error('IDENTITY: ' + what + ' is not a regular file: ' + host);
  const bytes = fs.readFileSync(host);
  if (bytes.length === 0) throw new Error('IDENTITY: ' + what + ' is empty: ' + host);
  return { linux, host, bytes: bytes.length, sha256: sha(bytes) };
}

function samePin(a, b) {
  return a && b && a.bytes === b.bytes && a.sha256 === b.sha256;
}

// ---------------------------------------------------------------- the selected identity
// Built from the current materials, never from the artifact under test. `wave` and `objects` are the inputs that
// produced the image; recording them is what lets a later reader tell whether the image still corresponds to them.
export function createSelectedIdentity(materials) {
  const image = readPinned('image', materials.image);
  const map = readPinned('map', materials.map);
  if (typeof materials.wave !== 'string' || !materials.wave.startsWith('/')) {
    throw new Error('IDENTITY: the source wave must be an absolute Linux path');
  }
  if (!Array.isArray(materials.objects) || materials.objects.length === 0) {
    throw new Error('IDENTITY: at least one object must be recorded');
  }
  const objects = materials.objects.map((o) => {
    if (typeof o.logical !== 'string' || !o.logical) throw new Error('IDENTITY: object logical name required');
    return { logical: o.logical, ...readPinned('object', o) };
  });
  // A logical name may appear once only: two objects claiming the same role is not an identity.
  const seen = new Set();
  for (const o of objects) {
    if (seen.has(o.logical)) throw new Error('IDENTITY: duplicate object logical name: ' + o.logical);
    seen.add(o.logical);
  }
  const profile = materials.profile ? readPinned('profile', materials.profile) : null;
  const document = {
    kind: 'selected_image_identity',
    task: materials.task,
    // The identity covers the bytes AND the Linux paths the guest will be given, so a path swap that keeps the bytes is
    // visible and a byte swap that keeps the path is refused.
    identity: sha(Buffer.from(JSON.stringify({
      image: [image.linux, image.sha256],
      map: [map.linux, map.sha256],
      wave: materials.wave,
      objects: objects.map((o) => [o.logical, o.linux, o.sha256]),
    }), 'utf8')),
    image, map,
    wave: materials.wave,
    objects,
    profile,
    note: 'an identity is byte hashes bound to the Linux paths the guest is given; the same pathname at a different '
      + 'time is a different identity',
  };
  return document;
}

// Re-reads every member and requires it to still be what the identity says. This is the check a consumer runs before
// it launches anything: if the bytes moved, the approved identity is not what is on disk.
export function verifySelectedIdentity(identity, overrides) {
  if (!identity || identity.kind !== 'selected_image_identity') {
    throw new Error('IDENTITY: not a selected identity document');
  }
  const pick = (member, name) => (overrides && overrides[name] ? overrides[name] : member);
  const nowImage = readPinned('image', pick(identity.image, 'image'));
  const nowMap = readPinned('map', pick(identity.map, 'map'));
  if (!samePin(nowImage, identity.image)) {
    throw new Error('IDENTITY: the image bytes are not the selected identity: ' + nowImage.host
      + ' has ' + nowImage.bytes + '/' + nowImage.sha256.slice(0, 16)
      + ' but the identity records ' + identity.image.bytes + '/' + identity.image.sha256.slice(0, 16));
  }
  if (!samePin(nowMap, identity.map)) {
    throw new Error('IDENTITY: the map bytes are not the selected identity: ' + nowMap.host);
  }
  for (const object of identity.objects) {
    const now = readPinned('object', object);
    if (!samePin(now, object)) {
      throw new Error('IDENTITY: object bytes moved since selection: ' + object.logical);
    }
  }
  if (identity.profile) {
    const now = readPinned('profile', identity.profile);
    if (!samePin(now, identity.profile)) throw new Error('IDENTITY: profile bytes moved since selection');
  }
  return { verified: true, identity: identity.identity, image: nowImage, map: nowMap };
}

// The guest request consumer. A request must name the selected identity and its image path must carry exactly those
// bytes. Refusing here is what prevents "approved A, ran B", and it happens before any child exists.
export function assertRequestUsesSelectedIdentity(request, identity, options) {
  if (!request || typeof request !== 'object') throw new Error('IDENTITY: a guest request is required');
  if (!identity || identity.kind !== 'selected_image_identity') {
    throw new Error('IDENTITY: no selected identity was supplied to the request');
  }
  if (request.identity !== identity.identity) {
    throw new Error('IDENTITY: the request names a different selected identity: '
      + String(request.identity) + ' vs ' + identity.identity);
  }
  // The guest is handed a Linux path. It must be the selected Linux path, because that is what the identity binds.
  if (typeof request.image !== 'string' || !request.image.startsWith('/')) {
    throw new Error('IDENTITY: the request carries no Linux image path');
  }
  if (request.image !== identity.image.linux) {
    throw new Error('IDENTITY: the request Linux image path is not the selected one: ' + request.image
      + ' vs ' + identity.image.linux);
  }
  if (!request.image_pin) throw new Error('IDENTITY: the request carries no image pin');
  if (!samePin(request.image_pin, identity.image)) {
    throw new Error('IDENTITY: the request image pin is not the selected image: '
      + String(request.image_pin.sha256).slice(0, 16) + ' vs ' + identity.image.sha256.slice(0, 16));
  }
  if (request.map_pin && !samePin(request.map_pin, identity.map)) {
    throw new Error('IDENTITY: the request map pin is not the selected map');
  }
  // Independently of the declared pin, the bytes the request would actually hand the emulator must be the approved
  // bytes. A caller that rewrote the file after selection is caught here, before any child exists.
  const resolver = (options && options.resolve) || null;
  const host = resolver ? resolver(request.image) : identity.image.host;
  const now = readPinned('request image', { linux: request.image, host });
  if (!samePin(now, identity.image)) {
    throw new Error('IDENTITY: the request path carries different bytes than the selected identity: '
      + host + ' has ' + now.sha256.slice(0, 16));
  }
  return { accepted: true, identity: identity.identity, image: now };
}

// ---------------------------------------------------------------- the map, parsed rather than searched
// A GNU ld map records a symbol as either `0xADDR name` or `0xADDR name = .`. The 113 gate searched the whole map text
// for an address string, which passes when the address appears anywhere — on another symbol's line, in a size column,
// or in a comment. This parser builds an actual name-to-address table and refuses anything ambiguous.
export function parseMapSymbols(mapText) {
  if (typeof mapText !== 'string' || mapText.length === 0) throw new Error('MAP: map text required');
  const lines = mapText.split('\n');
  const symbols = new Map();
  const duplicates = [];
  let definitions = 0;
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    // Both accepted forms, anchored, so a fragment inside another column cannot match.
    const definition = /^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/.exec(line);
    // An assignment whose right-hand side is any expression: `= .`, `= ALIGN (0x1000)`, and so on. The address on the
    // left is the resolved value, which is exactly what the association needs.
    const assignment = /^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\S.*$/.exec(line);
    const match = definition || assignment;
    if (!match) continue;
    const address = parseInt(match[1], 16);
    if (!Number.isSafeInteger(address) || address < 0 || address > 0xFFFFFFFF) {
      throw new Error('MAP: symbol address out of range on line ' + (index + 1));
    }
    const name = match[2];
    definitions += 1;
    if (symbols.has(name)) {
      if (symbols.get(name) !== address) duplicates.push({ name, first: symbols.get(name), again: address });
      continue;
    }
    symbols.set(name, address);
  }
  if (definitions === 0) throw new Error('MAP: no symbol definitions found');
  if (duplicates.length > 0) {
    throw new Error('MAP: a symbol is defined at two addresses: ' + JSON.stringify(duplicates.slice(0, 4)));
  }
  return { symbols, definitions, names: [...symbols.keys()] };
}

// The association the 113 gate lacked: every required symbol's address in the ELF symbol table must equal its address
// in the map, and every required symbol must appear in both.
export function crossCheckMap(inspection, mapSymbols, requiredNames) {
  if (!inspection || !Array.isArray(inspection.sections)) throw new Error('MAP: an image inspection is required');
  if (!mapSymbols || !(mapSymbols.symbols instanceof Map)) throw new Error('MAP: parsed map symbols required');
  if (!Array.isArray(requiredNames) || requiredNames.length === 0) {
    throw new Error('MAP: at least one required symbol name');
  }
  const fromImage = new Map();
  for (const symbol of inspection.symbols || []) {
    if (symbol.section === 0) continue;
    if (!symbol.raw) continue;
    if (!fromImage.has(symbol.raw)) fromImage.set(symbol.raw, symbol.value);
  }
  const missingInImage = [];
  const missingInMap = [];
  const mismatched = [];
  for (const name of requiredNames) {
    const inImage = fromImage.get(name);
    const inMap = mapSymbols.symbols.get(name);
    if (inImage === undefined) { missingInImage.push(name); continue; }
    if (inMap === undefined) { missingInMap.push(name); continue; }
    if (inImage !== inMap) mismatched.push({ name, image: inImage, map: inMap });
  }
  if (missingInImage.length || missingInMap.length || mismatched.length) {
    throw new Error('MAP: symbol to address association failed: '
      + JSON.stringify({ missingInImage, missingInMap, mismatched: mismatched.slice(0, 4) }));
  }
  return { associated: requiredNames.length, names: requiredNames };
}
