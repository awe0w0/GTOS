# High-resolution desktop milestone

This is a real graphical shell running in the GTOS kernel, not a mock-up or a
browser UI. It is a foundation toward the modern-desktop roadmap, not a complete
modern operating system. Only the Applications store, monitor, appearance panel,
Welcome panel and external bytecode application are present. No terminal, file
manager, network service, account system or native userspace application is
represented by a nonfunctional icon.

## Display contract

The kernel requests a GRUB Multiboot v1 RGB framebuffer, normally 800 × 600 × 32.
`drivers::Framebuffer::ReadMode` validates the supplied mode before allocation.
The checked ABI follows GRUB's actual naturally aligned Multiboot v1 definition:
the RGB/palette union begins at byte 112 and the structure occupies 120 bytes.
A raw-byte fixture protects this layout; the historical manual diagram showing
RGB at 110 does not match this GRUB implementation.
Supported surface dimensions are at most 1920 × 1080; the desktop requires at
least 640 × 480. The integration can fall back to the existing VGA shell when the
bootloader supplies an unsupported format or allocation fails.

`Configure(mbi, backing, backingPixels)` binds a separately allocated, contiguous
RAM backbuffer. The backbuffer is always packed `0x00RRGGBB`; the video target's
pitch and RGB bit positions/sizes are honored during `Present()`. Only a 32-bit
video word is supported. 16-bit/24-bit, indexed, text, overlapping color masks,
unaligned pitch/address, addresses above 4 GiB, overlapping front/back buffers,
and insufficient backing capacity are rejected. Smaller RGB masks in a 32-bit
word are supported through explicit channel conversion.

All pixel, rectangle, blend and text writes are clipped. Rectangle endpoints use
64-bit arithmetic before intersection, avoiding signed overflow from VM values.
A dirty bounding rectangle limits the video-memory copy. Rendering uses a RAM
backbuffer and a software copy; it is **not** page flipping, GPU acceleration,
vblank synchronization, or a tear-free hardware compositor. Desktop redraws are
capped at one per two 100 Hz ticks; VM frames run at one per five ticks. No measured
frame-time or latency guarantee is claimed.

The application palette is converted to RGB and integer-scaled into its window.
Game drawing has its own 272 × 128 bounded surface and persists between VM draw
operations. A minimized game pauses stepping; an unfocused visible game continues
with zero held input. There is one external VM host. Opening another package
replaces that VM, rather than creating another isolated process.

## Windows and input

- Five managed window kinds, each with open/minimized/maximized state and focus
- Titlebar drag, bounded bottom-right resize, close/minimize/maximize controls
- Exact geometry restoration after maximizing; resize preserves the top-left
- Taskbar indicators and click-to-minimize / click-to-restore
- Launcher with case-insensitive substring search across panels and installed apps
- Application rows scroll to keep the keyboard selection visible, up to eight apps
- Uninstall has a modal confirmation; Cancel/Esc leaves disk data unchanged
- English/Simplified Chinese and light/dark appearance apply immediately and
  persist through the optional transactional settings store; unavailable storage
  is explicitly labeled session-only
- Live physical/heap memory, CPU discovery, BSP scheduling, parked AP, task, disk,
  paging and write-protect data comes from `SystemSnapshot`, not decorative values

The kernel IRQ handlers enqueue input; drawing, filesystem operations and VM
execution happen in the foreground loop. Queue overflow clears held input and
cancels an active drag so a missing release cannot leave the UI stuck.

Keyboard shortcuts:

| Key | Action |
| --- | --- |
| 1 / H | Welcome |
| 2 / M | System monitor |
| 3 | Applications |
| 4 | Appearance |
| L | Open launcher |
| Tab | Cycle nonminimized windows |
| [ | Minimize focused window |
| ] | Maximize / restore focused window |
| Esc | Close focused window; dismiss launcher or cancel removal |
| Up / Down | Select launcher result or application row |
| Enter / G | Launch selected application |
| I | Install boot-media package |
| U | Ask to remove selected application |
| T | Toggle theme while Appearance has focus |
| R | Restart while the external game has focus |

The game consumes ordinary letter keys while focused; Tab and window-management
keys still work. The driver currently exposes translated keys without modifier
state. There is no Alt-Tab, modifier chord API, full IME, clipboard, text-selection,
mouse wheel, touch support or screen-reader accessibility interface yet.
The launcher now has a bounded pinyin composer and scalar-safe UTF-8 caret editing;
see `desktop-localization.md` for its precise scope.

## Typography

`modern_font_data.inc` embeds GTOS Sans, a 4-bit coverage rasterization of DejaVu
Sans 2.37 at 14 and 28 pixels. Large headings use separately rasterized 28-pixel
glyphs rather than enlarging the body glyphs. ASCII characters 32–126 are present;
the UTF-8 painter also uses the licensed GTOS Han atlas for Simplified Chinese.
Coverage is bounded to catalog/dictionary glyphs; unsupported scalars have an
explicit replacement square. Universal font coverage, complex shaping and vector
text remain future work. The original GTOS 5 × 7 font remains in the tiny VM game canvas.

The complete font permission notice is in `desktop-font-license.txt`.
`tests/desktop_font.py` regenerates the raster atlas with Pillow and the installed
DejaVu font. Kernel builds do not require Pillow, FreeType, font files or network
access.

## Verification

`./tests/desktop_test.sh` runs the actual framebuffer, painter, window manager,
desktop, VM and application-store sources with host platform stubs. It exercises:

- Mode rejection, RGB/BGR/small-channel masks, dirty copies and padded scanlines
- Front/back canaries, extreme rectangle/text coordinates and scoped clipping
- 4,000 randomized move/resize operations, focus and minimize/maximize restoration
- Launcher search, no matches, maximum-length query/title paint containment,
  repeat suppression, theme changes and queue overflow
- Actual mouse drag/resize/title controls and taskbar restoration
- Modal/launcher interruption cancels captured drag/resize, including movement
  after dismissal before the physical mouse button is released
- Installing and playing the real Catch package, visibly moving its paddle
- Canceling/confirming removal, remount persistence and an eight-entry app list

Both a freestanding i386 executable (through qemu-i386 where required) and a
native x86-64 executable run. Optional ASan/UBSan runs use
`GTOS_DESKTOP_SANITIZERS=1 ./tests/desktop_test.sh`; LeakSanitizer is disabled
because it cannot operate under the execution sandbox's ptrace restrictions.

`python3 tests/desktop_qemu.py --output <new-directory>` boots the default ISO with
GRUB and interacts solely through QMP keyboard/mouse input. It retains native
800 × 600 PNG/PPM screenshots, debug logs, the test disk and an acceptance trace.
It verifies visible window workflows, launcher search/dismissal, theme pixels,
Catch input/restart, install/removal persistence over reboot, 32/64/128 MiB guests,
and refusal to write an unformatted disk. It must pass on the integrated ISO;
host raster previews alone do not establish boot or hardware-input correctness.

## Remaining work toward the long-term desktop goal

Session restoration, independent application processes and enforced
isolation, a filesystem-backed file manager, a terminal, reliable multi-app IPC,
broader Unicode fonts, general text editing and focus accessibility, richer input handling,
notifications, display-mode changes, refresh synchronization, compositing damage
optimization, and measured responsiveness remain separate milestones. The present
single-address-space compositor and bounded VM are not a security boundary.
