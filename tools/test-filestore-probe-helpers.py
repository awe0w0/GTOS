#!/usr/bin/env python3
"""Unit checks for the two portability rules the FileStore probe now enforces.

These are checks of the probe's own helpers, not of the file system: an image without the distribution loader must still launch the
stock verifier directly, and an execution path that is not a regular file must be refused. Both are import-level behaviours, so they
are tested without building a kernel or starting a guest.
"""
import importlib.util
import pathlib
import sys
import tempfile

PROBE = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'build-filestore-probe.py'


def load():
    spec = importlib.util.spec_from_file_location('filestore_probe', PROBE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    module = load()
    checked = []

    # No loader on the image: the stock verifier is executed directly, exactly as the pre-existing CI path always did.
    direct = module.stock_cmd(['/tmp/stock-verifier', 'disk.img', 'inspect'], loader='/nonexistent/ld.so')
    assert direct == ['/tmp/stock-verifier', 'disk.img', 'inspect'], direct
    checked.append('missing loader falls back to direct exec')

    # Loader present: the verifier is started through it, with the verifier first in its own argument list.
    with tempfile.NamedTemporaryFile() as loader:
        loaded = module.stock_cmd(['/tmp/stock-verifier', 'disk.img', 'inspect'], loader=loader.name)
    assert loaded[0] == loader.name and loaded[1] == '--library-path', loaded
    assert loaded[-3:] == ['/tmp/stock-verifier', 'disk.img', 'inspect'], loaded
    checked.append('present loader is used with the verifier as the loader argument')

    # Execution targets: a regular file is fine, a directory is not, and an empty argument list is refused rather than indexed.
    with tempfile.NamedTemporaryFile() as regular:
        assert module.exec_target_ok([regular.name]) is True
    checked.append('regular file accepted as an execute target')
    with tempfile.TemporaryDirectory() as directory:
        assert module.exec_target_ok([directory]) is False
    checked.append('directory refused as an execute target')
    assert module.exec_target_ok([]) is False
    checked.append('empty argument list refused')
    assert module.exec_target_ok(['/nonexistent/program']) is False
    checked.append('missing path refused')

    for line in checked:
        print('PASS ' + line)
    print('PROBE HELPER CONTRACT PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
