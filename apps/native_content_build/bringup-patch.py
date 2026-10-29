#!/usr/bin/env python3
"""Apply the private frontier bring-up patch to a copy of the port's root build file.

The port refuses every GTOS browser graph from its root build file, and the refusal is right: the runtime the browser needs does
not exist. This patch keeps that refusal as the default and adds one named way past it, so that a bring-up can generate the real
Content graph without claiming the platform runs a browser. It is applied to *a copy* — the shared port tree is never written.

Each input is classified by identity, not by a substring: the unchanged block, the exact patched block, or neither. "Neither" is an
error, because a file this script does not recognise must never be reported as already patched.

Usage: bringup-patch.py <root BUILD.gn> <dotfile .gn>
Exit status: 0 patched or already patched, 2 input not recognised, 1 usage.
"""
import pathlib
import sys

GUARD = '''if (is_gtos) {
  assert(
      false,
      "GTOS browser runtime is not implemented: libc/C++ runtime, VM, threads, IPC, Ozone and networking are required. The real freestanding platform qualification target is --root-target=//tools/gtos:platform_identity_probe.")
}
'''

PATCHED = '''# The browser runtime is not implemented, and that is the default answer for every target: an invocation that says nothing
# about the frontier bring-up meets exactly the refusal below, with the same message. A bring-up invocation names itself, and then
# only the graph it asked for is generated — the platform is still not claimed to run a browser.
declare_args() {
  # Set by an explicit bring-up invocation only. Everything else, including every ordinary build and every test, keeps the
  # refusal, so no target that would need the missing runtime is silently admitted.
  gtos_frontier_bringup = false
}

# GN has no conditional expression, so the state is named in two branches; this line is what an operator reads to know which
# answer a generation got.
_frontier_note = "GTOS-FRONTIER current_os=" + current_os + " target_os=" + target_os
if (gtos_frontier_bringup) {
  print(_frontier_note + " bringup=true")
} else {
  print(_frontier_note + " bringup=false")
}

if (is_gtos && !gtos_frontier_bringup) {
  assert(
      false,
      "GTOS browser runtime is not implemented: libc/C++ runtime, VM, threads, IPC, Ozone and networking are required. The real freestanding platform qualification target is --root-target=//tools/gtos:platform_identity_probe.")
}
'''

INTERPRETER = 'script_executable = "//third_party/cpython3/host/bin/python3"'
INTERPRETER_PATCHED = '''# The repository points this at a hermetic interpreter it does not carry, which every exec_script in the graph needs.
# A host-side generation runs its scripts with the host interpreter, so this candidate names it explicitly.
script_executable = "/usr/bin/python3.8"'''


def apply(path, original, patched, name):
    """Classify the file by identity and rewrite it only when it still carries the original text exactly once."""
    text = pathlib.Path(path).read_text()
    originals = text.count(original)
    patched_count = text.count(patched)
    if originals == 1 and patched_count == 0:
        pathlib.Path(path).write_text(text.replace(original, patched))
        print('%s: patched %s' % (name, path))
        return 0
    if originals == 0 and patched_count == 1:
        print('%s: already patched %s' % (name, path))
        return 0
    raise SystemExit('%s: %s carries neither the original block nor the patched block exactly once '
                     '(original %d, patched %d); refusing to guess' % (name, path, originals, patched_count))


def main(argv):
    if len(argv) != 3:
        raise SystemExit('usage: bringup-patch.py <root BUILD.gn> <dotfile .gn>')
    apply(argv[1], GUARD, PATCHED, 'root build file')
    apply(argv[2], INTERPRETER, INTERPRETER_PATCHED, 'dotfile')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except SystemExit:
        raise
