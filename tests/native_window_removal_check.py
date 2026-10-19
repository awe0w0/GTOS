#!/usr/bin/env python3
"""Offline counterexamples for the window-removal criterion.

Runs no guest. Two kinds of input:

  * small synthetic frames built in memory, and
  * the real saved screenshots of the r6 reclaim run, which are reused here so
    the criterion is proved on real frames without booting anything again.

The required behaviour:

  * the real after-reap frame passes (the window really was removed),
  * the real fault frame fails (the window was composited),
  * a stale peer frame, a single changed pixel and a blanked client area with the
    frame still drawn all fail.

    native_window_removal_check.py [P]

P defaults to the r6 reclaim artifact directory.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import native_window_removal as removal  # noqa: E402

DEFAULT_R6 = pathlib.Path(
    '/mnt/f/GTOS-Chromium/artifacts/native-window-input-implementation-20261019-a/'
    'reclaim-guest-04')

# The real slot-0 rectangle and the composited 16x16 frame inside it, both
# derived from the layout code in the runner.
WINDOW = (208, 158, 480, 360)
CONTENT = (400, 310, 96, 96)
SCALE = CONTENT[2] // 16
# A wallpaper-only strip of that rectangle: below the Welcome window and its
# shadow (which reach y=475), above the dock (which starts at y=548), left of the
# wallpaper lettering (which starts at x=587), and clear of the pointer.
STRIP = (208, 480, 372, 36)

# The payload frames, recomputed the same way the runner does.
CHECKER = ((221, 229, 236), (167, 181, 194))


def frame(tone, alpha=128):
    values = []
    for index in range(256):
        base = 0x30 + index % 4
        value = (base * tone) // 255
        value = (value * alpha) // 255
        values.append((value, value, value, alpha))
    rows = []
    for y in range(16):
        row = []
        for x in range(16):
            red, green, blue, alpha_value = values[y * 16 + x]
            background = CHECKER[((x // 4 + y // 4) & 1)]
            row.append(tuple(channel + (background[index] * (255 - alpha_value) + 127) // 255
                             for index, channel in enumerate((red, green, blue))))
        rows.append(row)
    return rows


FAULT = frame(255)
PEER = frame(96)


def mismatches(image, expected):
    return removal.blend_mismatches(image, expected, CONTENT, SCALE)


def decide(image):
    return removal.native_window_removed(
        image, STRIP, CONTENT, [('fault', FAULT), ('peer', PEER)], mismatches)


def synthetic(size=(800, 600)):
    """A frame with the desktop wallpaper painted exactly where it belongs."""
    from PIL import Image
    image = Image.new('RGB', size)
    for y in range(size[1]):
        for x in range(size[0]):
            image.putpixel((x, y), removal.wallpaper_pixel(x, y, size[0], size[1]))
    return image


def paint(image, expected):
    for y in range(16):
        for x in range(16):
            for dy in range(SCALE):
                for dx in range(SCALE):
                    image.putpixel((CONTENT[0] + x * SCALE + dx,
                                    CONTENT[1] + y * SCALE + dy), expected[y][x])


def main():
    problems = []
    r6 = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_R6

    def expect(name, ok_wanted, image, why=''):
        ok, detail = decide(image)
        if ok != ok_wanted:
            problems.append('%s: expected ok=%s, got %s (%s)'
                            % (name, ok_wanted, ok, detail.get('reason')))
        return detail

    # ---- real frames from the accepted r6 run ------------------------------
    after_path = r6 / 'after-reap.ppm'
    fault_path = r6 / 'fault-window.ppm'
    if after_path.is_file():
        detail = expect('real after-reap frame', True,
                        removal.load_image(after_path))
        if detail.get('strip_changed_pixels'):
            problems.append('real after-reap strip changed %d pixels'
                            % detail['strip_changed_pixels'])
    else:
        problems.append('missing real control frame: %s' % after_path)
    if fault_path.is_file():
        detail = expect('real fault frame', False,
                        removal.load_image(fault_path))
        if not detail.get('strip_changed_pixels'):
            problems.append('real fault frame did not disturb the strip')
    else:
        problems.append('missing real control frame: %s' % fault_path)

    # ---- synthetic counterexamples ----------------------------------------
    clean = synthetic()
    expect('synthetic wallpaper', True, clean)

    stale = synthetic()
    paint(stale, PEER)
    expect('stale peer frame', False, stale)

    speck = synthetic()
    speck.putpixel((STRIP[0] + 5, STRIP[1] + 5), (255, 0, 255))
    detail = expect('single pixel changed', False, speck)
    if detail.get('strip_changed_pixels') != 1:
        problems.append('single-pixel case did not report exactly one pixel')

    # The client area cleared but the window frame still drawn: the panel covers
    # the whole rectangle, and the content area was repainted flat, so no payload
    # frame is present yet the window is still on screen.
    blanked = synthetic()
    for y in range(WINDOW[1], WINDOW[1] + WINDOW[3]):
        for x in range(WINDOW[0], WINDOW[0] + WINDOW[2]):
            blanked.putpixel((x, y), (26, 34, 43))
    for y in range(CONTENT[1], CONTENT[1] + CONTENT[3]):
        for x in range(CONTENT[0], CONTENT[0] + CONTENT[2]):
            blanked.putpixel((x, y), (12, 18, 24))
    expect('client blanked, frame still drawn', False, blanked)

    panel = synthetic()
    for y in range(WINDOW[1], WINDOW[1] + WINDOW[3]):
        for x in range(WINDOW[0], WINDOW[0] + WINDOW[2]):
            panel.putpixel((x, y), (26, 34, 43))
    expect('window panel still composited', False, panel)

    for problem in problems:
        print('FIXTURE FAIL: ' + problem)
    print('WINDOW REMOVAL FIXTURE PASS' if not problems
          else 'WINDOW REMOVAL FIXTURE FAIL')
    return 0 if not problems else 1


if __name__ == '__main__':
    sys.exit(main())
