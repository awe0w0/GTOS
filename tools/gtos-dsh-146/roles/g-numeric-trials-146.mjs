// GTOS-DSH-146: judge every trial in the 144 matrix with this packet's own numeric judge.
//
// The 144 resource judge covers three scenes: the ones whose subject is an allocator, a recovery, or a frame recovery. The
// other seventeen scenes are about numbers that are not resources -- CR0, CR3, the TSS descriptor, the paging canaries, the
// AP state, the ticket ranges -- and 144 judged those with markers: a scene passed if its channel contained a string.
//
// That is the gap this runs at. Each trial's two channels are parsed into labelled numbers, and the numbers are checked
// against each other: CR3 equals DIR and is page aligned, the TSS descriptor's type is 0x8B, the descriptor's base is the
// EXPECT the trial declared, the SGDT limit contains the selector's index, the paging canary the page table maps is the
// canary the kernel wrote, the AP state is the parked bitmask, the workers' pool is sized from the detected count, and each
// ticket's VALUE is the EXPECTED printed beside it.
//
// Two scenes never reach the boot numbers -- one panics at B02, one has its option rejected -- and those are judged as
// refusals rather than against numbers they never printed.
//
// A scene the judge has no obligations for is refused, not skipped: a scene that ran and was not judged is a trial whose
// output nobody read, and counting it as a pass is how a marker becomes a verdict.
//
// This is a module rather than a script because two things run it: the standalone run that writes its own record, and the
// `guest_resource_semantics_full` role, which owns the judgement and reports it inside its own record. One implementation,
// two callers.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

export const R = 'F:/GTOS-Chromium/artifacts/native-end-to-end-consumers-and-independent-required-closure-20261028-a';
export const R144 = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const WSL = '//wsl$/Ubuntu-20.04';
export const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':/' + p.slice(9) : WSL + p);
const readJson = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));

export async function judgeAllTrials() {
  const N = await import(pathToFileURL(R + '/worktree-146/tools/gtos-dsh-146/r-numeric-146.mjs').href);
  const matrixPath = R144 + '/g-matrix-gm144-01-144.json';
  const matrix = readJson(matrixPath);

  const rows = [];
  for (const t of matrix.trials) {
    const key = t.scene + '-t' + t.trial;
    const serialPath = t.serial && t.serial.path;
    const e9Path = t.e9 && t.e9.path;
    if (!serialPath || !e9Path) {
      rows.push({ trial: key, scene: t.scene, judged: false, accepted: false, why: 'the trial names no channel' });
      continue;
    }
    const serial = fs.readFileSync(toHost(serialPath), 'utf8');
    const e9 = fs.readFileSync(toHost(e9Path), 'utf8');
    const serialSha = sha(Buffer.from(serial));
    const e9Sha = sha(Buffer.from(e9));
    let verdict = null;
    let error = null;
    try { verdict = N.judgeNonResource(t.scene, serial, e9); }
    catch (e) { error = String(e && e.message ? e.message : e); }
    // The judge answers `{ obligations: [{obligation, value}], failed: [names], ok, numbers_read }`. An earlier version of
    // this code read `accepted` and `o.ok`, neither of which the judge produces, so every obligation looked failed and all
    // 28 trials were reported as refused by a judge that had in fact passed them. The shape is read from the return value.
    const families = verdict && Array.isArray(verdict.obligations)
      ? [...new Set(verdict.obligations.map((o) => o.obligation.split('.')[0]))] : null;
    rows.push({
      trial: key, scene: t.scene,
      judged: verdict !== null,
      accepted: !!(verdict && verdict.ok === true),
      obligations: verdict && Array.isArray(verdict.obligations) ? verdict.obligations.length : null,
      families,
      failed_obligations: verdict && Array.isArray(verdict.failed) ? verdict.failed : null,
      numbers_parsed: verdict && typeof verdict.numbers_read === 'number' ? verdict.numbers_read : null,
      channels_compared: verdict ? verdict.channels_compared : null,
      repeated_labels: verdict ? verdict.repeated_labels : null,
      channels: {
        serial: { path: serialPath, bytes: serial.length, sha256: serialSha,
          matches_matrix: t.serial.sha256 ? serialSha === t.serial.sha256 : null },
        e9: { path: e9Path, bytes: e9.length, sha256: e9Sha,
          matches_matrix: t.e9.sha256 ? e9Sha === t.e9.sha256 : null },
      },
      error,
    });
  }

  const judged = rows.filter((r) => r.judged);
  const accepted = rows.filter((r) => r.accepted);
  const unjudged = rows.filter((r) => !r.judged);
  const scenes = [...new Set(rows.map((r) => r.scene))];
  const coveredScenes = [...new Set(judged.map((r) => r.scene))];
  const drifted = rows.filter((r) => r.channels
    && ((r.channels.serial.matches_matrix === false) || (r.channels.e9.matches_matrix === false)));

  return {
    judge: { module: R + '/worktree-146/tools/gtos-dsh-146/r-numeric-146.mjs',
      bytes: fs.statSync(R + '/worktree-146/tools/gtos-dsh-146/r-numeric-146.mjs').size,
      sha256: sha(fs.readFileSync(R + '/worktree-146/tools/gtos-dsh-146/r-numeric-146.mjs')) },
    matrix: { path: matrixPath, bytes: fs.statSync(matrixPath).size, sha256: sha(fs.readFileSync(matrixPath)) },
    numeric_scenes: N.NUMERIC_SCENES, marker_only_scenes: N.MARKER_ONLY_SCENES,
    trials_total: rows.length, trials_judged: judged.length, trials_accepted: accepted.length,
    scenes_total: scenes.length, scenes_judged: coveredScenes.length,
    unjudged_trials: unjudged.map((r) => ({ trial: r.trial, why: r.why || (r.error ? r.error : 'the judge returned no verdict') })),
    trials_whose_channels_drifted_from_the_matrix: drifted.map((r) => r.trial),
    failed_trials: rows.filter((r) => r.judged && !r.accepted).map((r) => ({ trial: r.trial, failed_obligations: r.failed_obligations })),
    rows,
    ok: unjudged.length === 0 && accepted.length === rows.length && drifted.length === 0 && coveredScenes.length === scenes.length,
  };
}

// Run directly: write the standalone record.
if (process.argv[1] && /g-numeric-trials-146\.mjs$/.test(process.argv[1].replace(/\\/g, '/'))) {
  const verdict = await judgeAllTrials();
  const out = {
    task: 'GTOS-DSH-146',
    what: 'every trial in the 144 matrix, judged by this packet own numeric judge rather than by a marker',
    ...verdict,
    ok_means: 'every trial in the matrix was parsed into numbers and judged, every scene has at least one judged trial, no '
      + 'channel drifted from the hash the matrix pinned, and every obligation holds on the real text.',
  };
  const outPath = R + '/stage-output/roles-146-' + (process.argv[2] || '01') + '/g-numeric-full.json';
  if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
  fs.writeFileSync(outPath, JSON.stringify(out, null, 2) + '\n');
  console.log(JSON.stringify({ ok: out.ok, trials: out.trials_total, judged: out.trials_judged,
    accepted: out.trials_accepted, scenes: out.scenes_judged + '/' + out.scenes_total,
    unjudged: out.unjudged_trials, failed: out.failed_trials.slice(0, 6) }, null, 2));
  process.exit(out.ok ? 0 : 1);
}
