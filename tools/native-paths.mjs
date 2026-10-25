import fs from 'node:fs';
import path from 'node:path';

function linuxPath(value) {
  if (typeof value !== 'string' || !value.startsWith('/var/tmp/') || value.includes('\\') || value.includes('\0') || path.posix.normalize(value) !== value) throw Error('non-canonical native path');
  return value;
}

function roots(context) {
  const work = linuxPath(context.work_root), source = linuxPath(context.source_root);
  if (work === source || work.startsWith(source + '/') || source.startsWith(work + '/')) throw Error('native roots must be disjoint');
  return [work, source];
}

export function nativeHostPath(context, value) {
  const p = linuxPath(value);
  if (!roots(context).some(r => p === r || p.startsWith(r + '/'))) throw Error('native path outside owned roots');
  return '//wsl$/Ubuntu-20.04' + p;
}

export function assertHostWritePath(context, value) {
  if (typeof value !== 'string' || value.includes('\0')) throw Error('invalid host path');
  const text = value.replaceAll('\\', '/');
  if (!/^[A-Za-z]:\//.test(text) && !text.startsWith('//wsl$/Ubuntu-20.04/')) throw Error('native path must be mapped before host I/O');
  const key = path.win32.resolve(text).toLowerCase();
  const allowed = [context.report_root, ...roots(context).map(r => '//wsl$/Ubuntu-20.04' + r)].map(r => path.win32.resolve(r).toLowerCase());
  if (!allowed.some(r => key === r || key.startsWith(r + '\\'))) throw Error('host write outside owned roots');
  return value;
}

export function ensureHostDirectory(context, value) {
  assertHostWritePath(context, value);
  fs.mkdirSync(value, { recursive: true });
  if (!fs.statSync(value).isDirectory()) throw Error('expected a directory');
  return value;
}

export function deriveAssemblyRequest(context, stage) {
  const choice = context.native_choices[stage];
  if (!choice || choice.category !== 'owner_assembly') throw Error('unregistered assembly stage');
  const cmd = choice.argv;
  if (!Array.isArray(cmd) || cmd.length !== 5 || cmd.some(t => typeof t !== 'string')) throw Error('assembly argv shape');
  if (cmd[0] !== context.tools.as || cmd[1] !== '--32' || cmd[2] !== '-o') throw Error('assembly tool or flags mismatch');
  if (cmd[3] !== choice.linux_output || !cmd[3].startsWith(context.work_root + '/objects/')) throw Error('stage output mismatch');
  nativeHostPath(context, cmd[3]);
  if (choice.cwd !== context.source_root + '/view') throw Error('assembly cwd mismatch');
  if (typeof choice.source !== 'string' || path.posix.isAbsolute(choice.source) || choice.source.includes('\\') || choice.source.includes('\0') || path.posix.normalize(choice.source) !== choice.source || choice.source.startsWith('../') || cmd[4] !== choice.source) throw Error('stage source mismatch');
  nativeHostPath(context, choice.cwd + '/' + choice.source);
  return { stage, kind: choice.category, cwd: choice.cwd, argv: cmd.slice(), linux_output: choice.linux_output, host_output: nativeHostPath(context, choice.linux_output) };
}
