// GTOS-DSH-114 T0: the production static-image gate and the selected image identity.
//
// Two subjects, both the real production modules:
//   references/static-image-114.mjs   the ELF32 reader the gate uses
//   references/image-identity-114.mjs the identity chain from materials to guest request
//
// The method is the one the 113 review demanded: build a fixture that fully passes, confirm it passes, then break
// exactly one obligation and assert that the refusal names that obligation. A fixture that fails for an earlier reason
// proves nothing, so the expected message is matched rather than merely "it threw".
//
// Every fixture byte lives under the TEST_ONLY root this control names. Nothing here writes into the business
// namespace, and the business namespace is snapshotted before and after to prove it.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const control = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
if (control.task !== 'GTOS-DSH-114') throw new Error('control is for ' + control.task);

const R = control.report_root;
const TO = control.test_only_root;
if (fs.existsSync(TO)) throw new Error('the TEST_ONLY root must not exist yet: ' + TO);
fs.mkdirSync(TO, { recursive: true });

const staticImage = await import('file:///' + control.static_image_module);
const identityModule = await import('file:///' + control.identity_module);

const results = [];
let failed = 0;
function check(name, ok, detail) {
  results.push({ name, ok: Boolean(ok), detail: detail === undefined ? null : detail });
  if (!ok) failed += 1;
}
function refuses(name, fn, matcher) {
  let error = null;
  try { fn(); } catch (e) { error = String(e && e.message ? e.message : e); }
  const ok = error !== null && (!matcher || matcher.test(error));
  check(name, ok, error === null ? 'did not refuse' : error.slice(0, 180));
  return error;
}

// ================================================================ the fixture image
// A minimal image that satisfies every obligation the production reader enforces, laid out so each obligation can be
// broken independently. The layout is deliberate: the load segment maps 0x100000..0x102000 from file offset 0x1000.
const LAYOUT = {
  entry: 0x100900,        // loader, inside the executable load segment
  textVaddr: 0x100000, textSize: 0x1000, textOffset: 0x1000,
  dataVaddr: 0x101000, dataSize: 4, dataOffset: 0x2000,
  strtabOffset: 0x2004, strtabSize: 0x40,
  symtabOffset: 0x2100,
  shstrtabOffset: 0x2200, shstrtabSize: 0x30,
  shoff: 0x2300,
  kernelStart: 0x100000, readonlyEnd: 0x101000, kernelEnd: 0x102000,
  startCtors: 0x101000, endCtors: 0x101004, ctorTarget: 0x100800,
  multibootOffset: 0x100,
};

