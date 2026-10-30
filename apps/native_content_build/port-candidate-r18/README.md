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
    apply.py         compose a build root from the pinned checkouts and these patches

The byte-exact overlay the patches were generated from also stays on the artifact side, under the packet's own artifacts
directory (`port-candidate-154-r18/overlay/`). It is not needed to compose: the patches are the change, and `apply.py` rebuilds the
bytes from the origin plus the patch, refusing if the result does not match the digest in the manifest.

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
    python3 apply.py --view /var/tmp/gtos-port-r18/view       # compose links + patched real files

Composition links every entry of the two pinned checkouts, then, for each edited file, copies the origin into the view, applies the
patch to that copy, and compares the result with the digest the manifest records. It refuses:

* any destination under a shared source root;
* a shared origin whose digest no longer matches the pinned one;
* an applied file whose digest is not the recorded one.

It also never writes through a link into a shared tree: each symlinked ancestor is replaced by a real directory of links inside the
view first. That rule is not hypothetical. The first composition this round wrote through a `third_party` symlink into the pinned
libxml file. It was restored from the checkout's own git object and the composer was changed so it cannot happen again.

## What the candidate does and does not prove

Composing this candidate and asking GN for `//content/shell:content_shell_app` measured seven rungs: the V8 refusal (rc 1, still
default), then the crash-key assertion, then the `data +=` fixture defect, then `//ui/menus/BUILD.gn`'s platform assertion, then the
libxml configure requirement — reached from `//content/common`, from `//base` and even from `//third_party/zlib`. GN therefore never
produces a build graph for any target tried, and no object, link or guest run is claimed from it. See
`artifacts/.../c1-content-frontier-154-r18.md` for the stage-by-stage records.
