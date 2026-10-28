// GTOS-DSH-147: after the repeated-scene set was widened from the six the taskbook names to eight, the two files that
// asserted the old trial count have to assert the new one. Nothing else about them changes: the judge still refuses a
// matrix that omits a trial, and the static check still requires the count to equal scenes plus repeats.
import fs from 'node:fs';

const R = 'F:/GTOS-Chromium/artifacts/native-scene-bound-witness-and-hermetic-consumer-closure-20261028-a';
const HERE = R + '/worktree-147/tools/gtos-dsh-147';

const IMPORT_OLD = "import { SCENES, COLD_REPEAT_SCENES, EXPECTED_SCENE_COUNT, EXPECTED_TRIAL_COUNT } from './scene-contract-147.mjs';";
const IMPORT_NEW = "import { SCENES, COLD_REPEAT_SCENES, REPEATED_SCENES, EXPECTED_SCENE_COUNT, EXPECTED_TRIAL_COUNT } from './scene-contract-147.mjs';";

const edits = [
  {
    file: HERE + '/n-numeric-147.mjs',
    pairs: [
      [IMPORT_OLD, IMPORT_NEW],
      ["results.push({ id: 'contract:trial-count', ok: EXPECTED_TRIAL_COUNT === 26,",
        "results.push({ id: 'contract:trial-count', ok: EXPECTED_TRIAL_COUNT === 28,"],
      ['detail: `${EXPECTED_TRIAL_COUNT} trials: ${SCENES.length} scenes, ${COLD_REPEAT_SCENES.length} of them repeated` });',
        'detail: `${EXPECTED_TRIAL_COUNT} trials: ${SCENES.length} scenes, ${REPEATED_SCENES.length} of them repeated` });'],
    ],
  },
  {
    file: HERE + '/contract-vs-source-147.mjs',
    pairs: [
      [IMPORT_OLD, IMPORT_NEW],
      ['if (EXPECTED_TRIAL_COUNT !== SCENES.length + COLD_REPEAT_SCENES.length) {',
        'if (EXPECTED_TRIAL_COUNT !== SCENES.length + REPEATED_SCENES.length) {'],
      ['detail: `${EXPECTED_TRIAL_COUNT} != ${SCENES.length} + ${COLD_REPEAT_SCENES.length}` });',
        'detail: `${EXPECTED_TRIAL_COUNT} != ${SCENES.length} + ${REPEATED_SCENES.length}` });'],
    ],
  },
];

let bad = 0;
for (const e of edits) {
  let t = fs.readFileSync(e.file, 'utf8');
  for (const [oldText, newText] of e.pairs) {
    if (t.includes(newText)) { console.log('already: ' + e.file.split('/').pop() + ' :: ' + newText.slice(0, 60)); continue; }
    if (!t.includes(oldText)) { console.log('MISSING: ' + e.file.split('/').pop() + ' :: ' + oldText.slice(0, 80)); bad += 1; continue; }
    t = t.replace(oldText, newText);
  }
  fs.writeFileSync(e.file, t);
}
console.log(bad === 0 ? 'edits applied' : 'edits incomplete: ' + bad);
process.exit(bad === 0 ? 0 : 1);
