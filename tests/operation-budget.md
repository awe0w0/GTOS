# Operation routing and budget tests

Run the test with pinned Node and a JSON control containing module {path, bytes, sha256} and a fresh result_path.
The suite imports the actual production module. It covers shared category caps, incomplete and failed history,
new helper and stage aliases, category hopping after exhaustion, mismatched control modes, malformed history,
cross task borrowing, invalid caps, nonmutation and 512 admission/exhaustion cycles.

This module validates declared operation identity under a frozen policy. It cannot infer helper semantics from
JavaScript text or stop an operator from bypassing the runner. Review must bind each actual helper branch to
the operation it performs. The real runner must call this function before reservation/freeze/launch, retain
operation_id/operation_mode/policy pin in both records, and hash the policy and module before and after use.
An integration suite must exercise the actual runner, including refusal before helper side effects and successful
same-operation retry in the original category with fresh budget. Pure unit results alone do not establish wiring.
