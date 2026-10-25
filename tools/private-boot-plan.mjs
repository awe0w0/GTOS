// Requests are arrays. The ELF is input data to one emulator, never a host executable.
const need = (ok, text) => { if (!ok) throw new Error('PRIVATE BOOT: ' + text); };
const linux = p => typeof p === 'string' && /^\/(?:[A-Za-z0-9_.-]+\/)*[A-Za-z0-9_.-]+$/.test(p)
  && p.split('/').every(s => s !== '.' && s !== '..');
const same = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => v === b[i]);
export const E99_MARKER = 'PANIC E99 PHASE B02 SAFE DIAGNOSTIC FAILURE';
export const BOOT_PROBES = Object.freeze(['outside_source_write_denied', 'outside_tmp_write_denied',
  'inside_work_root_write_allowed', 'private_shm_write_allowed', 'positive_python_allowed',
  'negative_cat_denied', 'negative_true_denied', 'loader_readable']);

export function bootRequest(context, stage, kind, suppliedArgv) {
  need(context && linux(context.work_root) && context.work_root.startsWith('/var/tmp/')
    && linux(context.source_root) && context.source_root.startsWith('/var/tmp/'), 'canonical native roots');
  const L = context.work_root, S = context.source_root, T = context.toolchain_root;
  need(L !== S && !L.startsWith(S + '/') && !S.startsWith(L + '/'), 'disjoint roots');
  need(linux(T) && T.startsWith('/mnt/f/GTOS-Chromium/toolchains/'), 'fixed toolchain root');
  need(/^[a-z][a-z0-9]*-[0-9]{2}$/.test(stage) && ['version', 'help', 'e99'].includes(kind), 'registered request kind and stage');
  const emulator = T + '/root/usr/bin/qemu-system-i386';
  const loader = T + '/root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2';
  const libraryPath = T + '/root/usr/lib/x86_64-linux-gnu:' + T + '/root/lib/x86_64-linux-gnu';
  const prefix = [loader, '--library-path', libraryPath, emulator];
  const e9 = L + '/vm/' + stage + '/e9.log', serial = L + '/vm/' + stage + '/serial.log';
  const args = kind === 'e99' ? ['-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'qemu32',
    '-smp', '1', '-m', '128M', '-nodefaults', '-no-user-config', '-display', 'none', '-monitor', 'none',
    '-nic', 'none', '-vga', 'none', '-serial', 'file:' + serial,
    '-chardev', 'file,id=e9,path=' + e9, '-device', 'isa-debugcon,iobase=0xe9,chardev=e9',
    '-no-reboot', '-no-shutdown', '-L', T + '/firmware', '-kernel', S + '/image/provider-static.elf',
    '-append', 'legacy live serial bootlog bootlog-fail'] : [kind === 'version' ? '--version' : '-help'];
  const argv = [...prefix, ...args];
  if (suppliedArgv !== undefined) need(same(suppliedArgv, argv), 'exact ordered argv, no extra options');
  return { stage, kind, argv, emulator_argv: [emulator, ...args], cmd0: loader, emulator,
    image: kind === 'e99' ? S + '/image/provider-static.elf' : null,
    outputs: kind === 'e99' ? [e9, serial] : [], e9, serial, cwd: L,
    limit_seconds: kind === 'e99' ? 12 : 20,
    env: { QEMU_MODULE_DIR: T + '/root/usr/lib/x86_64-linux-gnu/qemu' },
    expected_marker: kind === 'e99' ? E99_MARKER : null };
}


export function bootExecSet(context, kind, tools) {
  need(['native_metadata', 'emulator_probe', 'vm_e99_trial'].includes(kind), 'execution category');
  const request = bootRequest(context, 'identity-01', 'version');
  const result = [tools?.python_realpath, tools?.loader];
  if (kind !== 'native_metadata') result.push(request.cmd0, request.emulator);
  need(result.every(linux) && new Set(result).size === result.length, 'exact distinct file EXEC identities');
  return result;
}