function buildImage(mutate) {
  const total = 0x2400;
  const b = Buffer.alloc(total);
  const strings = Buffer.from('\0kernel_start\0kernel_readonly_start\0kernel_readonly_end\0kernel_end\0'
    + 'start_ctors\0end_ctors\0loader\0ctor_fn\0', 'utf8');
  const shstrings = Buffer.from('\0.text\0.data\0.symtab\0.strtab\0.shstrtab\0', 'utf8');
  const symName = (name) => {
    const at = strings.indexOf(Buffer.from('\0' + name + '\0', 'utf8'));
    if (at < 0) throw new Error('fixture string missing: ' + name);
    return at + 1;
  };

  // ELF header
  b.write('\x7fELF', 0, 'binary');
  b[4] = 1; b[5] = 1; b[6] = 1;
  b.writeUInt16LE(2, 16);            // ET_EXEC
  b.writeUInt16LE(3, 18);            // i386
  b.writeUInt32LE(1, 20);            // version
  b.writeUInt32LE(LAYOUT.entry, 24);
  b.writeUInt32LE(52, 28);           // phoff
  b.writeUInt32LE(LAYOUT.shoff, 32);
  b.writeUInt16LE(52, 40);           // ehsize
  b.writeUInt16LE(32, 42);           // phentsize
  b.writeUInt16LE(1, 44);            // phnum
  b.writeUInt16LE(40, 46);           // shentsize
  b.writeUInt16LE(6, 48);            // shnum
  b.writeUInt16LE(5, 50);            // shstrndx

  // One load segment covering both .text and .data, writable and executable so both sections map.
  const ph = 52;
  b.writeUInt32LE(1, ph);                              // PT_LOAD
  b.writeUInt32LE(LAYOUT.textOffset, ph + 4);
  b.writeUInt32LE(LAYOUT.textVaddr, ph + 8);
  b.writeUInt32LE(LAYOUT.textVaddr, ph + 12);
  b.writeUInt32LE(LAYOUT.textSize + LAYOUT.dataSize, ph + 16);
  b.writeUInt32LE(LAYOUT.textSize + LAYOUT.dataSize, ph + 20);
  b.writeUInt32LE(7, ph + 24);                         // RWX
  b.writeUInt32LE(0x1000, ph + 28);

  // Multiboot header with the original flags and a valid checksum.
  const magic = 0x1BADB002;
  b.writeUInt32LE(magic, LAYOUT.multibootOffset);
  b.writeUInt32LE(7, LAYOUT.multibootOffset + 4);
  b.writeUInt32LE(((0 - (magic + 7)) >>> 0), LAYOUT.multibootOffset + 8);

  // .text: a byte at the constructor target and at the loader entry.
  b[LAYOUT.textOffset + (LAYOUT.ctorTarget - LAYOUT.textVaddr)] = 0xC3;
  b[LAYOUT.textOffset + (LAYOUT.entry - LAYOUT.textVaddr)] = 0xC3;

  // .data: the single constructor slot.
  b.writeUInt32LE(LAYOUT.ctorTarget, LAYOUT.dataOffset);

  strings.copy(b, LAYOUT.strtabOffset);
  shstrings.copy(b, LAYOUT.shstrtabOffset);

  // Symbols: null, six script symbols, loader, and one function the constructor points at.
  const syms = [
    { name: 0, value: 0, size: 0, info: 0x00, section: 0 },
    { name: symName('kernel_start'), value: LAYOUT.kernelStart, size: 0, info: 0x10, section: 1 },
    { name: symName('kernel_readonly_start'), value: LAYOUT.kernelStart, size: 0, info: 0x10, section: 1 },
    { name: symName('kernel_readonly_end'), value: LAYOUT.readonlyEnd, size: 0, info: 0x10, section: 1 },
    { name: symName('kernel_end'), value: LAYOUT.kernelEnd, size: 0, info: 0x10, section: 1 },
    { name: symName('start_ctors'), value: LAYOUT.startCtors, size: 0, info: 0x10, section: 2 },
    { name: symName('end_ctors'), value: LAYOUT.endCtors, size: 0, info: 0x10, section: 2 },
    { name: symName('loader'), value: LAYOUT.entry, size: 0, info: 0x10, section: 1 },
    { name: symName('ctor_fn'), value: LAYOUT.ctorTarget, size: 0, info: 0x12, section: 1 },
  ];
  syms.forEach((s, i) => {
    const at = LAYOUT.symtabOffset + i * 16;
    b.writeUInt32LE(s.name, at);
    b.writeUInt32LE(s.value, at + 4);
    b.writeUInt32LE(s.size, at + 8);
    b[at + 12] = s.info;
    b.writeUInt16LE(0, at + 13);
    b.writeUInt16LE(s.section, at + 14);
  });

  // Section headers: null, .text, .data, .symtab, .strtab, .shstrtab.
  const shName = (name) => {
    const at = shstrings.indexOf(Buffer.from('\0' + name + '\0', 'utf8'));
    if (at < 0) throw new Error('fixture section string missing: ' + name);
    return at + 1;
  };
  const sections = [
    { name: 0, type: 0, flags: 0, address: 0, offset: 0, size: 0, link: 0, info: 0, entrySize: 0 },
    { name: shName('.text'), type: 1, flags: 6, address: LAYOUT.textVaddr, offset: LAYOUT.textOffset,
      size: LAYOUT.textSize, link: 0, info: 0, entrySize: 0 },
    { name: shName('.data'), type: 1, flags: 3, address: LAYOUT.dataVaddr, offset: LAYOUT.dataOffset,
      size: LAYOUT.dataSize, link: 0, info: 0, entrySize: 0 },
    { name: shName('.symtab'), type: 2, flags: 0, address: 0, offset: LAYOUT.symtabOffset,
      size: syms.length * 16, link: 4, info: 1, entrySize: 16 },
    { name: shName('.strtab'), type: 3, flags: 0, address: 0, offset: LAYOUT.strtabOffset,
      size: strings.length, link: 0, info: 0, entrySize: 0 },
    { name: shName('.shstrtab'), type: 3, flags: 0, address: 0, offset: LAYOUT.shstrtabOffset,
      size: shstrings.length, link: 0, info: 0, entrySize: 0 },
  ];
  sections.forEach((s, i) => {
    const at = LAYOUT.shoff + i * 40;
    b.writeUInt32LE(s.name, at);
    b.writeUInt32LE(s.type, at + 4);
    b.writeUInt32LE(s.flags, at + 8);
    b.writeUInt32LE(s.address, at + 12);
    b.writeUInt32LE(s.offset, at + 16);
    b.writeUInt32LE(s.size, at + 20);
    b.writeUInt32LE(s.link, at + 24);
    b.writeUInt32LE(s.info, at + 28);
    b.writeUInt32LE(0, at + 32);
    b.writeUInt32LE(s.entrySize, at + 36);
  });

  if (mutate) mutate({ buffer: b, syms, sections, layout: LAYOUT, strings, shstrings });
  return b;
}

