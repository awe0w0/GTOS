// Read a private i386 image as data. This module never loads or executes it.
export function inspectStaticImage(input, options = {}) {
  if (!(input instanceof Uint8Array) || input.byteLength > 64 * 1024 * 1024) throw Error('bounded image bytes required');
  const b = Buffer.from(input);
  const check = (ok, message) => { if (!ok) throw Error(message); };
  const range = (offset, size) => check(Number.isSafeInteger(offset) && Number.isSafeInteger(size) && offset >= 0 && size >= 0 && offset <= b.length && size <= b.length - offset, 'image file range');
  const end32 = (start, size) => { check(start + size <= 0x100000000, 'image address overflow'); return start + size; };
  range(0, 52);
  check(b.subarray(0, 7).equals(Buffer.from([0x7f, 69, 76, 70, 1, 1, 1])) && b.readUInt16LE(16) === 2 && b.readUInt16LE(18) === 3 && b.readUInt32LE(20) === 1 && b.readUInt16LE(40) === 52, 'ELF32 little-endian i386 executable required');
  const entry = b.readUInt32LE(24), phoff = b.readUInt32LE(28), shoff = b.readUInt32LE(32);
  const phnum = b.readUInt16LE(44), shnum = b.readUInt16LE(48), names = b.readUInt16LE(50);
  check(b.readUInt16LE(42) === 32 && phnum > 0 && phnum <= 64 && b.readUInt16LE(46) === 40 && shnum > 0 && shnum <= 4096 && names < shnum, 'bounded image header tables');
  range(phoff, phnum * 32); range(shoff, shnum * 40);
  const programs = [];
  for (let i = 0; i < phnum; i++) {
    const at = phoff + 32 * i;
    const p = { type: b.readUInt32LE(at), offset: b.readUInt32LE(at + 4), address: b.readUInt32LE(at + 8), physical: b.readUInt32LE(at + 12), filesz: b.readUInt32LE(at + 16), memsz: b.readUInt32LE(at + 20), flags: b.readUInt32LE(at + 24), align: b.readUInt32LE(at + 28) };
    check(p.type !== 2 && p.type !== 3, 'no dynamic loader or interpreter');
    range(p.offset, p.filesz); check(p.filesz <= p.memsz, 'segment file size exceeds memory size');
    end32(p.address, p.memsz); end32(p.physical, p.memsz);
    if (p.type === 1) check(p.align === 0 || p.align === 1 || ((p.align & (p.align - 1)) === 0 && p.offset % p.align === p.address % p.align), 'load alignment');
    programs.push(p);
  }
  const loads = programs.filter(p => p.type === 1 && p.memsz > 0);
  check(loads.length > 0, 'load segment required');
  for (let i = 0; i < loads.length; i++) for (let j = i + 1; j < loads.length; j++) check(loads[i].address + loads[i].memsz <= loads[j].address || loads[j].address + loads[j].memsz <= loads[i].address, 'overlapping load addresses');
  const fileOffset = (address, size, flags = 0) => {
    end32(address, size);
    const p = loads.find(p => address >= p.address && address - p.address <= p.filesz && size <= p.filesz - (address - p.address) && (p.flags & flags) === flags);
    check(p, 'address is not in a suitable file-backed load segment');
    const offset = p.offset + address - p.address; range(offset, size); return offset;
  };
  const sections = [];
  for (let i = 0; i < shnum; i++) {
    const at = shoff + i * 40;
    const s = { index: i, nameOffset: b.readUInt32LE(at), type: b.readUInt32LE(at + 4), flags: b.readUInt32LE(at + 8), address: b.readUInt32LE(at + 12), offset: b.readUInt32LE(at + 16), size: b.readUInt32LE(at + 20), link: b.readUInt32LE(at + 24), info: b.readUInt32LE(at + 28), entrySize: b.readUInt32LE(at + 36) };
    if (s.type !== 8) range(s.offset, s.size);
    end32(s.address, s.size); sections.push(s);
  }
  const string = (table, offset) => {
    check(table && table.type === 3 && offset < table.size, 'string table offset');
    const start = table.offset + offset, end = b.indexOf(0, start);
    check(end >= start && end < table.offset + table.size, 'unterminated image string');
    return b.toString('utf8', start, end);
  };
  for (const s of sections) {
    s.name = string(sections[names], s.nameOffset);
    if ((s.flags & 2) && s.size) {
      if (s.type === 8) check(loads.some(p => s.address >= p.address && s.address - p.address <= p.memsz && s.size <= p.memsz - (s.address - p.address)), 'NOBITS outside load memory');
      else check(fileOffset(s.address, s.size) === s.offset, 'allocated section file mapping');
    }
    check(!([4, 9].includes(s.type) && s.size), 'unresolved image relocation section');
  }
  const symbols = [];
  for (const s of sections.filter(s => s.type === 2)) {
    check(s.entrySize === 16 && s.size % 16 === 0 && s.link < shnum && sections[s.link].type === 3, 'image symbol table shape');
    for (let n = 0; n < s.size; n += 16) {
      const at = s.offset + n, section = b.readUInt16LE(at + 14);
      check(section < shnum || section === 0xfff1 || section === 0xfff2, 'image symbol section');
      symbols.push({ raw: string(sections[s.link], b.readUInt32LE(at)), value: b.readUInt32LE(at + 4), binding: b[at + 12] >>> 4, type: b[at + 12] & 15, section });
    }
  }
  check(symbols.length > 0, 'unstripped image symbols required');
  const undefinedSymbols = symbols.filter(s => s.raw && s.section === 0 && (s.binding === 1 || s.binding === 2));
  check(undefinedSymbols.length === 0, 'unresolved strong or weak image symbol');
  check(!symbols.some(s => s.section === 0xfff2), 'unallocated COMMON image symbol');
  const defined = name => {
    const hits = symbols.filter(s => s.raw === name && s.section !== 0 && s.binding === 1);
    check(hits.length === 1, 'one GLOBAL image symbol required: ' + name); return hits[0];
  };
  const scriptNames = ['kernel_start', 'kernel_readonly_start', 'kernel_readonly_end', 'kernel_end', 'start_ctors', 'end_ctors'];
  const script = Object.fromEntries(scriptNames.map(n => [n, defined(n).value]));
  const expectedStart = options.expectedKernelStart ?? 0x100000;
  check(Number.isInteger(expectedStart) && expectedStart >= 0 && expectedStart < 0x100000000 && script.kernel_start === expectedStart && script.kernel_readonly_start === expectedStart, 'kernel start address');
  check(script.kernel_readonly_end >= expectedStart && script.kernel_readonly_end % 4096 === 0 && script.kernel_end >= script.kernel_readonly_end && script.kernel_end % 4096 === 0, 'page-aligned kernel boundaries');
  const loader = defined('loader'); check(loader.section < shnum && loader.section > 0 && loader.value === entry, 'entry equals real loader definition'); fileOffset(entry, 1, 1);
  check(script.start_ctors >= script.kernel_readonly_end && script.end_ctors >= script.start_ctors && script.end_ctors <= script.kernel_end && script.start_ctors % 4 === 0 && (script.end_ctors - script.start_ctors) % 4 === 0 && script.end_ctors - script.start_ctors <= 16384, 'bounded constructor pointer range');
  const count = (script.end_ctors - script.start_ctors) / 4, constructors = [];
  for (let i = 0; i < count; i++) {
    const address = script.start_ctors + 4 * i, at = fileOffset(address, 4, 4), target = b.readUInt32LE(at);
    fileOffset(target, 1, 1);
    const aliases = symbols.filter(s => s.section > 0 && s.section < shnum && s.type === 2 && s.value === target && s.raw).map(s => s.raw);
    check(aliases.length > 0, 'constructor target has a real function definition'); constructors.push({ address, target, symbols: aliases });
  }
  if (options.expectedConstructors !== undefined) {
    const wanted = options.expectedConstructors;
    check(Array.isArray(wanted) && wanted.every(n => typeof n === 'string' && n.length > 0) && new Set(wanted).size === wanted.length && wanted.length === count && wanted.every((n, i) => constructors[i].symbols.includes(n)), 'exact ordered constructor ownership');
  }
  check(!sections.some(s => s.name.startsWith('.fini_array') && s.size > 0), 'expected linker script discards fini array');
  const multiboot = [];
  for (let at = 0; at + 12 <= Math.min(8192, b.length); at += 4) if (b.readUInt32LE(at) === 0x1badb002 && ((b.readUInt32LE(at) + b.readUInt32LE(at + 4) + b.readUInt32LE(at + 8)) >>> 0) === 0) multiboot.push({ offset: at, flags: b.readUInt32LE(at + 4) });
  check(multiboot.length === 1 && multiboot[0].flags === 7, 'one aligned Multiboot v1 header with original flags and checksum');
  const stacks = programs.filter(p => p.type === 0x6474e551);
  return { bytes: b.length, elf_type: 2, machine: 3, entry, programs, sections, script_symbols: script, constructors, multiboot: multiboot[0], undefined_symbols: [], executable_stack_requested: stacks.length === 1 ? !!(stacks[0].flags & 1) : null, writable_executable_loads: loads.filter(p => (p.flags & 3) === 3).length, final_initializer_addresses_validated: true, constructors_executed: false, target_execution: false, init_ready: 'unknown', services_runtime_ready: 'unknown' };
}
