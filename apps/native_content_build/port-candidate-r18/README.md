# GTOS port candidate, r18

This directory is the reviewable form of the private port candidate for GTOS-DSH-154: every edit the port needs to move the
Chromium build graph from the pinned checkouts toward a GTOS content target, expressed as a patch against the file it came from,
plus the manifest that pins the exact bytes each patch was generated against.

It exists because the port work had been written directly into the shared checkouts. The review keeps those bytes as they are and
does not undo them; from here on, port edits are made here, replayed into a composed build root the packet owns, and reviewed as
patches.

## What is here

    candidate.json   the manifest: each path, the origin it was captured from, both digests, whether it is edited, and the patches
    patches/         one unified diff per edited file, `-p1` against a checkout of the source root
    apply.py         compose a build root from the pinned checkouts plus this candidate's overlay
    refresh.py       rebuild the manifest from the overlay that is on disk

The byte-exact overlay itself (`overlay/` with the full files) stays outside the repository, under the packet's own artifacts
directory: `port-candidate-154-r18/`. Committing four full files, one of which is a 9436-line V8 build file, would drown the
review in text that is mostly unchanged; the patches are the change.

## How the edits were chosen

Each patch is the smallest change that removes a real, measured obstruction, and none of them fabricates a capability:

* `v8/BUILD.gn` — the port's refusal of every GTOS build becomes a declared argument, `gtos_v8_bringup`, that is false by default.
  The refusal text is unchanged and still fires for every ordinary build. The argument exists so the graph can be asked what it
  still lacks; the answer is a real GN or Ninja failure naming the next missing piece instead of one sentence.
* `third_party/libxml/BUILD.gn` — GTOS was not in the OS-specific header list, so the file failed with "os_include is not
  currently in scope". The GTOS branch states the missing dependency: there is no configure output for a freestanding target, and
  the linux directory beside it is configure output for a libc GTOS does not have. No headers are invented or reused.
* `components/crash/core/common/BUILD.gn` — the crash-key default is `is_fuchsia`; GTOS has no crashpad, breakpad or OS crash
  reporter either, so it takes the stub the file already provides: `is_fuchsia || is_gtos`.
* `content/test/BUILD.gn` — the pinned checkout appends to `data` in `content_unittests` without ever assigning it, and the GN this
  tree pins (2589, `3fef1f00031b`) rejects that append with "Undefined identifier". The statement is inside `if (!is_ios)`, which
  is true on Linux too, so the checkout cannot load this file on any platform. `data = []` before the conditional appends restores
  what the appends mean and adds or removes no entry.

## How to replay

    python3 apply.py --check-only --view /tmp/unused          # pin check: refuse if a shared origin moved
    python3 apply.py --view /var/tmp/gtos-port-r18/view       # compose links + real overlay files
    python3 refresh.py                                        # after editing the overlay, re-pin the manifest

`apply.py` refuses to compose into any path under `sources/`, refuses when a shared origin's digest no longer matches the pinned
one, and never writes an overlay file through a link into a shared tree: each symlinked ancestor is materialised inside the view
first. That last rule is not hypothetical. The first composition this round wrote through a `third_party` symlink into the pinned
libxml file; it was restored from the checkout's own git object and the composer was changed so it cannot happen again.

## What the candidate does and does not prove

Composing this candidate and asking GN for `//content/shell:content_shell_app` measured four rungs: the V8 refusal (rc 1, still
default), then the crash-key assertion, then the `data +=` fixture defect, then `//ui/menus/BUILD.gn`'s platform assertion, which
has no GTOS backend. The generated build graph therefore stops at the browser UI layer, and no object, link or guest run is
claimed from it. See `artifacts/.../c1-content-frontier-154-r18.md` for the stage-by-stage records.
