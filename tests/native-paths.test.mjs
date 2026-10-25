import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const before = fs.readFileSync(control.module.path);
if (hash(before) !== control.module.sha256 || before.length !== control.module.bytes) throw Error('module pin mismatch');
if (fs.existsSync(control.fixture_root)) throw Error('new fixture root required');
fs.mkdirSync(control.fixture_root, { recursive: true });
const actual = await import(pathToFileURL(control.module.path).href);
const context = { report_root: control.fixture_root, work_root: '/var/tmp/native-path-unit-work', source_root: '/var/tmp/native-path-unit-source', tools: { as: '/usr/bin/x86_64-linux-gnu-as' }, native_choices: {} };
const results = [];
function check(name, pass, detail = null) { results.push({ name, pass: !!pass, detail }); if (!pass) throw Error(name); }
function refuses(name, fn) { let error = null; try { fn(); } catch (e) { error = String(e.message); } check(name, typeof error === 'string', error); }

check('work root maps to UNC', actual.nativeHostPath(context, context.work_root) === '//wsl$/Ubuntu-20.04' + context.work_root);
check('output parent maps to UNC', actual.nativeHostPath(context, context.work_root + '/objects/sipi-02') === '//wsl$/Ubuntu-20.04' + context.work_root + '/objects/sipi-02');
check('source maps to UNC', actual.nativeHostPath(context, context.source_root + '/view/a.s') === '//wsl$/Ubuntu-20.04' + context.source_root + '/view/a.s');
for (const value of ['relative/path', '/var/tmp/elsewhere', context.work_root + '/../escape', context.work_root + '/a/../b', context.work_root + '\\objects', 'F:/var/tmp/escape', context.work_root + '\0x']) refuses('invalid native path ' + JSON.stringify(value), () => actual.nativeHostPath(context, value));
refuses('nested source root refused', () => actual.nativeHostPath({ ...context, source_root: context.work_root + '/view' }, context.work_root));
refuses('unmapped Linux host I/O refused', () => actual.ensureHostDirectory(context, context.work_root + '/must-not-be-created'));
refuses('drive-root misrouting refused', () => actual.ensureHostDirectory(context, 'F:/var/tmp/native-path-unit-work/objects/sipi-01'));
refuses('adjacent report root refused', () => actual.assertHostWritePath(context, context.report_root + '-other/a'));
refuses('other distro refused', () => actual.assertHostWritePath(context, '//wsl$/Other-Distro' + context.work_root));
const realDirectory = path.join(control.fixture_root, '空 格 valid-name');
actual.ensureHostDirectory(context, realDirectory);
check('actual directory created by actual module', fs.statSync(realDirectory).isDirectory());
const occupied = path.join(control.fixture_root, 'occupied-file');
fs.writeFileSync(occupied, 'preserve bytes', { flag: 'wx' });
refuses('existing file is not a directory', () => actual.ensureHostDirectory(context, occupied));
check('existing file unchanged', fs.readFileSync(occupied, 'utf8') === 'preserve bytes');
for (const stage of ['sipi-01', 'sipi-02']) {
  const output = context.work_root + '/objects/' + stage + '/cpu_startup_trampoline.asm.o';
  context.native_choices[stage] = { category: 'owner_assembly', cwd: context.source_root + '/view', source: 'src/hardwarecommunication/cpu_startup_trampoline.s', linux_output: output, argv: [context.tools.as, '--32', '-o', output, 'src/hardwarecommunication/cpu_startup_trampoline.s'] };
}
const one = actual.deriveAssemblyRequest(context, 'sipi-01'), two = actual.deriveAssemblyRequest(context, 'sipi-02');
check('first stage derives first output', one.argv[3] === context.native_choices['sipi-01'].linux_output);
check('second stage derives distinct output', two.argv[3] === context.native_choices['sipi-02'].linux_output && two.argv[3] !== one.argv[3]);
check('derived output host is UNC', two.host_output === '//wsl$/Ubuntu-20.04' + two.argv[3]);
one.argv[3] = 'changed caller array';
check('caller cannot mutate authority argv through result', context.native_choices['sipi-01'].argv[3] === context.native_choices['sipi-01'].linux_output);
refuses('unknown stage refused', () => actual.deriveAssemblyRequest(context, 'sipi-99'));
const stale = structuredClone(context);stale.native_choices['sipi-02'].argv = stale.native_choices['sipi-01'].argv.slice();
refuses('old stage argv copied into new stage refused', () => actual.deriveAssemblyRequest(stale, 'sipi-02'));
const wrongSource = structuredClone(context);wrongSource.native_choices['sipi-01'].argv[4] = '../elsewhere.s';
refuses('source mismatch refused', () => actual.deriveAssemblyRequest(wrongSource, 'sipi-01'));
const wrongTool = structuredClone(context);wrongTool.native_choices['sipi-01'].argv[0] = '/usr/bin/other';
refuses('tool mismatch refused', () => actual.deriveAssemblyRequest(wrongTool, 'sipi-01'));
check('tested module unchanged', hash(fs.readFileSync(control.module.path)) === control.module.sha256);
fs.writeFileSync(control.result_path, JSON.stringify({ kind: 'actual_native_paths_unit', passed: true, ok: true, assertions: results.length, failures: 0, module: control.module, fixture_root: control.fixture_root, results, native_commands_started: 0, writes_outside_fixture: false }, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ passed: true, assertions: results.length, native_commands_started: 0 }));
