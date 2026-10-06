# GTOS app packages and Catch

GTOS apps are external `.gtapp` files containing a manifest and executable,
versioned bytecode. The kernel's generic VM provides arithmetic, input, random
numbers, and drawing. It does not contain a Catch game ID, collision handler,
scoring function, or game-specific update loop. `apps/catch.json` implements all
of that behavior and assembles to `apps/catch.gtapp` without changing the kernel.

This is a deliberately small educational application ABI. It is not an ELF
loader, a general filesystem, native process isolation, or a signed app store.
Only packages from sources you trust should be installed. CRC32 detects accidental
corruption; it does not authenticate the author or resist deliberate tampering.

## Build, inspect, and run

From the repository root, using Python 3 and no third-party Python packages:

```sh
python3 tools/package.py build apps/catch.json apps/catch.gtapp
python3 tools/package.py inspect apps/catch.gtapp
python3 tools/package.py inspect apps/catch.gtapp --disassemble
```

The supplied package has 87 instructions and occupies 824 bytes. Builds are
deterministic: labels and comments occupy no bytes, metadata is zero padded, and
there are no timestamps or host paths in a package. A failed build leaves an
existing output untouched. Successful output is replaced atomically. The source
and output paths must be different.

The ISO includes Catch as a GRUB Multiboot module. On the desktop, install the
available package with `I`, then launch the selected installed app with `G`.
Installation requires a valid dedicated GTOS app-store disk. Consult the main
README for the current desktop controls, boot command, and storage attachment.

### Catch controls

- Hold `A` or `D` to move the green paddle left or right
- Catch the falling gold square; the top-right number is your consecutive score
- Each successful catch raises the fall speed, up to a fixed cap
- A miss resets the score and speed, then starts another falling square
- Press `Space` to restart the game; holding it does not repeatedly reset play

The package reads the host's 100 Hz tick counter and updates at most once every
five ticks, giving 20 Hz physics. Drawing happens after updates. If the VM is
paused or the host is delayed, the game performs one update rather than a large
catch-up burst. Motion therefore does not depend on how often the desktop calls
`Step`. Signed tick differences also handle the ordinary 32-bit counter wrap.

### Offline image management

The host disk tool uses an explicit dedicated image format, separate from the
bytecode package format. Create a new image once:

```sh
python3 tools/disk.py create gtos-apps.img
```

`create` refuses an existing path. It does not format a physical drive or replace
an existing image. To inspect an image or verify every installed package:

```sh
python3 tools/disk.py list gtos-apps.img
python3 tools/disk.py check gtos-apps.img
```

Shut down every VM using the image before changing it from the host. The required
`--offline` switch is an acknowledgement of that condition, not a way to stop a
VM or prove it is stopped:

```sh
python3 tools/disk.py install gtos-apps.img apps/catch.gtapp --offline
python3 tools/disk.py uninstall gtos-apps.img catch --offline
```

Installing an existing package ID replaces that app. The current store holds up
to eight installed entries, with sixteen 8192-byte payload slots and alternating
checksummed directory snapshots. A new payload is written and checked before its
directory becomes current. Removed payload bytes are not securely erased. Keep
backups; checksums and copy-on-write metadata do not turn this prototype into a
general-purpose crash-proof filesystem.

## Assembly source

A source file is one JSON object with exactly these fields:

- `id`: 1–23 lowercase ASCII letters, digits, underscores, or hyphens
- `title`: 1–23 printable ASCII characters
- `summary`: 0–39 printable ASCII characters
- `entry`: optional instruction index or label; defaults to index 0
- `code`: an array of instructions, label objects, and optional comment objects

An instruction is an array with an opcode name and exactly its required
operands. Opcode names and register names are case insensitive; the canonical
listing uses lowercase. Registers are `r0` through `r31`. Labels are case
sensitive and match `[A-Za-z_][A-Za-z0-9_]*`. Duplicate labels, duplicate JSON
keys, unknown source fields, invalid operands, and out-of-bounds targets are
errors. Booleans are not accepted as integer operands.

```json
{
  "id": "hello",
  "title": "HELLO GTOS",
  "summary": "AN EXTERNAL BYTECODE APP",
  "entry": "draw",
  "code": [
    {"comment": "Each label names the next instruction."},
    {"label": "draw"},
    ["clear", 1],
    ["movi", "r0", 10],
    ["movi", "r1", 10],
    ["text", "r0", "r1", 0],
    ["yield"],
    ["jmp", "draw"]
  ]
}
```

Use `yield` in a continuing app. A loop that never yields is stopped when it
exhausts the per-step instruction budget. `halt` ends execution. Falling off the
end of the code is a fault, so yielding as the final instruction still needs a
valid instruction to resume into if the app is meant to continue.

## Binary format, version 1

All multibyte integers are little endian. Packages are at least 136 bytes and at
most 8192 bytes. There is no trailing padding or data section. A package consists
of a 128-byte header followed by 1–1008 eight-byte instructions.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 8 | ASCII `GTAPP01` followed by one zero byte |
| 8 | 4 | Format version, 1 |
| 12 | 4 | Header size, 128 |
| 16 | 4 | Exact total package length |
| 20 | 4 | Instruction count |
| 24 | 4 | Entry instruction index, less than the count |
| 28 | 4 | Flags, zero |
| 32 | 24 | ID, NUL terminated and zero padded |
| 56 | 24 | Title, NUL terminated and zero padded |
| 80 | 40 | Summary, NUL terminated and zero padded |
| 120 | 4 | CRC32 of the entire package with these four bytes zeroed |
| 124 | 4 | Reserved, zero |

