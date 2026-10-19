#!/usr/bin/env python3
"""Real guest acceptance for window/front/draft/queue reclaim at Stop and Reap.

Boots the reclaim diagnostic ISO built by tests/native_window_reclaim_build.sh and
drives the real desktop through QMP. The two diagnostic payloads keep the original
fault/peer role contracts and additionally own a live published window with an
unfinished draft when they stop, so the kernel's own StopGroup and Reap are the
things being observed rather than a host-side simulation.

  * the fault payload owns a live window with a draft, then takes its exact user
    page fault; its window is gone at Stop, before Reap runs,
  * the peer payload owns a live window with a draft plus one real routed input
    record, then exits normally; its window is gone at Stop as well,
  * after Reap the page/object accounting is back at the boot baseline, a fresh
    owner can create/publish/close in the freed slot, and both old handles stay
    refused forever,
  * the GUI removal is proved positively, in two independent parts: the kernel
    asks the real window manager whether any native slot is still open, and the
    whole native window rectangle must be back to the exact pre-window frame.

    native_window_reclaim_qemu.py FRESH_OUT ISO

Nothing here reuses a previous disk or evidence directory.
"""
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'tools'))

import native_window_removal as removal  # noqa: E402

DESKTOP = 'DESKTOP READY'
FAULT_WINDOW = 'NATIVE RECLAIM FAULT WINDOW LIVE'
PEER_WINDOW = 'NATIVE RECLAIM PEER WINDOW LIVE'
STOP_PASS = 'GTOS NATIVE RECLAIM STOP PASS V1'
REAP_PASS = 'GTOS NATIVE RECLAIM REAP PASS V1'
RUNTIME_PASS = 'NATIVE RUNTIME PASS'
REAPED = 'NATIVE REAPED 00000003'
PROBE_EXIT = 'BROWSER PROBE EXIT 00000000'
# The kernel asks the real window manager, after the desktop's next Update,
# whether any of the four native slots is still open.
GUI_CLOSED = 'GTOS NATIVE RECLAIM GUI CLOSED V1'
GUI_OPEN = 'GTOS NATIVE RECLAIM GUI OPEN V1'

# The required check set is fixed HERE, before the run. A check that never gets
# written is a failure, so a partially executed run cannot pass by having a
# short list of true entries.
REQUIRED_CHECKS = (
    'desktop_ready',
    'fault_window_live',
    'peer_window_live',
    'framebuffer_800x600',
    'window_visible_before_stop',
    'input_routed_to_peer',
    'stop_pass',
    'reap_pass',
    'reaped',
    'probe_exit_zero',
    'native_runtime_pass',
    # The removal is a positive state, in two independent parts: the window
    # manager reports every native slot closed, and the screen is back to the
    # exact pre-window frame.
    'gui_slots_closed',
    'window_removed_positive',
    'no_reclaim_failure',
    'no_runtime_failure',
    'single_runtime_pass',
    'guest_closed',
)

# The desktop boots at 800x600 and the pointer starts at (width - 30, 16).
SCREEN = (800, 600)
ORIGIN = (SCREEN[0] - 30, 16)

# Mirror of the real layout code. The peer creates its window only after the fault
# payload has been stopped, so it takes the first native slot again and lands on
# exactly the same rectangle a single-window run would use.
TOP, BOTTOM = 30, 52
WIDTHS = [602, 570, 590, 510, 594, 360, 480, 480, 480, 480]
HEIGHTS = [400, 412, 424, 430, 396, 300, 360, 360, 360, 360]
MIN_WIDTHS = [500, 500, 540, 480, 592, 320, 320, 320, 320, 320]
MIN_HEIGHTS = [360, 350, 398, 398, 380, 240, 240, 240, 240, 240]
NATIVE_SLOT0 = 6


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
# A wallpaper-only strip of the native window's rectangle, with independent
# geometric basis for every edge: it starts below the Welcome window and its
# shadow (which reach y=475), ends above the dock (which starts at y=548), stops
# left of the wallpaper lettering (which starts at x=587), and never contains the
# pointer. If the native window is still composited, its own panel covers this
# strip, so the strip is the positive screen evidence for removal.
STRIP = (WINDOW[0], 480, 372, 36)
# The strip is only evidence if it really lies inside the native window's
# rectangle, below the composited frame, and clear of the pointer. A geometry
# change that broke this must fail loudly here rather than quietly weaken the
# screen check.
if not (STRIP[0] >= WINDOW[0] and STRIP[1] >= WINDOW[1]
        and STRIP[0] + STRIP[2] <= WINDOW[0] + WINDOW[2]
        and STRIP[1] + STRIP[3] <= WINDOW[1] + WINDOW[3]
        and STRIP[1] >= CONTENT[1] + CONTENT[3]):
    raise SystemExit('window removal strip is not inside the native window below '
                     'the composited frame: strip=%r window=%r content=%r'
                     % (STRIP, WINDOW, CONTENT))
# A point well away from the window, used to prove the pointer left it.
OUTSIDE = (40, 560)

