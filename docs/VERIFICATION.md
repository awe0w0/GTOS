# Foundation verification record

Verified on 2026-10-06 with GNU g++ 14.2 and QEMU 10.0, GRUB Multiboot v1,
i386 protected mode, and dedicated disposable raw app-store images.

## Deterministic tests

- Memory: 567,156 assertions at both `-O0` and `-O2`, zero failures
- CPU: firmware-table validation/malformed probes, 256-task capacity, task
  lifecycle, boot-context retention, affinity constraints and timer rollover
- App protocol: 31 Python validator/assembler cases, actual i386 VM/game tests
- Storage tools: 10 cases; actual i386 store tests include 34 interrupted/torn
  installs, capacity limits, corruption detection and valid-snapshot fallback
- Storage implementation also passed native AddressSanitizer/UBSan checks

These are focused module tests, not proof of arbitrary-hardware safety.

## Actual QEMU acceptance

`tests/qemu_smoke.py` creates fresh images and verifies:

1. GRUB boots with 32, 64 and 128 MiB; memory self-tests pass
2. One and four firmware processors are identified; scheduler online count is
   honestly one because AP scheduling is not implemented in this milestone
3. Real tasks sleep, wake, yield, return and terminate while the desktop runs
4. A software interrupt at vector 0x80 reaches its registered handler
5. The mouse selects the application list and keyboard selects hardware/apps
6. Catch installs from an external GRUB module onto ATA storage and launches
7. A screenshot-derived paddle position changes in response to arrow-key input
8. Restart, close/relaunch and repeated mouse close/relaunch remain functional
9. Install, removal and reinstall persist across fresh QEMU processes
10. Unformatted media is refused and remains byte-for-byte unchanged after an
    attempted installation

Debug logs, QMP screenshots and disposable disk images are stored in the output
path supplied by the caller. An acceptance failure is a test failure even if the
kernel still boots. The screenshot check needs Pillow; it is not silently skipped.

## Independent review and resolved defects

Review covered loader alignment and undefined direction flag, exception error
frames, per-frame interrupt vectors, interrupt/C++ ABI alignment, GDT limits,
interrupt-disable ordering, syscall registration, PS/2 movement sign handling,
heap bounds/ownership and GUI lifecycle. Fixes also include retaining application
pixels over VM yields, scrolling all eight store entries, selecting replaced apps,
edge-triggered install/remove commands and repeated mouse-close behavior.

## Deliberate limitations

No paging, process/ring-3 isolation, general filesystem, modern compositor,
AHCI/NVMe or AP scheduler is claimed. The VM permits only its bounded instruction
set, but it does not replace hardware-enforced userspace isolation. ATA support
is the tested legacy IDE PIO path. Hardware outside QEMU remains unverified.

## AP startup extension

The optimized AP module additionally passed QEMU one/two/four/eight-CPU boots,
a deliberately nonexistent AP timeout, and a pre-IPI no-APIC rejection case.
APs execute on independent retained stacks, publish identity/stack/checksum
observations atomically and park with interrupts disabled. They are not scheduled.
The integrated desktop acceptance matrix passes with the expected parked counts.
Low-bootstrap allocation adds 486 assertions at each optimization level.

## Paging protection extension

The integrated desktop runs with CR0.PG and CR0.WP set, a null guard and read-only
kernel text/rodata. Independent GRUB/QEMU tests verify exact CR2/error values for
null access, text write, rodata write, unrequested IOAPIC access, access after
unmap and write after protection. Additional cases reject unsafe PAE activation
without changing CR0/CR3, retry after clearing PAE, and support compatible PSE/PGE
bits. Host tests exercise mapping bounds, borrowed-frame ownership, pinned boot
mappings and failure cleanup. These protections are supervisor-only; they do not
create isolated userspace processes or NX permissions.

## High-resolution desktop extension

Actual GRUB/QEMU runs validate the 800×600 RGB desktop, titlebar movement, resize,
maximize/restore, minimize/taskbar restore, launcher search/no-result dismissal,
light/dark pixel changes, external game input/restart, cancellation/confirmation
of uninstall, and installation persistence at 32/64/128 MiB. A separate legacy
ISO verifies that disabling Bochs/QEMU VBE restores the VGA fallback correctly.
The framebuffer ABI is guarded by a byte-encoded GRUB fixture: its aligned color
union starts at112, with a120-byte Multiboot information structure.

## Chinese localization and durable settings extension

Real QEMU tests verify English/Simplified Chinese selection, all nine visible
pinyin candidates, UTF-8 cursor/backspace/insertion, Chinese application search,
localized playable 接球, and language/theme persistence across a full reboot.
Unowned media permits only session-local choices and remains byte-for-byte intact.

Strict UTF-8/catalog/composer/font tests pass i386, native x86-64 and fail-fast
ASan/UBSan. All 154 catalog entries and 359 candidate phrases have verified
coverage in the licensed 440-glyph atlas. Settings tests cover 2,052 torn-update
cases and dirty-cache recovery without a power cycle. App-store recovery adds
1,026 cases, with a flush barrier before reusing the older durable directory and
matching half-range ambiguity checks in the offline tool.