CRC32 is the standard IEEE polynomial used by Python's `zlib.crc32`; the check
vector `123456789` produces `0xcbf43926`. The stored checksum is an unsigned
32-bit value. Every unused instruction operand is reserved and must be zero.
Unsupported versions, invalid metadata, length mismatches, bad checksums, unknown
opcodes, invalid registers, and invalid branch targets are refused before launch.

Each instruction is the structure `<BBBBi`: one byte each for `opcode`, `a`, `b`,
`c`, then one signed 32-bit immediate. The assembler handles this encoding;
authors normally use the operand forms below.

| Op | Assembly form | Operation |
| ---: | --- | --- |
| 0 | `halt` | Stop the app |
| 1 | `movi ra immediate` | Set register `a` to a signed 32-bit constant |
| 2 | `mov ra rb` | Copy register `b` to `a` |
| 3 | `add ra rb rc` | `a = b + c`, wrapping to 32 bits |
| 4 | `sub ra rb rc` | `a = b - c`, wrapping to 32 bits |
| 5 | `mul ra rb rc` | `a = b * c`, wrapping to 32 bits |
| 6 | `mod ra rb rc` | Signed remainder; divisor zero faults |
| 7 | `and ra rb rc` | Bitwise AND |
| 8 | `lt ra rb rc` | Signed less-than, returning 0 or 1 |
| 9 | `eq ra rb rc` | Equality, returning 0 or 1 |
| 10 | `jmp target` | Jump to instruction index or source label |
| 11 | `jnz ra target` | Jump if register `a` is nonzero |
| 12 | `input ra selector` | Read one host input value |
| 13 | `random ra bound` | Pseudorandom integer in `[0, bound)` |
| 14 | `clear color` | Fill the app canvas |
| 15 | `rect rx ry rw rh color` | Filled rectangle using register-valued geometry |
| 16 | `text rx ry selector` | Draw the package title or summary |
| 17 | `number rx ry rvalue color` | Draw a signed decimal integer |
| 18 | `yield` | Return to the desktop; resume at the next instruction |

For ordinary instructions, register operands occupy `a`, then `b`, then `c`;
the non-register operand occupies the immediate. The exception is `rect`, where
`a`, `b`, and `c` hold the x, y, and width register indices, the immediate's low
byte is the height register index, and its next byte is the palette index. The
upper two immediate bytes are zero.

### Inputs and drawing

`input` selectors:

| Selector | Value |
| ---: | --- |
| 0 | Held-key bitmask |
| 1 | Unsigned host ticks, interpreted in a register as 32-bit signed bits |
| 2 | Canvas width, 272 pixels |
| 3 | Canvas height, 128 pixels |

Held-key bits are left=1, right=2, up=4, down=8, and action=16. The desktop maps
physical keys to these actions. Apps consume held state and implement their own
edge detection if needed, as Catch does for restart.

Colors are palette indices 0–255. The current desktop defines its stable visual
palette at 0–15; Catch uses background 1, divider 3, paddle 8, ball 12, and white
15. `text` selector 0 draws the title; selector 1 draws the summary. Text uses
white (15). It is clipped to the available whole character width and omitted
when its starting position is outside the canvas. The font advances six pixels
per character and occupies eight vertical pixels. `number` is omitted unless
the complete rendered integer fits. Rectangles are clipped to the canvas;
nonpositive dimensions and completely offscreen rectangles draw nothing.

Random bounds must be 1–1,000,000. Random output is deterministic after reset,
pseudorandom, and unsuitable for cryptographic use. For signed remainder,
`-7 mod 3` is `-1`; the `INT32_MIN mod -1` corner case produces zero.

### Execution and resource limits

The VM has 32 signed 32-bit registers, all zeroed on reset. An app has no bytecode
instruction for arbitrary memory access, disk I/O, ports, networking, or native
machine-code execution. A validated package is copied into private VM program
storage before execution. Each `Step` executes at most 2048 instructions. A
budget overrun, division by zero, or program-counter overrun stops the app with
a reported fault. Execution is cooperative on the desktop; it is not a general
preemptive userspace scheduler. The host and VM still run inside the kernel, so
these software bounds are not hardware-enforced process isolation.

## Verification

```sh
python3 -m unittest discover -s tests -p 'package*.py' -v
./tests/package_vm.sh
```

The Python tests cover exact header/CRC layout, every opcode, labels and source
validation, maximum-size packages, malformed binary rejection, CLI failure
behavior, reproducible Catch output, game controls, collisions, scoring, misses,
restart edges, counter wrap, and 5,000 bounded execution steps. Their small
reference interpreter contains generic opcode behavior, not a game simulation.

The C++ integration test includes the actual assembled Catch bytes and links the
real guest `package.cpp` and `vm.cpp`. It checks initial rendering, tick gating,
held controls and edge clamps, restart edges, repeated catches during auto-play,
miss recovery, stop/reset, private code copying, invalid-register rejection, and
instruction-budget faults. It always compiles a freestanding i386 executable and
runs it directly or through `qemu-i386` when supported. On an x86_64 Linux host
that refuses i386 execution, it reports the limitation and executes the same VM
sources through a freestanding native x86_64 harness instead. That fallback
tests C++ behavior; it does not substitute for booting the actual guest in QEMU.