# The payload paints every pixel with alpha 128, so the composited content is a
# fixed blend of the frame and the light checkerboard. The prediction below is an
# exact integer computation, not a tolerance: it is derived from the payload's own
# fill loop and the desktop's own compositing formula.
CHECKER = ((221, 229, 236), (167, 181, 194))
ALPHA = 128
# The tone the fault payload publishes with (apps/native/reclaim_fault.cpp).
FAULT_TONE = 255
# The tone the peer payload publishes with (apps/native/reclaim_peer.cpp).
PEER_TONE = 96


def fail(message):
    print('FAIL: ' + message, file=sys.stderr)
    raise SystemExit(1)


def harden_text(guest):
    """Make the debugcon log readable while QEMU still holds it."""
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


def reclaim_frame(tone):
    """apps/native/reclaim.h ReclaimFillFrame, in the same integer arithmetic."""
    frame = []
    for pixel in range(256):
        base = 0x30 + pixel % 4
        value = (base * tone) // 255
        value = (value * ALPHA) // 255
        frame.append((value, value, value, ALPHA))
    return frame


def expected_content(tone):
    """ModernDesktop::DrawNativeSlot over the 4x4 checkerboard, exactly."""
    frame = reclaim_frame(tone)
    rows = []
    for y in range(16):
        row = []
        for x in range(16):
            red, green, blue, alpha = frame[y * 16 + x]
            background = CHECKER[((x // 4 + y // 4) & 1)]
            row.append(tuple(channel + (background[index] * (255 - alpha) + 127) // 255
                             for index, channel in enumerate((red, green, blue))))
        rows.append(row)
    return rows


def content_mismatches(image, expected):
    """Every pixel of the content rectangle must match the prediction exactly."""
    return removal.blend_mismatches(image, expected, CONTENT, SCALE)


def removal_blends():
    """The payload frames that must not survive inside the window rectangle."""
    return [('fault', expected_content(FAULT_TONE)),
            ('peer', expected_content(PEER_TONE))]


def screendump(guest, out, name):
    """Save the real framebuffer as PPM plus a PNG copy, and return the image."""
    from PIL import Image
    ppm = (out / (name + '.ppm')).resolve()
    guest.call('screendump', {'filename': str(ppm)})
    time.sleep(0.4)
    image = Image.open(ppm)
    image.load()
    image.resize((image.width * 2, image.height * 2)).save(out / (name + '.png'))
    return image


def source_binding(modules):
    """Hash the files that really execute this run."""
    bound = {}
    for name, module in modules.items():
        path = getattr(module, '__file__', None)
        if not path:
            continue
        data = pathlib.Path(path).read_bytes()
        bound[name] = {'path': str(path), 'bytes': len(data),
                       'sha256': hashlib.sha256(data).hexdigest()}
    return bound


class Pointer:
    """Absolute pointer control on top of the guest's relative PS/2 mouse."""

    def __init__(self, guest):
        self.guest = guest
        self.x, self.y = ORIGIN

    def move(self, x, y):
        self.guest.mouse(x - self.x, y - self.y, False)
        self.x, self.y = x, y

    def click(self, x, y):
        self.move(x, y)
        self.guest.mouse(0, 0, True)


def wait_for(guest, needle, seconds, deadline_text):
    """Wait for a log marker, reporting the real failure instead of a bare timeout."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        text = guest.text()
        if needle in text:
            return text
        if 'PANIC' in text or 'NATIVE RUNTIME FAIL' in text or 'RECLAIM STOP FAIL' in text:
            fail(deadline_text + ': guest reported a failure first\n' + text[-2000:])
        time.sleep(0.02)
    fail(deadline_text + ': never saw %r within %.0f s\n%s' %
         (needle, seconds, guest.text()[-2000:]))


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out = pathlib.Path(sys.argv[1]).resolve()
    iso = pathlib.Path(sys.argv[2]).resolve()
    if out.exists():
        fail('choose a fresh evidence directory: ' + str(out))
    if not iso.is_file():
        fail('reclaim ISO not found: ' + str(iso))
    out.mkdir(parents=True)

    import disk as disktool
    import qemu_runtime
    import qemu_smoke

    disk = out / 'apps.img'
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk),
                    '--size-mib', '8'], check=True, capture_output=True)
    disktool.Image(disk, writable=True)
    qemu_smoke.BOOT_ISO = iso

    state = {'scope': 'Real guest: window/front/draft/queue reclaim at NativeRuntime '
                      'StopGroup and Reap, driven through the real desktop and PS/2',
             'iso': str(iso), 'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
             'geometry': {'window': WINDOW, 'content': CONTENT, 'scale': SCALE,
                          'strip': STRIP,
                          'centre': CENTRE, 'outside': OUTSIDE, 'origin': ORIGIN},
             'required_checks': list(REQUIRED_CHECKS),
             'sources': source_binding({'runner': sys.modules[__name__],
                                        'removal': removal,
                                        'qemu_smoke': qemu_smoke,
                                        'qemu_runtime': qemu_runtime,
                                        'disk': disktool}),
             'checks': {}, 'terminal': {}, 'exceptions': [],
             'completed': False, 'guest_pass': False}
    checks = state['checks']
    guest = None
    close_error = None
    try:
        guest = harden_text(qemu_smoke.Guest(out, disk, 64, 4, wait_ready=False))
        guest.wait(DESKTOP, 180)
        checks['desktop_ready'] = True

        # The fault payload's window is live with a published frame and a draft.
        wait_for(guest, FAULT_WINDOW, 120, 'the fault payload never got a live window')
        checks['fault_window_live'] = True
        before = screendump(guest, out, 'fault-window')
        checks['framebuffer_800x600'] = (before.width, before.height) == SCREEN
        expected = expected_content(FAULT_TONE)
        mismatches = content_mismatches(before, expected)
        state['pixel_mismatches'] = mismatches
        # The exact blend can only appear where the desktop composited this
        # payload's own frame, so a match is direct evidence the window was there.
        checks['window_visible_before_stop'] = not mismatches
        state['sample_content_before'] = before.getpixel(CENTRE)

        # The fault payload now faults; the peer then creates its own window in the
        # slot the fault owner used, and waits for one real routed input record.
        wait_for(guest, PEER_WINDOW, 120, 'the peer payload never got a live window')
        checks['peer_window_live'] = True
        # Drive the real hover and press immediately: the peer only waits inside
        # the kernel's existing demo budget, so no time may be wasted here.
        pointer = Pointer(guest)
        pointer.click(CENTRE[0], CENTRE[1])
        pointer.move(OUTSIDE[0], OUTSIDE[1])
        wait_for(guest, 'NATIVE CPL3 PEER SURVIVED', 60,
                 'the peer never received the routed input record')
        checks['input_routed_to_peer'] = True

        # Stop has already reclaimed both owners; Reap has not run yet, and the
        # kernel prints the two verdicts separately for exactly that reason.
        text = wait_for(guest, STOP_PASS, 60, 'Stop reclaim did not pass')
        checks['stop_pass'] = True
        wait_for(guest, REAP_PASS, 60, 'Reap reclaim did not pass')
        checks['reap_pass'] = True
        wait_for(guest, REAPED, 60, 'the reaped count was not reported')
        checks['reaped'] = True
        wait_for(guest, PROBE_EXIT, 60, 'the browser-platform fixture did not exit cleanly')
        checks['probe_exit_zero'] = True
        wait_for(guest, RUNTIME_PASS, 60, 'the kernel did not accept the run')
        checks['native_runtime_pass'] = True

        # The kernel's own read-only check, printed after the desktop's next
        # Update, asks the real window manager whether any native slot is still
        # open. That is the window boundary state; the pixels below are the
        # screen state, and neither substitutes for the other.
        final = wait_for(guest, GUI_CLOSED, 60,
                         'the desktop never reported the native slots closed')
        checks['gui_slots_closed'] = GUI_CLOSED in final and GUI_OPEN not in final

        after = screendump(guest, out, 'after-reap')
        state['sample_content_after'] = after.getpixel(CENTRE)
        state['after_pixel_mismatches'] = content_mismatches(after, expected)
        # Positive expectation: the wallpaper strip inside the native window's
        # rectangle is exactly the background the desktop computes, and neither
        # payload frame survives. A stale frame, a still-composited panel, a
        # blanked client area with the frame drawn, or a single changed pixel all
        # fail it.
        removed, detail = removal.native_window_removed(
            after, STRIP, CONTENT, removal_blends(), content_mismatches)
        state['removal'] = detail
        checks['window_removed_positive'] = removed

        final = guest.text()
        checks['no_reclaim_failure'] = 'RECLAIM STOP FAIL' not in final \
            and 'RECLAIM REAP FAIL' not in final
        checks['no_runtime_failure'] = 'NATIVE RUNTIME FAIL' not in final
        checks['single_runtime_pass'] = final.count(RUNTIME_PASS) == 1
        state['completed'] = True
    except BaseException as error:
        state['exceptions'].append('%s: %s' % (type(error).__name__, error))
        raise
    finally:
        if guest is not None:
            state['terminal'] = {
                'argv': getattr(getattr(guest, 'p', None), 'args', None),
                'pid': getattr(getattr(guest, 'p', None), 'pid', None),
                'exit_code': getattr(getattr(guest, 'p', None), 'returncode', None),
            }
            try:
                (out / 'guest.log').write_text(guest.text())
            except Exception as error:
                (out / 'guest-log-error.txt').write_text(repr(error) + '\n')
                state['exceptions'].append('guest.log: %r' % (error,))
            try:
                guest.close()
                checks['guest_closed'] = True
            except Exception as error:
                close_error = error
                state['exceptions'].append('guest.close: %r' % (error,))
                checks['guest_closed'] = False
            state['terminal']['exit_code'] = getattr(getattr(guest, 'p', None),
                                                     'returncode', None)
        state['checks'] = checks
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
        fail('reclaim guest checks failed: ' + (', '.join(failed) or 'no checks ran'))
    print('reclaim guest pass:', out / 'results.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
