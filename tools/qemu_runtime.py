"""Per-process environment for system or unpacked Debian/Ubuntu QEMU tools."""
import os
from pathlib import Path


def qemu_environment(rootless=None, environ=None):
    """Copy the environment, adding both split-/usr library paths if rootless.

    ``rootless`` is the unpacked runtime's ``root`` directory, not the runtime
    directory itself. System QEMU receives an unchanged copy. Never mutate the
    caller's environment or add empty search components (which mean cwd to the
    dynamic loader). Keep inherited nonempty entries, in their original order.
    Binary discovery and explicit firmware selection remain with the caller.
    """
    env = dict(os.environ if environ is None else environ)
    if rootless is not None:
        root = Path(rootless).resolve()
        libraries = [str(root / 'lib/x86_64-linux-gnu'),
                     str(root / 'usr/lib/x86_64-linux-gnu')]
        libraries.extend(part for part in env.get('LD_LIBRARY_PATH', '').split(os.pathsep)
                         if part)
        env['LD_LIBRARY_PATH'] = os.pathsep.join(libraries)
        env['QEMU_MODULE_DIR'] = str(root / 'usr/lib/x86_64-linux-gnu/qemu')
    return env
