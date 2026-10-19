#!/usr/bin/env python3
"""Positive criterion for "the native window is gone from the screen".

The old check asked "does the after frame differ from the fault frame anywhere
inside the content rectangle". That is not a removal test: a peer frame with a
different tone differs too, a single changed pixel differs, and a window whose
client area was blanked but whose frame is still composited differs as well.

This module states the removal as a positive expectation instead, and it needs no
"before the window opened" reference frame (which this guest cannot produce: the
fault payload opens its window immediately at process start):

  1. A strip of the native window's rectangle that lies below every other window,
     above the dock, clear of the wallpaper's own lettering and clear of the
     pointer must show EXACTLY the wallpaper the desktop computes for those rows.
     The expectation is recomputed here with the same integer arithmetic as
     ModernDesktop::Render, so it is independent of any captured image.
  2. No pixel of the composited payload frame may survive in the content
     rectangle.
  3. The window manager must report the slot closed (checked by the caller).

A window that is still composited covers that strip with its own panel, so (1)
fails. A window whose client area was blanked still has its panel there, so (1)
fails. One changed pixel fails (1) as well.

    import native_window_removal as removal
    ok, detail = removal.native_window_removed(after, strip, content, blends,
                                               blend_mismatches)
"""
import pathlib

# ModernDesktop::Render wallpaper, exactly as modern_render.cpp draws it.
WALLPAPER_BAND = (0x17, 0x3F, 0x49)


def wallpaper_pixel(x, y, width=800, height=600, light_theme=False):
    """The colour the desktop paints at (x, y) when no window covers it."""
    t = (y * 60) // height
    if light_theme:
        colour = (((160 + t // 2) << 16) | ((188 + t // 3) << 8) | (204 + t // 3))
    else:
        colour = (((12 + t // 7) << 16) | ((27 + t // 3) << 8) | (42 + t // 2))
    start = width * 3 // 5 - y // 2
    end = start + width // 3
    if start < 0:
        start = 0
    if end > width:
        end = width
    if start <= x < end:
        return WALLPAPER_BAND
    return ((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF)


def wallpaper_region(rect, width=800, height=600, light_theme=False):
    """Expected wallpaper rows for rect, as rows of RGB triples."""
    x0, y0, w, h = rect
    return [[wallpaper_pixel(x0 + column, y0 + row, width, height, light_theme)
             for column in range(w)] for row in range(h)]


def region(image, rect):
    """Every pixel of rect as rows of RGB triples."""
    x, y, width, height = rect
    return [[image.getpixel((x + column, y + row))[:3] for column in range(width)]
            for row in range(height)]


def distinct_colours(pixels):
    seen = set()
    for row in pixels:
        seen.update(row)
    return len(seen)


def differences(expected_pixels, after_pixels):
    """Count differing pixels and keep the first few, with coordinates."""
    count = 0
    examples = []
    for row, (want_row, got_row) in enumerate(zip(expected_pixels, after_pixels)):
        for column, (want, got) in enumerate(zip(want_row, got_row)):
            if want != got:
                count += 1
                if len(examples) < 8:
                    examples.append({'at': [column, row],
                                     'expected': list(want),
                                     'after': list(got)})
    return count, examples


def blend_mismatches(image, expected, rect, scale):
    """Every pixel of the composited 16x16 frame must match the prediction.

    `rect` is where the frame was composited and `scale` is the integer zoom the
    desktop applied, so this covers the whole drawn area and not a sample.
    """
    x0, y0 = rect[0], rect[1]
    mismatches = []
    for y in range(16):
        for x in range(16):
            want = expected[y][x]
            for dy in range(scale):
                for dx in range(scale):
                    at = (x0 + x * scale + dx, y0 + y * scale + dy)
                    got = image.getpixel(at)[:3]
                    if got != want:
                        if len(mismatches) < 8:
                            mismatches.append([list(at), list(want), list(got)])
    return mismatches


def native_window_removed(after, strip, content, blends, blend_mismatches_fn,
                          width=800, height=600, light_theme=False):
    """Decide whether the native window has really been removed.

    after               frame captured after Reap and the desktop's next Update
    strip               a wallpaper-only part of the native window's rectangle
    content             the composited payload frame inside that rectangle
    blends              [(name, expected_pixels)] of payload frames that must be gone
    blend_mismatches_fn callable(image, expected) -> list of mismatching pixels

    Returns (ok, detail).
    """
    detail = {'strip': list(strip), 'content': list(content),
              'strip_pixels': strip[2] * strip[3]}
    expected = wallpaper_region(strip, width, height, light_theme)
    detail['strip_colours'] = distinct_colours(expected)
    changed, examples = differences(expected, region(after, strip))
    detail['strip_changed_pixels'] = changed
    detail['strip_examples'] = examples
    if changed:
        detail['reason'] = ('%d of %d wallpaper pixels in the native window strip '
                            'do not match the desktop background, so something is '
                            'still composited there'
                            % (changed, detail['strip_pixels']))
        return False, detail
    for name, blend in blends:
        leftover = blend_mismatches_fn(after, blend)
        if not leftover:
            detail['reason'] = ('the %s payload frame is still composited in the '
                                'content rectangle' % name)
            detail['leftover'] = leftover
            return False, detail
    detail['reason'] = ('all %d wallpaper pixels in the native window strip match '
                        'the desktop background and neither payload frame survives'
                        % detail['strip_pixels'])
    return True, detail


def load_image(path):
    from PIL import Image
    image = Image.open(pathlib.Path(path))
    image.load()
    return image.convert('RGB')
