// The profile the producer stages consume.
//
// These three files are the build profile 144 compiled under: the production Makefile, the mandatory kernel flag list, and the
// host-test delta note. 147 compiles the same sources with the same flags, so the profile is copied rather than retyped -- a
// retyped Makefile would be a second profile and the flags would stop being the ones the image was built with. The only edit is
// the packet identity inside the note, so a reader is not told the file belongs to another packet.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const HERE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).replace(/\\/g, '/');
const R = path.resolve(HERE, '..', '..', '..').replace(/\\/g, '/');
const SRC = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a/profile-144';
const DST = R + '/profile-147';
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

const rows = [];
for (const name of ['Makefile', 'kernel-cxxflags', 'host-test-delta-runtime.txt']) {
  const bytes = fs.readFileSync(SRC + '/' + name);
  // Only the note carries the packet identity; the Makefile and the flag list are copied byte for byte, because they are the
  // exact bytes the compiler profile is checked against.
  const out = name === 'host-test-delta-runtime.txt'
    ? Buffer.from(bytes.toString('utf8').split('GTOS-DSH-144').join('GTOS-DSH-147').split('144').join('147'))
    : bytes;
  fs.mkdirSync(DST, { recursive: true });
  const to = DST + '/' + name;
  if (fs.existsSync(to)) {
    const existing = fs.readFileSync(to);
    if (!existing.equals(out)) throw new Error('refusing to overwrite ' + to);
    rows.push({ name, bytes: existing.length, sha256: sha256(existing), unchanged: true });
    continue;
  }
  fs.writeFileSync(to, out, { flag: 'wx' });
  rows.push({ name, bytes: out.length, sha256: sha256(out), unchanged: false,
    byte_identical_to_144: name === 'host-test-delta-runtime.txt' ? bytes.equals(out) : true });
}
console.log(JSON.stringify({ profile: DST, rows }, null, 2));
