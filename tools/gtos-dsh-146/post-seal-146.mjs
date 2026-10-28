// GTOS-DSH-146: the external derived receipt.
//
// The taskbook asks for the exact final-tar hash and the post-seal fresh results to be an EXTERNAL derived receipt: not stuffed
// back into the payload, and not claimed as known before the seal. This writes exactly that. It is a record ABOUT the sealed
// transaction and the two things that were run against it afterwards, and it is deliberately not a payload member -- the
// payload was sealed before these results existed and re-sealing to include them is forbidden.
//
// It also states the ordering plainly, because the taskbook asked for business commits and the report before the archive and
// two of them came after.

import crypto from 'node:crypto';
import fs from 'node:fs';

const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const pin = (p) => ({ path: p, bytes: fs.statSync(p).size, sha256: sha(fs.readFileSync(p)) });

const stateRows = fs.readFileSync(R + '/publication-state-146.jsonl', 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l));
const archiveRow = stateRows.find((r) => r.verb === 'archive');
const sealRow = stateRows.find((r) => r.verb === 'seal');
const receipt = readJson(R + '/archive-146/receipt-146.json');
const fresh = readJson(R + '/fresh-146-01.json');
const ioReal = readJson(R + '/io-real-146-01.json');

const out = {
  task: 'GTOS-DSH-146',
  stage: 'post-seal-146-01',
  what: 'the external derived receipt for the one production archive and seal, with the two post-seal fresh consumptions of '
    + 'the sealed payload. This file is NOT a payload member and is not pinned by the receipt: the payload was sealed before '
    + 'these results existed, and a second seal to carry them is forbidden.',
  is_a_payload_member: false,
  sealed_transaction: {
    payload: pin(R + '/archive-146/payload-146.tar'),
    receipt: pin(R + '/archive-146/receipt-146.json'),
    plan: pin(R + '/plan-146-01.json'),
    spec: pin(R + '/spec-146-02.json'),
    context: pin(R + '/publication-context-146.json'),
    state: pin(R + '/publication-state-146.jsonl'),
    verification: pin(R + '/verify-146.json'),
    ordinal_digest: receipt.ordinal_digest,
    member_count: receipt.member_count,
    archives: stateRows.filter((r) => r.verb === 'archive').length,
    seals: stateRows.filter((r) => r.verb === 'seal').length,
    archive_row_digest: archiveRow ? archiveRow.ordinal_digest : null,
    seal_row_payload_sha256: sealRow ? sealRow.payload_sha256 : null,
    terminal: sealRow ? sealRow.terminal === true : false,
    exactly_one_archive_and_one_seal: stateRows.filter((r) => r.verb === 'archive').length === 1
      && stateRows.filter((r) => r.verb === 'seal').length === 1,
  },
  post_seal_fresh: {
    fresh: { record: pin(R + '/fresh-146-01.json'), ok: fresh.ok,
      members_read_back: fresh.members.members,
      ordinal_digest_recomputed: fresh.members.ordinal_digest,
      matches_the_receipt: fresh.members.matches,
      role_records_inside_the_payload: fresh.role_records_in_the_payload.rows.map((r) => r.role + ':' + r.says_true),
      required_files_present: fresh.closure.present + '/' + fresh.closure.required_files,
      consumers: Object.fromEntries(Object.entries(fresh.consumers).filter(([k]) => k !== 'ok').map(([k, v]) => [k, v.ok])),
      negatives_refused: fresh.negatives.refused + '/' + fresh.negatives.total },
    io_real: { record: pin(R + '/io-real-146-01.json'), ok: ioReal.ok,
      families: ioReal.runs.map((r) => r.family + ':' + r.accepted + '/' + r.total),
      requirements_at_their_required_tier: ioReal.members.satisfied + '/' + ioReal.members.total,
      packet_records: ioReal.packet_records.satisfied + '/' + ioReal.packet_records.total,
      traced_reads: ioReal.traces.reduce((n, t) => n + (t.reads || 0), 0),
      traced_parses: ioReal.traces.reduce((n, t) => n + (t.parses || 0), 0),
      trace_refusals: ioReal.traces.reduce((n, t) => n + (t.refusals || []).length, 0),
      negatives_refused: ioReal.negatives.refused + '/' + ioReal.negatives.total },
    both_ran_on_the_extracted_payload_tree: fresh.extraction.tree === ioReal.tree.path,
    tree: fresh.extraction.tree,
  },
  ordering: {
    business_commits_before_the_archive: 4,
    commits_before_the_archive: [
      '6669527', '7ea8060', '494bc47', 'b90dba1',
    ],
    commits_after_the_archive: ['126f8f1'],
    report_written_after_the_archive: true,
    taskbook_asked_for: 'all business commits/report before the archive',
    shortfall: 'the IO measurement over the real consumers and the report were written after the seal. The IO measurement '
      + 'cannot honestly be taken from a rehearsal tree when the question is what the sealed payload s read set is, and the '
      + 'report cannot precede the results it reports. Recorded rather than back-dated; the four implementation commits did '
      + 'precede the archive, and the payload carries the code that produced every pre-seal record.',
    disclosed_in: 'q-ordering-146.json',
  },
  ok: fresh.ok === true && ioReal.ok === true,
};
const p = R + '/post-seal-146-01.json';
if (fs.existsSync(p)) fs.unlinkSync(p);
fs.writeFileSync(p, JSON.stringify(out, null, 2) + '\n');
console.log(JSON.stringify({ ok: out.ok, one_archive_one_seal: out.sealed_transaction.exactly_one_archive_and_one_seal,
  fresh: out.post_seal_fresh.fresh.ok, io_real: out.post_seal_fresh.io_real.ok,
  reads: out.post_seal_fresh.io_real.traced_reads, parses: out.post_seal_fresh.io_real.traced_parses,
  requirements: out.post_seal_fresh.io_real.requirements_at_their_required_tier }, null, 2));
