import fs from 'node:fs';
const R = 'F:/GTOS-Chromium/artifacts/native-complete-guest-semantics-and-task-finality-20261028-a';
const d = JSON.parse(fs.readFileSync(R + '/g-matrix-gm144-01-144.json', 'utf8'));
const toHost = (p) => (p.startsWith('/mnt/') ? p.slice(7, 8).toUpperCase() + ':' + p.slice(8) : '//wsl$/Ubuntu-20.04' + p);
for (const scene of ['workers-smp2', 'workers-smp3']) {
  const t = d.trials.find((x) => x.scene === scene && x.trial === 1);
  const txt = fs.readFileSync(toHost(t.serial.path), 'utf8');
  const rows = [];
  let cur = {};
  for (const line of txt.split(/\r?\n/)) {
    const m = line.match(/^WORKER DIAG (PASS|TICKET|SLOT|APIC|VALUE|EXPECTED) ([0-9A-F]{8})$/);
    if (!m) continue;
    cur[m[1]] = parseInt(m[2], 16);
    if (m[1] === 'EXPECTED') { rows.push({ ...cur }); cur = {}; }
  }
  console.log('===', scene, 'rows', rows.length);
  // derive the seed implied by each value for n = 8 + (job%4)
  const sum = (n) => (n * (n + 1)) / 2;
  const derived = rows.map((r, i) => {
    const cand = [];
    for (let job = 0; job < 16; job += 1) {
      const n = 8 + (job % 4);
      if (r.VALUE - sum(n) === r.EXPECTED - sum(n)) cand.push(job);
    }
    return { i, pass: r.PASS, ticket: r.TICKET, slot: r.SLOT, apic: r.APIC, value: r.VALUE, expected: r.EXPECTED, job: cand };
  });
  // try to fit: value = 0x1000*(pass+1) + w*0x100 + job + sum(8+(job%4))
  for (const row of derived) {
    const fits = [];
    for (let w = 0; w < 2; w += 1) for (let job = 0; job < 16; job += 1) {
      const n = 8 + (job % 4);
      const seed = 0x1000 * (row.pass + 1) + w * 0x100 + job;
      if (seed + sum(n) === row.value && seed === row.expected - sum(n)) fits.push(`w${w}j${job}`);
    }
    row.fits = fits;
  }
  for (const r of derived) console.log(`${r.i + 1} t${r.ticket} p${r.pass} slot${r.slot} apic${r.apic} v=${r.value.toString(16)} e=${r.expected.toString(16)} fits=${r.fits.join(',')}`);
}