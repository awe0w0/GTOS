#!/usr/bin/env python3
"""Real guest acceptance for the native window ABI and the CPL3 input copy.

Boots the probe ISO built by tests/native_window_iso_build.sh, drives the real
desktop through QMP (the same PS/2 path a user would use), and proves:

  * the probe's own frame is composed into the framebuffer, pixel for pixel,
  * the real hover, press and release reach the probe through the kernel copy,
  * a real focus change produces exactly one RESET, and a refused read of it
    does not consume it,
  * the desktop's own close request produces exactly one CLOSE, same rule,
  * the object is closed once, the owner is reaped, and the window disappears.

The run is driven by the probe's own stage lines. Nothing here drives the next
piece of input until the probe reports it finished the previous stage, so a
skipped stage cannot be mistaken for a passing one.

    native_window_qemu.py FRESH_OUT ISO

Nothing here reuses a previous disk or evidence directory.
"""
import datetime
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'tools'))

START = 'GTOS NATIVE WINDOW START V1'
READY = 'GTOS NATIVE WINDOW READY V1'
PASS = 'GTOS NATIVE WINDOW PASS CREATE SUBMIT PRESENT INPUT CLOSE V1'
REAPED = 'NATIVE REAPED 00000003'
DESKTOP = 'DESKTOP READY'

# The required check set is fixed HERE, before the run. A check that never gets
# written is a failure, so a partially executed run cannot pass by having a
# short list of true entries.
REQUIRED_CHECKS = (
    'desktop_ready',
    'probe_started',
    'probe_ready',
    'framebuffer_800x600',
    'content_rect_matches_layout',
    'frame_pixels_exact',
    'frame_composed',
    'input_copy_stage',
    'reset_copy_stage',
    'close_copy_stage',
    'probe_pass',
    'reaped',
    'checker_gone',
    'content_changed_after_close',
    'stages_reported',
    'no_runtime_failure',
    'single_pass',
    'probe_exit_zero',
    'native_runtime_pass',
    'ordered_stages',
    'guest_closed',
)

# The three stages the probe must reach, in order, each with its own case count.
STAGES = ('INPUT_COPY_DONE', 'RESET_COPY_DONE', 'CLOSE_COPY_DONE')
STAGE_PREFIX = 'GTOS NATIVE WINDOW STAGE %s V1 '
STAGE_LINE = STAGE_PREFIX + '%d'
PROBE_EXIT = 'BROWSER PROBE EXIT 00000000'

# The framebuffer size the kernel boots the desktop with, and the pointer the
# desktop starts at (ModernDesktop ctor: mouseX = width - 30, mouseY = 16).
SCREEN = (800, 600)
ORIGIN = (SCREEN[0] - 30, 16)

# Mirror of the real layout code, so the click targets are derived from the same
# numbers the kernel uses instead of being hand-copied from a screenshot.
TOP, BOTTOM = 30, 52
WIDTHS = [602, 570, 590, 510, 594, 360, 480, 480, 480, 480]
HEIGHTS = [400, 412, 424, 430, 396, 300, 360, 360, 360, 360]
MIN_WIDTHS = [500, 500, 540, 480, 592, 320, 320, 320, 320, 320]
MIN_HEIGHTS = [360, 350, 398, 398, 380, 240, 240, 240, 240, 240]
NATIVE_SLOT0 = 6  # ModernNative0: Welcome, Applications, Monitor, Settings, Game, Image

CHECKER = ((221, 229, 236), (167, 181, 194))


def clamp(rect, kind):
    """ModernWindowManager::Clamp, including WorkArea()."""
    x, y, w, h = rect
    area_x, area_y = 0, TOP
    area_w, area_h = SCREEN[0], SCREEN[1] - TOP - BOTTOM
    w = max(w, MIN_WIDTHS[kind])
    h = max(h, MIN_HEIGHTS[kind])
    w = min(w, area_w)
    h = min(h, area_h)
    x = max(x, 0)
    y = max(y, area_y)
    x = min(x, area_w - w)
    y = min(y, area_y + area_h - h)
    return (x, y, w, h)


def window_bounds(kind):
    """ModernWindowManager ctor: the slot's initial rectangle."""
    x = (SCREEN[0] - WIDTHS[kind]) // 2 + kind * 12 - 24
    y = TOP + 38 + kind * 15
    return clamp((x, y, WIDTHS[kind], HEIGHTS[kind]), kind)