const expectations = {
  expectedKernelStart: LAYOUT.kernelStart,
  expectedConstructors: ['ctor_fn'],
};
const inspect = (buffer, options) => staticImage.inspectStaticImage(buffer, Object.assign({}, expectations, options || {}));

// ================================================================ 1. the positive
{
  const image = buildImage();
  const fixture = path.join(TO, 'positive.elf');
  fs.writeFileSync(fixture, image);
  const inspection = inspect(image);
  check('the fixture image passes the production reader', inspection.bytes === image.length,
    inspection.bytes);
  check('the fixture reports its entry', inspection.entry === LAYOUT.entry, inspection.entry);
  check('the fixture carries one constructor', inspection.constructors.length === 1,
    inspection.constructors.length);
  check('the fixture constructor resolves to the declared symbol',
    inspection.constructors[0].symbols.includes('ctor_fn'), inspection.constructors[0].symbols);
  check('the fixture reports the script symbols', inspection.script_symbols.kernel_start === LAYOUT.kernelStart,
    inspection.script_symbols);
  check('the fixture reports no undefined symbols', inspection.undefined_symbols.length === 0,
    inspection.undefined_symbols);
  check('the fixture records that nothing was executed', inspection.constructors_executed === false
    && inspection.target_execution === false, [inspection.constructors_executed, inspection.target_execution]);
}

// ================================================================ 2. header obligations
{
  refuses('a non-ELF file is refused', () => inspect(Buffer.from('not an elf at all'.repeat(8))), /ELF32/);
  refuses('a truncated header is refused', () => inspect(Buffer.alloc(20)), /image file range/);
  refuses('a relocatable object is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt16LE(1, 16));
    inspect(b);
  }, /ELF32 little-endian i386 executable/);
  refuses('a non-i386 machine is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt16LE(62, 18));
    inspect(b);
  }, /ELF32 little-endian i386 executable/);
  refuses('an interpreter segment is refused', () => {
    const b = buildImage((m) => {
      // A second program header would need phnum raised; instead turn the load into PT_INTERP.
      m.buffer.writeUInt32LE(3, 52);
    });
    inspect(b);
  }, /no dynamic loader or interpreter/);
  refuses('a dynamic segment is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(2, 52));
    inspect(b);
  }, /no dynamic loader or interpreter/);
  refuses('an unbounded program header table is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt16LE(0, 44));
    inspect(b);
  }, /bounded image header tables/);
  refuses('an oversized section table is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt16LE(9000, 48));
    inspect(b);
  }, /bounded image header tables/);
}

