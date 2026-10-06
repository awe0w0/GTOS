# English / Simplified Chinese desktop integration

The modern framebuffer desktop now has English and Simplified Chinese interfaces,
real Chinese raster glyphs, transactional language/theme preferences and a bounded
pinyin composer in the launcher. It does not provide a universal Chinese font,
system-wide input-method service, full text editor or general-purpose native app ABI.
Boot diagnostics and technical names such as GTOS, BSP, MiB and physical key names
remain unchanged. Arbitrary external application metadata is not machine-translated.

## Choosing language and appearance

Open Settings with `4` or its taskbar button. Click English / 简体中文, or press `C`
while Settings is focused. Click the Dark/Light previews, or press `T`.

The optional `storage::SettingsStore` is loaded by the kernel and passed as the
third `ModernDesktop` constructor argument. Defaults are English/Dark. A change
applies immediately, then attempts a checked save to the existing dedicated GTOS
app disk. There is no implicit formatting. Read-only, missing, undersized, foreign
or faulted media keeps the UI change for the current session and reports that it
is session-only. Disk-loaded settings are labeled loaded; a save is labeled saved
only after the store acknowledges the durability protocol. Failed saves do not
replace the store's last acknowledged values.

See `settings.md` for two-copy records, recovery barriers, failure states and
power-loss tests. Session/window-position restoration is still not implemented.

## Launcher text and pinyin

`L` opens the launcher. Chinese locale defaults to pinyin; English defaults to
direct Latin input. In Chinese locale, click the 拼音 / EN badge or press the
backtick key to change input mode. Input mode itself is session-only.

- Type an exact dictionary spelling, such as `yingyong`, `shezhi` or `jieqiu`
- A bounded panel displays every available candidate, numbered 1–9, in three columns
- Space chooses the first candidate; 1–9 or a candidate click chooses that item
- Enter commits the first candidate; with no matching phrase it commits raw Latin
- When composition is inactive, Enter opens the selected matching application
- Esc first cancels composition; a second Esc closes the launcher
- Left/Right move the committed-query caret by one Unicode scalar; a caret move
  cancels preedit. Backspace deletes the preceding scalar without splitting UTF-8
- Candidate commits and direct text insert at the current caret, not only at the end
- Long text scrolls around the caret using scalar boundaries and is clipped to its field

The query is 128 bytes including NUL. The preedit is at most 31 Latin letters.
If a candidate cannot fit, the query is unchanged and preedit is retained.
There is no selection, clipboard, normalization, grapheme-cluster editing,
predictive learning, fuzzy spelling, automatic phrase segmentation or network IME.
The current original dictionary contains 206 exact spellings and 359 candidates.

Window/locale/input changes, launcher dismissal, failed app activation and queue
recovery cancel composition. Reopening the launcher never resumes hidden preedit.
Held input-mode shortcut events are consumed without inserting literal backticks.
Search matches localized built-in titles, familiar aliases and original manifest
titles. In particular, 设置 finds the Appearance/Settings panel and 接球 finds the
verified bundled Catch package.

## Font rendering and bundled game

The UTF-8 painter decodes bounded scalars, uses the existing DejaVu-derived ASCII
atlas and the covered GTOS Han atlas, and draws a visible square for unsupported
characters. Body and heading glyphs are rasterized separately at 14/28 pixels.
All fixed catalog and candidate strings are checked against the atlas. The current
154-entry catalog uses 440 covered non-ASCII glyphs; atlas storage is 319,440 bytes.
See `i18n.md` and `apps/fonts/README.md` for scope, provenance and reproducibility.
Both font licenses must accompany the distributed source and ISO.

Only the bundled Catch version identified by `id=catch`, length 824 and stored
CRC32 `0x733AF7F5` receives translated metadata/prompts. This compatibility mapping
is not package authentication. Other versions/IDs keep their original text.
The package bytecode, ID, movement, score and collision logic are unchanged.
The game host draws 接球 at canvas y=1, above the divider at y=20, and the Chinese
instructions at y=109, below the paddle and within the 272×128 canvas. Covered Han
glyphs are thresholded into the game's existing 16-color surface, then integer
scaled with the rest of the application. Numeric scores remain ordinary digits.

## Verification

`GTOS_DESKTOP_SANITIZERS=1 ./tests/desktop_test.sh` runs real i386, native x86-64 and
strict ASan/UBSan sources. A fixed-label source inventory also refuses new
uncatalogued desktop strings before compiling. In addition to the English desktop suite, it covers:

- Scalar-width rendering, malformed input, UTF-8 caret movement and backspace
- Middle insertion, Chinese substring and localized settings-alias search
- All nine number choices, visible candidate cells and ninth-candidate mouse input
- Cancel/reopen/focus-loss behavior, failed disk-read activation and shortcut repeats
- Capacity failure preserving composition instead of silently dropping committed text
- Pixel-exact 接 and 移 glyphs at bounded positions in the real VM-host game output
- Third-party lookalike package retaining its original title/instructions
- Language/theme persistence, failed-save session state and settings remount

`python3 tests/desktop_language_qemu.py --output <fresh-directory>` runs the actual
GRUB/QEMU desktop through keyboard and mouse input. It records native-resolution
screenshots for language selection, nine candidates, UTF-8 editing/search, playable
Chinese Catch, repeated switching, persisted Chinese/light then English/dark boots,
and a foreign disk whose bytes must remain unchanged. Host previews alone do not
establish guest boot/input correctness. The separate English and legacy desktop
acceptance scripts remain regression gates.
