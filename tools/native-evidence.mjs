// Checks captured tool evidence. It never starts a process or reads an output file.
export const NATIVE_KINDS = Object.freeze([
  'native_metadata', 'object_compile', 'relocatable_link', 'symbol_demangle',
]);
export const PROBE_KEYS = Object.freeze([
  'inside_work_root_write_allowed', 'loader_readable', 'negative_cat_denied',
  'negative_true_denied', 'outside_source_write_denied', 'outside_tmp_write_denied',
  'positive_python_allowed', 'private_shm_write_allowed',
]);

const argvEqual = (a, b) => Array.isArray(a) && Array.isArray(b)
  && a.length === b.length && a.every((value, i) => typeof value === 'string' && value === b[i]);
const validPin = p => p !== null && typeof p === 'object'
  && Number.isSafeInteger(p.bytes) && p.bytes >= 0
  && typeof p.sha256 === 'string' && /^[0-9a-f]{64}$/.test(p.sha256);
const absent = value => value === null || value === undefined;

export function inspectNativeEvidence(evidence) {
  const e = evidence || {};
  const expected = e.expected || {};
  const inner = e.inner || {};
  const child = inner.child || {};
  const products = e.products || {};
  const checks = [];
  const check = (name, ok) => checks.push({ name, ok: ok === true });
  const objectRequired = e.kind === 'object_compile' || e.kind === 'relocatable_link';
  const depfileRequired = e.kind === 'object_compile';

  check('known native category', NATIVE_KINDS.includes(e.kind));
  check('nonempty expected argv', Array.isArray(expected.argv) && expected.argv.length > 0
    && expected.argv.every(value => typeof value === 'string'));
  check('spec argv matches expected', argvEqual(e.spec?.cmd, expected.argv));
  check('inner argv matches expected', argvEqual(inner.argv, expected.argv));
  check('child actually started', inner.child !== null && typeof inner.child === 'object');
  check('child exited zero', child.returncode === 0);
  check('child did not time out', child.timed_out === false);
  check('landlock applied', inner.landlock?.ok === true);
  check('all required probes observed true', inner.probe_checks !== null
    && typeof inner.probe_checks === 'object'
    && PROBE_KEYS.every(key => inner.probe_checks[key] === true)
    && Object.values(inner.probe_checks).every(value => value === true));
  check('final process group measured', child.group_measured === true
    && child.group_measured_final === true);
  check('no descendants left', Array.isArray(child.descendants_left)
    && child.descendants_left.length === 0);
  check('no descendant required cleanup', Array.isArray(child.killed) && child.killed.length === 0);
  check('real raw pin matches child', validPin(e.raw)
    && e.raw.requested_path === child.output_path
    && e.raw.bytes === child.output_bytes && e.raw.bytes === child.output_file_bytes
    && e.raw.sha256 === child.output_sha256);

  if (objectRequired) {
    check('object obligation is explicit', typeof expected.output === 'string'
      && expected.output.startsWith('/') && expected.output.endsWith('.o'));
    const outIndex = Array.isArray(expected.argv) ? expected.argv.indexOf('-o') : -1;
    check('argv names the required object', outIndex >= 0
      && expected.argv[outIndex + 1] === expected.output);
    check('required object was read and pinned', validPin(products.object)
      && products.object.bytes > 0 && products.object.requested_path === expected.output);
  } else {
    check('nonproduct category declares no object', absent(expected.output) && absent(products.object));
  }
  if (depfileRequired) {
    check('depfile obligation matches object', typeof expected.output === 'string'
      && typeof expected.depfile === 'string'
      && expected.depfile === expected.output.slice(0, -2) + '.d');
    check('required depfile was read and pinned', validPin(products.depfile)
      && products.depfile.bytes > 0 && products.depfile.requested_path === expected.depfile);
  } else {
    check('category declares no depfile', absent(expected.depfile) && absent(products.depfile));
  }
  return {
    ok: checks.every(row => row.ok),
    kind: e.kind || null,
    output_obligation: { object: objectRequired, depfile: depfileRequired },
    failed: checks.filter(row => !row.ok).map(row => row.name),
    checks,
    target_execution_proved: false,
  };
}