def content_rect(rect, image=16):
    """ModernDesktop::NativeContentRect for a square 16x16 frame."""
    x, y, w, h = rect
    available_w, available_h = w - 48, h - 124
    scale = min(available_w // image, available_h // image, 6)
    if scale <= 0:
        return (0, 0, 0, 0)
    side = image * scale
    return (x + (w - side) // 2, y + 82 + (available_h - side) // 2, side, side)


WINDOW = window_bounds(NATIVE_SLOT0)
CONTENT = content_rect(WINDOW)
SCALE = CONTENT[2] // 16
CENTRE = (CONTENT[0] + CONTENT[2] // 2, CONTENT[1] + CONTENT[3] // 2)
# The title bar close button occupies [x + w - 40, x + w) of the title strip.
CLOSE = (WINDOW[0] + WINDOW[2] - 20, WINDOW[1] + 19)
# A title-bar point that the Applications window (opened to move the focus away)
# cannot cover, so it still hit-tests to the native window.
CLOSE_VISIBLE = (WINDOW[0] + WINDOW[2] - 8, WINDOW[1] + 12)
# Dock entry 1 is Applications; pressing it opens and focuses it, which takes the
# focus away from the native window through the real window manager.
DOCK_APPS = (68 + 118 + 14, SCREEN[1] - 30)
WALLPAPER = (40, 560)

# The last frame the probe publishes before it waits for input.
PUBLISH_TONE = 64


def source_binding(modules):
    """Hash the files that really execute this run.

    A run is only reproducible if the runner and the harness modules it loaded
    are named by digest. Recording the module file that Python actually imported
    (not a guessed path) is what makes that binding real.
    """
    bound = {}
    for name, module in modules.items():
        path = getattr(module, '__file__', None)
        if not path:
            continue
        data = pathlib.Path(path).read_bytes()
        bound[name] = {'path': str(path), 'bytes': len(data),
                       'sha256': hashlib.sha256(data).hexdigest()}
    return bound


def fail(message):
    print('FAIL: ' + message, file=sys.stderr)
    raise SystemExit(1)


def harden_text(guest):
    """Make the debugcon log readable while QEMU still holds it.

    The evidence tree lives on a Windows-backed mount, where a concurrent read
    of the file QEMU keeps open can fail with ENXIO. Retry instead of treating
    that transient errno as a guest failure.
    """
    log = guest.log

    def text():
        last = None
        for _ in range(200):
            try:
                return log.read_text() if log.exists() else ''
            except OSError as error:
                last = error
                time.sleep(0.05)
        raise RuntimeError('debugcon log unreadable: %r' % (last,))

    guest.text = text
    return guest


def checker_bounds(image):
    """Bounding box of the light checkerboard behind a native frame."""
    xs, ys = [], []
    for y in range(image.height):
        for x in range(image.width):
            if image.getpixel((x, y))[:3] in CHECKER:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)


def screendump(guest, out, name):
    """Save the real framebuffer as PPM plus a PNG copy, and return the image."""
    from PIL import Image
    ppm = (out / (name + '.ppm')).resolve()
    guest.call('screendump', {'filename': str(ppm)})
    time.sleep(0.4)
    image = Image.open(ppm)
    image.load()
    # Nearest-neighbour resize without depending on the Pillow version's enum.
    image.resize((image.width * 2, image.height * 2)).save(out / (name + '.png'))
    return image


def parse_expected_premul():
    """The qualified premultiplied RGBA table, read from the shipped header.

    This is the same table the probe itself compares against after decoding the
    PNG, so the framebuffer oracle is bound to the qualified pixels rather than
    to a second, hand-written decode.
    """
    text = (ROOT / 'apps/png_resource_probe/resource_oracles.h').read_text()
    body = text.split('resource_expected_premul_rgba[1024]={', 1)[1]
    body = body.split('};', 1)[0]
    values = [int(token, 16) for token in re.findall(r'0x([0-9a-fA-F]{2})', body)]
    if len(values) != 1024:
        fail('expected premultiplied table has %d bytes, not 1024' % len(values))
    return values


def expected_framebuffer(premul, tone):
    """ModernDesktop::DrawNativeSlot, applied to the frame the probe published.

    The probe scales every channel by tone/255 before submitting (its own
    `fill`), and the desktop composites each source pixel over the 4x4
    checkerboard with `pixel + (background * (255 - alpha) + 127) / 255`.
    Both are integer operations, so this is an exact prediction, not a
    tolerance-based approximation.
    """
    frame = [(value * tone) // 255 for value in premul]
    pixels = []
    for y in range(16):
        row = []
        for x in range(16):
            at = (y * 16 + x) * 4
            alpha = frame[at + 3]
            background = CHECKER[((x // 4 + y // 4) & 1)]
            row.append(tuple(frame[at + channel] +
                             (background[channel] * (255 - alpha) + 127) // 255
                             for channel in range(3)))
        pixels.append(row)
    return pixels


def verify_frame(image, expected):
    """Every pixel of the content rectangle must match the prediction exactly."""
    mismatches = []
    for y in range(16):
        for x in range(16):
            want = expected[y][x]
            for dy in range(SCALE):
                for dx in range(SCALE):
                    got = image.getpixel((CONTENT[0] + x * SCALE + dx,
                                          CONTENT[1] + y * SCALE + dy))[:3]
                    if got != want:
                        if len(mismatches) < 8:
                            mismatches.append(((CONTENT[0] + x * SCALE + dx,
                                                CONTENT[1] + y * SCALE + dy), want, got))
    return mismatches


class Pointer:
    """Absolute pointer control on top of the guest's relative PS/2 mouse.

    `guest.mouse` sweeps x first and then y, so a waypoint list is honoured in
    order and every intermediate position is one of the points we chose. That is
    what keeps an incidental hover from landing inside the client area while a
    test is trying to keep the queue quiet.
    """

    def __init__(self, guest):
        self.guest = guest
        self.x, self.y = ORIGIN

    def move(self, x, y):
        self.guest.mouse(x - self.x, y - self.y, False)
        self.x, self.y = x, y

    def click(self, x, y):
        self.move(x, y)
        self.guest.mouse(0, 0, True)


def stage_count(text, name):
    match = re.search(re.escape(STAGE_PREFIX % name) + r'(\d+)', text)
    return int(match.group(1)) if match else None


def stage_wait(name):
    return STAGE_PREFIX % name


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out = pathlib.Path(sys.argv[1]).resolve()
    iso = pathlib.Path(sys.argv[2]).resolve()
    if out.exists():
        fail('choose a fresh evidence directory: ' + str(out))
    if not iso.is_file():
        fail('probe ISO not found: ' + str(iso))
    out.mkdir(parents=True)

    import disk as disktool
    import qemu_runtime
    import qemu_smoke

    disk = out / 'apps.img'
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk),
                    '--size-mib', '8'], check=True, capture_output=True)
    disktool.Image(disk, writable=True)
    qemu_smoke.BOOT_ISO = iso

    expected = expected_framebuffer(parse_expected_premul(), PUBLISH_TONE)
    state = {'scope': 'Real guest: native window ABI and CPL3 input copy through the real '
                      'desktop, PS/2 input and the VM ABI',
             'iso': str(iso), 'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
             'geometry': {'window': WINDOW, 'content': CONTENT, 'scale': SCALE,
                          'centre': CENTRE, 'close': CLOSE,
                          'close_visible': CLOSE_VISIBLE, 'dock_apps': DOCK_APPS,
                          'origin': ORIGIN},
             'publish_tone': PUBLISH_TONE,
             'required_stages': list(STAGES),
             'required_checks': list(REQUIRED_CHECKS),
             'sources': source_binding({'runner': sys.modules[__name__],
                                        'qemu_smoke': qemu_smoke,
                                        'qemu_runtime': qemu_runtime,
                                        'disk': disktool}),
             'checks': {}, 'stage_cases': {}, 'terminal': {}, 'exceptions': [],
             'completed': False, 'guest_pass': False}
    checks = state['checks']
    guest = None
    close_error = None
    try:
        guest = harden_text(qemu_smoke.Guest(out, disk, 64, 4, wait_ready=False))
        guest.wait(DESKTOP, 180)
        checks['desktop_ready'] = True
        guest.wait(START, 180)
        checks['probe_started'] = True
        guest.wait(READY, 180)
        checks['probe_ready'] = True

        before = screendump(guest, out, 'before-input')
        checks['framebuffer_800x600'] = (before.width, before.height) == SCREEN
        # The composed frame is drawn over a light checkerboard, so the real
        # client rectangle can be measured from the pixels instead of assumed.
        measured = checker_bounds(before)
        state['measured_content'] = measured
        checks['content_rect_matches_layout'] = measured == CONTENT
        # Exact pixels: the qualified premultiplied table, the tone the probe
        # published, and the desktop's own compositing formula.
        mismatches = verify_frame(before, expected)
        state['pixel_mismatches'] = mismatches[:8]
        checks['frame_pixels_exact'] = not mismatches
        state['sample_content_before'] = before.getpixel(CENTRE)
        state['sample_outside_before'] = before.getpixel(WALLPAPER)
        checks['frame_composed'] = (state['sample_content_before']
                                    != state['sample_outside_before'])

        # ---- stage 1: the real hover, press and release ---------------------
        # The pointer starts on the top panel, so the sweep onto the client area
        # produces a real POINTER_MOVE the desktop routes through the kernel.
        pointer = Pointer(guest)
        pointer.move(CENTRE[0], CENTRE[1])
        pointer.click(CENTRE[0], CENTRE[1])
        guest.wait(stage_wait('INPUT_COPY_DONE'), 90)
        checks['input_copy_stage'] = True

        # ---- stage 2: a real focus change, so exactly one RESET -------------
        # Pressing the dock entry opens and focuses Applications; the window
        # manager then resets the native window that just lost the focus.
        pointer.move(DOCK_APPS[0], DOCK_APPS[1])
        pointer.click(DOCK_APPS[0], DOCK_APPS[1])
        guest.wait(stage_wait('RESET_COPY_DONE'), 90)
        checks['reset_copy_stage'] = True

        # ---- stage 3: the desktop's own close request -----------------------
        # The Applications window covers most of the native title bar, so the
        # click uses a point of the title strip it cannot cover. That press both
        # refocuses the native window and reaches its close button.
        pointer.move(CLOSE_VISIBLE[0], CLOSE_VISIBLE[1])
        pointer.click(CLOSE_VISIBLE[0], CLOSE_VISIBLE[1])
        guest.wait(stage_wait('CLOSE_COPY_DONE'), 90)
        checks['close_copy_stage'] = True

        guest.wait(PASS, 90)
        checks['probe_pass'] = True
        guest.wait(REAPED, 60)
        checks['reaped'] = True

        after = screendump(guest, out, 'after-close')
        state['sample_content_after'] = after.getpixel(CENTRE)
        # The native client rectangle is the only place the desktop paints that
        # checkerboard, so its disappearance is direct evidence the slot is gone.
        # A wallpaper comparison would not be: the Applications window, which
        # still owns the focus, covers this point.
        checks['checker_gone'] = checker_bounds(after) is None
        checks['content_changed_after_close'] = (state['sample_content_after']
                                                != state['sample_content_before'])

        text = guest.text()
        for name in STAGES:
            cases = stage_count(text, name)
            state['stage_cases'][name] = cases
        # Every stage must have reported a positive case count. A stage that ran
        # but executed nothing is a failure, not a pass.
        checks['stages_reported'] = all(
            state['stage_cases'].get(name) for name in STAGES)
        checks['no_runtime_failure'] = 'NATIVE RUNTIME FAIL' not in text
        checks['single_pass'] = text.count('GTOS NATIVE WINDOW PASS') == 1
        # The probe's own CHECK failures surface as a non-zero exit code, so the
        # kernel's report is a second, independent witness to the same run.
        checks['probe_exit_zero'] = PROBE_EXIT in text
        # The kernel only prints this after it has checked the exit codes, the
        # reaped count and the restored free-frame baseline itself.
        checks['native_runtime_pass'] = 'NATIVE RUNTIME PASS' in text
        positions = [text.find(stage_wait(name)) for name in STAGES]
        checks['ordered_stages'] = all(at >= 0 for at in positions) and positions == \
            sorted(positions)
        state['completed'] = True
    except BaseException as error:
        # Record what actually went wrong instead of only a traceback on stderr.
        state['exceptions'].append('%s: %s' % (type(error).__name__, error))
        raise
    finally:
        # The terminal state of the process is evidence in its own right: the
        # argv and PID QEMU was started with, and the code it really exited
        # with. A missing code is reported as such, never guessed.
        if guest is not None:
            state['terminal'] = {
                'argv': getattr(getattr(guest, 'p', None), 'args', None),
                'pid': getattr(getattr(guest, 'p', None), 'pid', None),
                'exit_code': getattr(getattr(guest, 'p', None), 'returncode', None),
            }
            try:
                (out / 'guest.log').write_text(guest.text())
            except Exception as error:  # never mask the real failure
                (out / 'guest-log-error.txt').write_text(repr(error) + '\n')
                state['exceptions'].append('guest.log: %r' % (error,))
            try:
                guest.close()
                checks['guest_closed'] = True
            except Exception as error:
                # A failed shutdown must not hide the results file, but it must
                # also not be swallowed: record it and fail the run.
                close_error = error
                state['exceptions'].append('guest.close: %r' % (error,))
                checks['guest_closed'] = False
            state['terminal']['exit_code'] = getattr(getattr(guest, 'p', None),
                                                     'returncode', None)
        state['checks'] = checks
        # A required check that was never written is a failure, exactly like a
        # false one: an aborted run must never look like a pass.
        missing = [name for name in REQUIRED_CHECKS if name not in checks]
        state['missing_checks'] = missing
        state['guest_pass'] = (state['completed'] and not missing
                               and all(checks[name] for name in REQUIRED_CHECKS))
        state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (out / 'results.json').write_text(json.dumps(state, indent=2) + '\n')

    if close_error is not None:
        fail('guest.close failed: %r' % (close_error,))
    if not state['guest_pass']:
        failed = [name for name in REQUIRED_CHECKS if not checks.get(name)]
        failed += ['missing: ' + name for name in state['missing_checks']]
        fail('guest checks failed: ' + (', '.join(failed) or 'no checks ran'))
    print('guest pass:', out / 'results.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