// ================================================================ 3. the undefined-symbol rule
// This is the obligation the 113 review found under-covered. An unresolved symbol is identified by its section index,
// not by its name or binding, so a nameless or LOCAL undefined symbol must be refused too.
{
  refuses('an undefined GLOBAL symbol is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt16LE(0, at + 14);   // ctor_fn becomes undefined
    });
    inspect(b);
  }, /unresolved image symbol/);

  refuses('an undefined symbol with an EMPTY name is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt32LE(0, at);        // no name
      m.buffer.writeUInt16LE(0, at + 14);   // undefined
    });
    inspect(b);
  }, /unresolved image symbol/);

  refuses('an undefined symbol with LOCAL binding is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt16LE(0, at + 14);   // undefined
      m.buffer[at + 12] = 0x02;             // LOCAL FUNC
    });
    inspect(b);
  }, /unresolved image symbol/);

  refuses('an undefined symbol with a nonzero value is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt16LE(0, at + 14);   // undefined
      m.buffer.writeUInt32LE(0x1234, at + 4);
    });
    inspect(b);
  }, /unresolved image symbol/);

  refuses('a symbol in a reserved section index is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt16LE(0xff00, at + 14);
    });
    inspect(b);
  }, /SHN_ABS\/SHN_COMMON|image symbol section index/);

  refuses('a COMMON symbol is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 8 * 16;
      m.buffer.writeUInt16LE(0xfff2, at + 14);
    });
    inspect(b);
  }, /COMMON/);

  refuses('a symbol table whose entry size is wrong is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(7, LAYOUT.shoff + 3 * 40 + 36));
    inspect(b);
  }, /symbol table shape/);
}

// ================================================================ 4. script symbols and boundaries
{
  refuses('a kernel start that is not the declared start is refused', () => {
    const b = buildImage();
    inspect(b, { expectedKernelStart: 0x200000 });
  }, /kernel start address/);
  refuses('a non-page-aligned read-only end is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 3 * 16;
      m.buffer.writeUInt32LE(0x101001, at + 4);
    });
    inspect(b);
  }, /page-aligned kernel boundaries/);
  refuses('a kernel end below the read-only end is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 4 * 16;
      m.buffer.writeUInt32LE(0x100000, at + 4);
    });
    inspect(b);
  }, /page-aligned kernel boundaries/);
  refuses('an entry that is not the loader definition is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(LAYOUT.entry + 4, 24));
    inspect(b);
  }, /entry equals real loader definition/);
  refuses('a missing loader symbol is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 7 * 16;
      m.buffer.writeUInt16LE(0, at + 14);
    });
    inspect(b);
  }, /unresolved|one GLOBAL image symbol required: loader/);
  refuses('a duplicated global script symbol is refused', () => {
    const b = buildImage((m) => {
      // Two symbols now claim kernel_start, so the single-definition rule fires before any name lookup succeeds.
      const at = LAYOUT.symtabOffset + 7 * 16;
      m.buffer.writeUInt32LE(m.syms[1].name, at);
    });
    inspect(b);
  }, /one GLOBAL image symbol required/);
}

// ================================================================ 5. constructors
{
  refuses('a constructor slot pointing outside every load segment is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(0x00500000, LAYOUT.dataOffset));
    inspect(b);
  }, /suitable file-backed load segment/);
  refuses('a constructor slot pointing at data inside the load segment is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(LAYOUT.dataVaddr, LAYOUT.dataOffset));
    inspect(b);
  }, /constructor target has a real function definition/);
  refuses('a constructor slot with no function definition is refused', () => {
    const b = buildImage((m) => {
      // Point the slot at an address inside .text that no FUNC symbol names.
      m.buffer.writeUInt32LE(LAYOUT.textVaddr + 0x40, LAYOUT.dataOffset);
    });
    inspect(b);
  }, /constructor target has a real function definition/);
  refuses('a declared constructor list of the wrong length is refused', () => {
    const b = buildImage();
    inspect(b, { expectedConstructors: [] });
  }, /exact ordered constructor ownership/);
  refuses('a declared constructor list naming the wrong symbol is refused', () => {
    const b = buildImage();
    inspect(b, { expectedConstructors: ['loader'] });
  }, /exact ordered constructor ownership/);
  refuses('an unbounded constructor range is refused', () => {
    const b = buildImage((m) => {
      const at = LAYOUT.symtabOffset + 6 * 16;
      m.buffer.writeUInt32LE(LAYOUT.startCtors + 0x10000, at + 4);
    });
    inspect(b);
  }, /bounded constructor pointer range/);
  refuses('a fini array with content is refused', () => {
    const b = buildImage((m) => {
      // Append a real name after the existing table and point the .data section at it, so the section is genuinely
      // named .fini_array and genuinely has content.
      const appended = LAYOUT.shstrtabOffset + m.shstrings.length;
      m.buffer.write('.fini_array\0', appended, 'utf8');
      m.buffer.writeUInt32LE(m.shstrings.length, LAYOUT.shoff + 2 * 40);          // section 2 name offset
      m.buffer.writeUInt32LE(m.shstrings.length + 12, LAYOUT.shoff + 5 * 40 + 20); // shstrtab size grows
    });
    inspect(b);
  }, /fini/);
}

