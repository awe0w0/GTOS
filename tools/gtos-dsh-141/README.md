# GTOS-DSH-141 executed tooling: the runtime host unit

This directory holds the scripts this packet actually executed, at the bytes that ran. They are committed because a clean
worktree is not evidence that the tested tree is the committed tree: 140 ran a driver, a gate, a bridge, a consumer and an
engine that were never in any commit, so "the working tree was clean" said nothing about whether the code that produced the
results was the code the branch held.

| file | role | executed as |
| --- | --- | --- |
| `drive-141.mjs` | the only path to a compiler, linker or ELF: it writes a stage spec, the frozen runner reserves the stage and freezes a helper, the helper hands control to the production bridge, the bridge builds and admits its own request, and the original guard launches it | `node drive-141.mjs <phase> ...` |
| `runcases-141.cjs` | runs every case of the unit, one guarded process per case, and reads the verdict out of the guard's raw channel | `node runcases-141.cjs h147 rc141-27 11 runtime-host-unit-141b.json` |
| `staging-141-01.py` | the source view staging module: copies the frozen catalog plus this packet's own files into a wave the stage owns | frozen helper of the `source.view` stage |

## What the unit now does that it did not do in 140

The unit links at 1 MiB, in the window the production paging object models, because that object refuses an unsafe context
unless its own bytes, the allocator's, the directory and every table are identity mapped — and no host process can satisfy
that from `0x08048000`. The link layout is `tests/runtime_host.ld`; the reserved kernel range is derived from the linked
image end (`__gtos_host_image_end`) rather than written out beside it; the kernel stack arena is the production host-test
constant, mapped by the entry and probed by the unit before any case runs.

The production unsafe-context check is unchanged. So is every refusal it makes.

## Result

`runtime-host-unit-141b.json`: 11 of 11 cases pass, each in its own process, each an accepted guarded stage with the unit's
own `RUNTIME UNIT PASS` marker and zero exit status. Case 10 reports `ProtectRange consulted 00000001 times, refused
00000001 times at page count 00000002`, which is the target stage actually being consulted — in 140 it was consulted zero
times and the case was reported as a refusal.

Cases 5 through 10 are the ones that were failing before this change. They failed at `paging.enable()`, four instructions
into a setup that had nothing to do with what they test, and were recorded in 140 as "expected refusals".