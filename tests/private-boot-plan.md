# Private E99 boot plan

The test imports the production module using a pinned JSON control {module:{path,bytes,sha256},result_path}.
Plans use one bundled dynamic loader and one real emulator ELF. No shell launcher, disk, network, monitor,
GUI, acceleration passthrough, arbitrary options or extra kernel is accepted. Logs and outputs are stage specific.
Data fixtures cover exact E99 lines in both E9 and serial, phase order, missing or misleading evidence,
deadlines, raw identity, guard probes, group cleanup, invalid paths and 256 stable repetitions.

Fixtures are data; they are not execution evidence. Production must import the same module and read actual
guard spec/inner/result row/raw and both bounded log files. Preserve guard ok=false and exit=-9 for an expected
deadline kill. A separate diagnostic observation may pass; it is not tool rc0 or full boot success.
Actual target execution requires a new explicit grant, exact image and tool pins, and no changed source/flags.

A failed diagnostic leaves guest execution and constructor inference unproven (null), not false. E01 or a later failure may already have executed the guest.


108: The runtime root is exactly source_root + /runtime. DrvFs and another source root are refused. Runtime staging and its real Python unit suite receive only the two Python/loader EXEC identities. The original image and E99 request remain unchanged apart from the runtime path. This relocation is a practical alternative; it does not establish the cause of the old EACCES.