// ================================================================ 6. Multiboot
{
  refuses('a wrong Multiboot flag set is refused', () => {
    const b = buildImage((m) => {
      m.buffer.writeUInt32LE(3, LAYOUT.multibootOffset + 4);
      m.buffer.writeUInt32LE(((0 - (0x1BADB002 + 3)) >>> 0), LAYOUT.multibootOffset + 8);
    });
    inspect(b);
  }, /Multiboot/);
  refuses('an invalid Multiboot checksum is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(0, LAYOUT.multibootOffset + 8));
    inspect(b);
  }, /Multiboot/);
  refuses('a missing Multiboot header is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(0, LAYOUT.multibootOffset));
    inspect(b);
  }, /Multiboot/);
}

// ================================================================ 7. string and range bounds
{
  refuses('a string table claiming a range outside the file is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(0xFFFF0000, LAYOUT.shoff + 4 * 40 + 16));
    inspect(b);
  }, /image file range|string table range/);
  refuses('an unterminated symbol name is refused', () => {
    const b = buildImage((m) => {
      // Overwrite the string table's terminator run so 'kernel_start' has no NUL before the table ends.
      const start = LAYOUT.strtabOffset + 1;
      // Every byte after the leading NUL becomes a letter, so no name has a terminator inside the table.
      m.buffer.fill(0x41, start, LAYOUT.strtabOffset + m.strings.length);
    });
    inspect(b);
  }, /unterminated|string/);
  refuses('a symbol pointing past the string table is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(0xFFFF, LAYOUT.symtabOffset + 1 * 16));
    inspect(b);
  }, /string table offset/);
  refuses('a section whose file offset does not match its address is refused', () => {
    const b = buildImage((m) => m.buffer.writeUInt32LE(LAYOUT.textOffset + 4, LAYOUT.shoff + 1 * 40 + 16));
    inspect(b);
  }, /allocated section file mapping|image file range/);
  refuses('a load segment whose file size exceeds its memory size is refused', () => {
    const b = buildImage((m) => {
      m.buffer.writeUInt32LE(0x5000, 52 + 16);
      m.buffer.writeUInt32LE(0x1000, 52 + 20);
    });
    inspect(b);
  }, /segment file size exceeds memory size|image file range/);
}

// ================================================================ 8. the identity chain
// The identity module pins bytes. Its tests use small synthetic members, because validity is the gate's business and
// identity is only about which bytes are which.
const identityDir = path.join(TO, 'identity');
fs.mkdirSync(identityDir, { recursive: true });
const LINUX = '/var/tmp/gtos-dsh-114-e759-workers-view';
const memberImage = path.join(identityDir, 'image.bin');
const memberMap = path.join(identityDir, 'map.txt');
const memberObject = path.join(identityDir, 'kernel.o');
const memberProfile = path.join(identityDir, 'flags.txt');
fs.writeFileSync(memberImage, Buffer.from('image-bytes-v1\n'));
fs.writeFileSync(memberMap, Buffer.from('map-bytes-v1\n'));
fs.writeFileSync(memberObject, Buffer.from('object-bytes-v1\n'));
fs.writeFileSync(memberProfile, Buffer.from('-O2 -m32\n'));

// Every member declares the Linux path the guest is given and the host path the bytes are read from. The unit uses a
// Linux path it will never launch and a host path under TEST_ONLY; what is under test is the binding, not the launch.
const materials = {
  task: control.task,
  image: { linux: LINUX + '/image/provider.elf', host: memberImage },
  map: { linux: LINUX + '/image/provider.map', host: memberMap },
  wave: LINUX + '/wave-01',
  objects: [{ logical: 'kernel', linux: LINUX + '/objects/kernel.o', host: memberObject }],
  profile: { linux: LINUX + '/profile/flags.txt', host: memberProfile },
};
const identity = identityModule.createSelectedIdentity(materials);
check('an identity is created from the materials', typeof identity.identity === 'string'
  && identity.identity.length === 64, identity.identity);
