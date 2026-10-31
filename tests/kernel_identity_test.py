"""Six cases against the builder's own kernel comparison, driven on a kernel the recorded verify just built.

A change inside a loadable segment must be refused even when the file is otherwise untouched; a change outside the segments that is not the
run directory must also be refused; a different run directory of a different length may pass, and a shorter or longer file that the path does not
explain may not.
"""
import hashlib
import importlib.util
import pathlib
import sys
import tempfile

BUILDER = pathlib.Path('/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154/tools/build-native-c-file-probe.py')
KERNEL = pathlib.Path(sys.argv[1])
TOKEN = sys.argv[2].encode()
if not TOKEN.endswith(b'/'):
    TOKEN += b'/'

spec = importlib.util.spec_from_file_location('builder', BUILDER)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

base = builder.kernel_facts(KERNEL, TOKEN)
base['run_token'] = TOKEN
assert base['run_ref_offsets'], 'the kernel does not mention the run directory, so the path cases cannot be built'
assert not base['run_refs_in_loaded_segments'], 'the kernel keeps the run directory in a loadable segment'
first = base['load_ranges'][0]
frozen = {'metadata': {'load_ranges': [list(each) for each in base['load_ranges']],
                       'run_ref_offsets': base['run_ref_offsets'], 'run_refs_in_loaded_segments': []},
          'normalized': base['normalized_sha256'], 'bytes': len(base['bytes']), 'token_length': len(TOKEN)}
results = []


def read(data, token=TOKEN):
    """Read the bytes back through the code under test, from a real file."""
    temporary = pathlib.Path(tempfile.gettempdir()) / 'kernel-identity-case.bin'
    temporary.write_bytes(data)
    facts = builder.kernel_facts(temporary, token)
    facts['run_token'] = token
    return facts


def case(name, body, expected):
    try:
        detail = body() or ''
    except AssertionError as failure:
        verdict, detail = 'refused', str(failure)[:70]
    else:
        verdict = 'accepted'
    results.append((name, verdict, detail, expected))


case('the file is unchanged', lambda: builder.compare_kernel(base, 'O0', frozen), 'accepted')


def inside_a_segment():
    data = bytearray(KERNEL.read_bytes())
    data[first[0] + 512] ^= 0xFF
    builder.compare_kernel(read(bytes(data)), 'O0', frozen)


case('one byte inside a loadable segment', inside_a_segment, 'refused')


def outside_everything():
    data = bytearray(KERNEL.read_bytes())
    data[base['load_ranges'][-1][1] + 8] ^= 0xFF
    builder.compare_kernel(read(bytes(data)), 'O0', frozen)


case('one byte in the tables the loader never maps', outside_everything, 'refused')


def another_run_directory():
    other = b'/var/tmp/gtos-dsh-154-mainline/r22/candidate-verify-r22b-00-c0/out/'
    moved = KERNEL.read_bytes().replace(TOKEN, other)
    facts = read(moved, other)
    rule = builder.compare_kernel(facts, 'O0', frozen)
    assert 'length change' in rule, rule


case('the same bytes under another run directory', another_run_directory, 'accepted')


def directory_inside_a_segment():
    data = bytearray(KERNEL.read_bytes())
    data[first[0] + 1024:first[0] + 1024 + len(TOKEN)] = TOKEN
    builder.compare_kernel(read(bytes(data)), 'O0', frozen)


case('the run directory written into a loadable segment', directory_inside_a_segment, 'refused')


def no_offset_table():
    sparse = dict(frozen)
    sparse['metadata'] = None
    rule = builder.compare_kernel(base, 'O0', sparse)
    assert rule.startswith('normalized-only'), rule


case('a frozen record without an offset table', no_offset_table, 'accepted')

for name, verdict, detail, expected in results:
    print(('PASS ' if verdict == expected else 'FAIL ') + name + ': ' + verdict + (' (' + detail + ')' if detail else ''))
failed = [name for name, verdict, _, expected in results if verdict != expected]
print('cases', len(results), 'failed', len(failed))
raise SystemExit(1 if failed else 0)
