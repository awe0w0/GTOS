# GTOS-DSH-142 execution tooling

Everything this packet runs is here, and everything here is what actually ran. The driver locates the packet from its own
path, so it runs from a clean checkout of this worktree without editing a literal.

## The one path

```
drive-142.mjs  ->  run-142-r01.mjs (frozen, pinned)  ->  frozen helper  ->  references/private-boot-checkpoints-142.mjs
                                                                        ->  guard-041-capture.py  ->  the tool
```

No phase of the driver executes a compiler, a linker, a Python module or an ELF directly. The runner claims a stage name,
writes the reservation, freezes the helper, writes the control, and launches the helper under Windows Node. The helper hands the
frozen bridge control to the production bridge. The bridge admits its own request, builds the argv, and launches the guard
through `wsl.exe`. The guard runs the tool in its own child with a Landlock ruleset and writes the ledger row, the inner report
and the raw channel itself. The bridge then classifies from those three, and nothing is accepted on a wrapper's exit code alone.

## Why the runner lives in the report root

`assertRunnerPin` requires a frozen runner under a `frozen-runners` directory with a pin beside it, named from the task and the
runner revision. The frozen runner is therefore `R/frozen-runners/run-142-r01.mjs` with `R/runner-pin-142-run-142-r01.json`.
The copy in this directory is the one a reviewer reads, and the driver's first act is to require the two to be byte-identical,
so a committed copy that has drifted is a stop rather than a second, unpinned runner.

## Phases

| phase | what it does |
| --- | --- |
| `contract` | freezes `R/contract-142.json`: roots, the executor closure with a pin for each module, the pinned tools, and the runtime inventory. Every later spec pins this file, so a build cannot be described by a contract written afterwards. |
| `runtimem <stage>` | materialises the fixed 206-file / 94050160 B runtime bundle into `<source view>/runtime` and publishes it with one rename. |
| `runtimeverify <stage>` | reads the published tree back and re-derives every byte, hash, mode, link count and inode from the filesystem, inside the distribution. |
| `doctor` | prints what exists, so a resumed session can tell finished work from unfinished. |

## The runtime copy and its reader are different programs

`staging-142-01.py` copies and hashes each file as it writes it. That is a writer reporting on itself, so it is not the check.
`verify-runtime-142.py` starts from the bytes on disk and re-derives everything: the file set, each byte count, each hash, each
POSIX mode, each link count, inode uniqueness, that no entry is a symlink, and that each file resolves inside the destination.

It also exercises four defects against a small tree it builds itself, and requires each to be refused: a symlink where an
ordinary file belongs, a declared file that is absent, a file whose bytes changed, and a file with the wrong mode. The same run
includes a control case that must be *accepted*, because a comparator that refuses everything would otherwise look like four
successful negatives.

The reader reports the directory modes rather than judging them. A directory's execute bit is what makes a path traversable and
every usable tree has it; the obligation is that no program outside the declared pair is executable, and that is checked on the
files. The first version of the reader treated a directory's execute bit as a program grant and refused the whole tree; the
correction is recorded in the report it writes.

## Derived, not rewritten

The modules in `references/` are derived from the 141 originals that actually ran, by four substitutions that name a packet
artefact (`GTOS-DSH-141` -> `GTOS-DSH-142`, `-141.mjs` -> `-142.mjs`, `-141.json` -> `-142.json`, and the two root paths). A
blanket substitution of `141` would also rewrite byte counts and hash digits, so the substitution is limited to those forms and
any remaining occurrence is reported line by line. Both derivation records report zero residue.

`staging-142-01.py` compares `control['task']` against a literal, so deriving it without changing that literal would produce a
module that refuses every control this packet writes. The derivation record shows the literal now reads `GTOS-DSH-142`.