check('the identity records the image bytes', identity.image.sha256 === sha(fs.readFileSync(memberImage)),
  identity.image.sha256);
check('the identity records the map bytes', identity.map.sha256 === sha(fs.readFileSync(memberMap)),
  identity.map.sha256);
check('the identity records both paths for the image',
  identity.image.linux === materials.image.linux && identity.image.host === memberImage, identity.image);
check('the identity records the source wave', identity.wave === materials.wave, identity.wave);
check('the identity records each object', identity.objects.length === 1
  && identity.objects[0].logical === 'kernel', identity.objects);

check('the identity verifies against unchanged bytes',
  identityModule.verifySelectedIdentity(identity).verified === true, null);

refuses('an identity whose image bytes moved is refused', () => {
  fs.writeFileSync(memberImage, Buffer.from('image-bytes-v2\n'));
  identityModule.verifySelectedIdentity(identity);
}, /image bytes are not the selected identity/);
fs.writeFileSync(memberImage, Buffer.from('image-bytes-v1\n'));

refuses('an identity whose map bytes moved is refused', () => {
  fs.writeFileSync(memberMap, Buffer.from('map-bytes-v2\n'));
  identityModule.verifySelectedIdentity(identity);
}, /map bytes are not the selected identity/);
fs.writeFileSync(memberMap, Buffer.from('map-bytes-v1\n'));

refuses('an identity whose object bytes moved is refused', () => {
  fs.writeFileSync(memberObject, Buffer.from('object-bytes-v2\n'));
  identityModule.verifySelectedIdentity(identity);
}, /object bytes moved/);
fs.writeFileSync(memberObject, Buffer.from('object-bytes-v1\n'));

refuses('an identity pointing at an absent host file is refused', () => {
  identityModule.verifySelectedIdentity(identity, {
    image: { linux: materials.image.linux, host: path.join(identityDir, 'absent.bin') },
  });
}, /absent/);

refuses('an identity whose member has no Linux path is refused', () => {
  identityModule.createSelectedIdentity(Object.assign({}, materials, { image: { host: memberImage } }));
}, /absolute Linux path/);

check('the identity verifies again after the bytes are restored',
  identityModule.verifySelectedIdentity(identity).verified === true, null);

// ================================================================ 9. the guest request consumer
// This is the check that would have caught 113: the request must name the selected identity, carry the selected Linux
// path, and the bytes that path resolves to must be the approved bytes.
const goodRequest = {
  identity: identity.identity,
  image: identity.image.linux,
  image_pin: { linux: identity.image.linux, host: identity.image.host,
    bytes: identity.image.bytes, sha256: identity.image.sha256 },
  map_pin: { linux: identity.map.linux, host: identity.map.host,
    bytes: identity.map.bytes, sha256: identity.map.sha256 },
};
const toHost = () => memberImage;
check('a request bound to the selected identity is accepted',
  identityModule.assertRequestUsesSelectedIdentity(goodRequest, identity, { resolve: toHost }).accepted === true,
  null);

refuses('a request with no identity is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(Object.assign({}, goodRequest, { identity: undefined }), identity,
    { resolve: toHost });
}, /different selected identity/);

refuses('a request naming another identity is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(Object.assign({}, goodRequest, { identity: 'f'.repeat(64) }),
    identity, { resolve: toHost });
}, /different selected identity/);

refuses('a request carrying a different Linux image path is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(
    Object.assign({}, goodRequest, { image: LINUX + '/image/other.elf' }), identity, { resolve: toHost });
}, /Linux image path is not the selected one/);

refuses('a request whose image pin is another image is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(Object.assign({}, goodRequest, {
    image_pin: { linux: identity.image.linux, host: memberImage, bytes: 3, sha256: 'a'.repeat(64) },
  }), identity, { resolve: toHost });
}, /request image pin is not the selected image/);