export function inspectBootEvidence(input) {
  const checks = [], check = (name, ok) => checks.push({ name, ok: !!ok });
  const r = input?.request, spec = input?.spec, inner = input?.inner, row = input?.row;
  const child = inner?.child;
  check('e99 request only', r?.kind === 'e99' && Array.isArray(r.argv));
  check('current stage identity', spec?.stage === r?.stage && inner?.stage === r?.stage && row?.stage === r?.stage && typeof r?.stage === 'string');
  check('exact guard and inner argv', same(spec?.cmd, r?.argv) && same(inner?.argv, r?.argv) && same(row?.argv, r?.argv));
  check('real child under successful guard', child && inner.refused !== true && row?.refused === false
    && inner.landlock?.ok === true && row.landlock_ok === true);
  check('all eight probes at both levels', BOOT_PROBES.every(p => inner?.probe_checks?.[p] === true && row?.probe_checks?.[p] === true));
  check('inner deadline, not outer transport timeout', child?.timed_out === true && child.returncode === -9
    && row?.exit_code === -9 && row.inner_timed_out === true && row.timed_out === false && row.unshare_rc === 1 && row.ok === false);
  check('two measured empty groups', child?.group_measured === true && child.group_measured_final === true
    && Number.isInteger(child.group_pgid) && child.group_pgid > 0 && Array.isArray(child.descendants_left)
    && child.descendants_left.length === 0 && Array.isArray(child.killed) && child.killed.length === 0
    && row?.group?.measured === true && row.group.final_measured === true && row.group.pgid === child.group_pgid
    && Array.isArray(row.group.descendants_left) && row.group.descendants_left.length === 0
    && Array.isArray(row.group.killed_after) && row.group.killed_after.length === 0);
  check('actual full raw agrees with child', input?.raw && input.raw.requested_path === child?.output_path
    && Number.isInteger(input.raw.bytes) && input.raw.bytes === child.output_bytes && input.raw.bytes === child.output_file_bytes
    && /^[0-9a-f]{64}$/.test(input.raw.sha256) && input.raw.sha256 === child.output_sha256);
  for (const key of ['e9', 'serial']) {
    const b = input?.[key];
    check(key + ' bounded actual log bytes', Buffer.isBuffer(b) && b.length > 0 && b.length <= 65536);
    const lines = Buffer.isBuffer(b) && b.length <= 65536 ? b.toString('utf8').split(/\r?\n/) : [];
    check(key + ' exact E99 diagnostic', lines.includes(E99_MARKER));
    check(key + ' B01 then B02 before E99', lines.indexOf('BOOT B01 HANDOFF') >= 0
      && lines.indexOf('BOOT B02 MEMORY') > lines.indexOf('BOOT B01 HANDOFF')
      && lines.indexOf(E99_MARKER) > lines.indexOf('BOOT B02 MEMORY'));
    check(key + ' no later phase or other panic', !lines.some(s => /^BOOT B(?:0[3-9]|[1-9][0-9])\b/.test(s)
      || (/^PANIC /.test(s) && s !== E99_MARKER) || /^(PHYSICAL SELFTEST|HEAP SELFTEST|DESKTOP READY|NATIVE RUNTIME)/.test(s)));
  }
  const ok = checks.every(c => c.ok);
  return { ok, checks, failed: checks.filter(c => !c.ok).map(c => c.name),
    emulator_process_started: !!child, guard_tool_ok: row?.ok ?? null,
    expected_deadline_kill: child?.timed_out === true && child.returncode === -9,
    early_boot_diagnostic_observed: ok, target_execution_observed: ok ? true : null,
    constructors_execution_inferred_from_validated_path: ok ? true : null,
    failed_diagnostic_does_not_prove_absence_of_guest_execution: true,
    services_runtime_ready: 'unknown', init_ready: 'unknown', full_boot_pass: false };
}
