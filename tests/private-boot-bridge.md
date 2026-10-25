# Private boot bridge regressions

Run with Node and one JSON control containing pins for `module` and `request_module`, a fresh `fixture_root`, and a fresh `result_path`. The suite imports the production bridge and request module; it does not launch WSL, guard, QEMU or the kernel image.

The regressions distinguish five-token metadata/query requests from the E99 request, separate WSL's process flag from guard's file EXEC grants, require pin verification before imports, reject oversized logs without truncation, and preserve the failed native guard row when an independently proved E99 diagnostic uses the expected deadline kill. Normal metadata/query exits require zero; E99 requires the guard's expected outer exit one and actual diagnostic evidence. Missing diagnostics leave guest execution unknown.

Filesystem fixtures exercise fresh outputs, missing parents, image identity, raw capture identity, duplicate and malformed ledger rows. A module with a top-level side effect proves that wrong component pins are rejected before import. These are host regressions; passing them does not prove a guest has booted or that the native guard and QEMU can run on this host. Production native evidence and bounded process cleanup remain separate gates.


108: The production runner calls assertNativeStageIdentity after reservation and before freeze. The bridge additionally reads the current false guard reservation and refuses preexisting spec/inner/raw or any prior ledger row with that stage. Runtime staging has a separate S-root ledger; Python unit/metadata/probes/VM use L. No old ledger is repaired or accepted by these regressions.