refuses('a request with no image pin is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(Object.assign({}, goodRequest, { image_pin: undefined }),
    identity, { resolve: toHost });
}, /carries no image pin/);

refuses('a request whose map pin is another map is refused', () => {
  identityModule.assertRequestUsesSelectedIdentity(Object.assign({}, goodRequest, {
    map_pin: { linux: identity.map.linux, host: memberMap, bytes: 1, sha256: 'b'.repeat(64) },
  }), identity, { resolve: toHost });
}, /request map pin is not the selected map/);

// The real 113 case: the declared pin still says the approved bytes, but the path now resolves to different bytes.
// The pin alone would pass; the independent byte read is what refuses it.
{
  const swapped = path.join(identityDir, 'image-swapped.bin');
  fs.writeFileSync(swapped, Buffer.from('image-bytes-v2\n'));
  refuses('a path that resolves to different bytes is refused even when the pin is unchanged', () => {
    identityModule.assertRequestUsesSelectedIdentity(goodRequest, identity, { resolve: () => swapped });
  }, /different bytes than the selected identity/);

  // The same bytes behind another host pathname are the same identity and must be accepted.
  const alias = path.join(identityDir, 'image-alias.bin');
  fs.copyFileSync(memberImage, alias);
  check('the same bytes behind another host pathname are accepted',
    identityModule.assertRequestUsesSelectedIdentity(goodRequest, identity, { resolve: () => alias }).accepted
      === true, alias);
}

refuses('an identity with no objects is refused', () => {
  identityModule.createSelectedIdentity(Object.assign({}, materials, { objects: [] }));
}, /at least one object/);
refuses('an identity with a duplicate object role is refused', () => {
  identityModule.createSelectedIdentity(Object.assign({}, materials, {
    objects: [
      { logical: 'kernel', linux: LINUX + '/objects/kernel.o', host: memberObject },
      { logical: 'kernel', linux: LINUX + '/objects/kernel2.o', host: memberObject },
    ],
  }));
}, /duplicate object logical name/);
refuses('an identity with a relative wave is refused', () => {
  identityModule.createSelectedIdentity(Object.assign({}, materials, { wave: 'wave-01' }));
}, /absolute Linux path/);
refuses('an identity whose image is absent is refused', () => {
  identityModule.createSelectedIdentity(Object.assign({}, materials, {
    image: { linux: LINUX + '/image/nope.elf', host: path.join(identityDir, 'nope.bin') },
  }));
}, /absent/);

// ================================================================ 10. the map, parsed rather than searched
{
  const realMap = [
    'Linker script and memory map',
    '',
    '                0x0000000000100000                kernel_start = .',
    '                0x0000000000100000                kernel_readonly_start = .',
    '                0x000000000019d000                kernel_readonly_end = .',
    '                0x000000000019d000                start_ctors = .',
    '                0x000000000019d00c                end_ctors = .',
    '                0x000000000010e230                native_fp_enter_trap',
    '                0x000000000012e4ce                loader',
  ].join('\n');
  const parsed = identityModule.parseMapSymbols(realMap);
  check('the map parser reads the assignment form', parsed.symbols.get('kernel_start') === 0x100000,
    parsed.symbols.get('kernel_start'));
  check('the map parser reads the definition form', parsed.symbols.get('loader') === 0x12e4ce,
    parsed.symbols.get('loader'));
  check('the map parser counts every definition', parsed.definitions === 7, parsed.definitions);

  refuses('a map with no symbol definitions is refused', () => {
    identityModule.parseMapSymbols('nothing here\nat all\n');
  }, /no symbol definitions/);
  refuses('a map defining one symbol at two addresses is refused', () => {
    identityModule.parseMapSymbols('                0x0000000000100000                kernel_start = .\n'
      + '                0x0000000000200000                kernel_start = .\n');
  }, /defined at two addresses/);
  refuses('an empty map is refused', () => identityModule.parseMapSymbols(''), /map text required/);

  // The association the 113 gate lacked: a substring search would pass an address that appears on another line.
  const inspection = inspect(buildImage());
  const mapFor = (pairs) => ({
    symbols: new Map(pairs), definitions: pairs.length, names: pairs.map((p) => p[0]),
  });
  const matching = mapFor([
    ['kernel_start', LAYOUT.kernelStart],
    ['kernel_readonly_start', LAYOUT.kernelStart],
    ['kernel_readonly_end', LAYOUT.readonlyEnd],
    ['kernel_end', LAYOUT.kernelEnd],
    ['start_ctors', LAYOUT.startCtors],
    ['end_ctors', LAYOUT.endCtors],
    ['loader', LAYOUT.entry],
  ]);
  const requiredNames = [...matching.symbols.keys()];
  check('a matching map associates every required symbol',
    identityModule.crossCheckMap(inspection, matching, requiredNames).associated === requiredNames.length, null);

  const wrongAddress = mapFor([
    ['kernel_start', LAYOUT.kernelStart],
    ['kernel_readonly_start', LAYOUT.kernelStart],
    ['kernel_readonly_end', LAYOUT.readonlyEnd],
    ['kernel_end', LAYOUT.kernelEnd],
    ['start_ctors', LAYOUT.startCtors],
    ['end_ctors', LAYOUT.endCtors],
    ['loader', LAYOUT.entry + 0x10],
  ]);
  refuses('a map with one wrong address is refused', () => {
    identityModule.crossCheckMap(inspection, wrongAddress, requiredNames);
  }, /association failed/);

  const missing = mapFor(requiredNames.filter((n) => n !== 'loader').map((n) => [n, matching.symbols.get(n)]));
  refuses('a map missing a required symbol is refused', () => {
    identityModule.crossCheckMap(inspection, missing, requiredNames);
  }, /association failed/);

  refuses('a required symbol absent from the image is refused', () => {
    identityModule.crossCheckMap(inspection, matching, [...requiredNames, 'no_such_symbol']);
  }, /association failed/);
}

// ================================================================ 11. the business namespace is untouched
// The suite reads the business report root and writes only under TEST_ONLY. Both halves are checked.
const businessAttempts = fs.readdirSync(R).filter((n) => /^(attempt|result)-.*\.json$/.test(n)).sort();
const testOnlyFiles = [];
const walk = (dir) => {
  for (const name of fs.readdirSync(dir).sort()) {
    const full = path.join(dir, name);
    if (fs.statSync(full).isDirectory()) { walk(full); continue; }
    testOnlyFiles.push(full);
  }
};
walk(TO);
const toRoot = path.resolve(TO) + path.sep;
check('every fixture file is inside the declared TEST_ONLY subtree',
  testOnlyFiles.every((f) => path.resolve(f).startsWith(toRoot)),
  testOnlyFiles.filter((f) => !path.resolve(f).startsWith(toRoot)).slice(0, 4));
check('the fixture inventory is not empty, so the check above means something', testOnlyFiles.length > 0,
  testOnlyFiles.length);
check('no fixture file carries a business attempt or result name',
  testOnlyFiles.every((f) => !/^(attempt|result)-.*\.json$/.test(path.basename(f))),
  testOnlyFiles.filter((f) => /^(attempt|result)-.*\.json$/.test(path.basename(f))).slice(0, 4));
check('the business attempt and result set is the one the run itself created',
  businessAttempts.length > 0, businessAttempts.length);

const summary = {
  task: control.task, kind: 't0_static_gate_and_identity',
  static_image_module: control.static_image_module,
  static_image_bytes: fs.statSync(control.static_image_module).size,
  static_image_sha256: sha(fs.readFileSync(control.static_image_module)),
  identity_module: control.identity_module,
  identity_module_bytes: fs.statSync(control.identity_module).size,
  identity_module_sha256: sha(fs.readFileSync(control.identity_module)),
  test_only_root: TO,
  fixture_files: testOnlyFiles.length,
  checks: results.length, failed, results,
  ok: failed === 0,
};
const p = control.result_path;
if (fs.existsSync(p)) throw new Error('result exists');
fs.writeFileSync(p, JSON.stringify(summary, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
console.log(JSON.stringify({ ok: summary.ok, checks: summary.checks, failed, fixtures: testOnlyFiles.length }, null, 2));
for (const c of results.filter((x) => !x.ok)) console.log('  FAIL ' + c.name + ' :: ' + JSON.stringify(c.detail));
if (failed) process.exitCode = 1;
