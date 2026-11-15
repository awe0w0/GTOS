"""Build and boot the production PCnet driver on an isolated QEMU hub."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys


REPO = Path(__file__).resolve().parents[1]
SOURCES = (
    'src/drivers/amd_am79c973.cpp', 'src/drivers/driver.cpp',
    'src/gdt.cpp', 'src/hardwarecommunication/interrupts.cpp',
    'src/hardwarecommunication/pci.cpp', 'src/hardwarecommunication/port.cpp',
    'src/multitasking.cpp', 'src/memorymanagement.cpp',
    'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp',
    'src/process/native_surface.cpp', 'src/process/native_windows.cpp',
    'src/process/native_fp.cpp', 'src/process/resources.cpp', 'src/process/elf32.cpp',
    'src/memory/process_address_space.cpp', 'src/memory/paging.cpp',
    'src/memory/physical.cpp', 'src/memory/bootstrap.cpp',
    'src/net/etherframe.cpp', 'src/net/arp.cpp', 'src/net/ipv4.cpp', 'src/net/icmp.cpp',
    'apps/native_process_info_probe/memory.cc',
    'tests/i386_pure_virtual_runtime.cpp', 'tests/pcnet_smoke.cpp',
)
ASSEMBLY = ('tests/native_process_loader.s', 'src/process/native_fp.s',
            'src/hardwarecommunication/interruptstubs.s')
LEVELS = ('O0', 'O2')


def run_command(argv, log, expected=0, timeout=120):
    with log.open('xb') as stream:
        child = subprocess.run(argv, cwd=REPO, stdout=stream,
                               stderr=subprocess.STDOUT, timeout=timeout, check=False)
    if child.returncode != expected:
        raise RuntimeError(f'{argv[0]} returned {child.returncode}; expected {expected}: {log}')


def file_identity(path):
    return dict(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def build(output, probe):
    output.mkdir()
    kernel_flags = (REPO / 'tools/kernel-cxxflags').read_text().split()
    common = ['-m32', '-std=c++11', '-ffreestanding', '-nostdlib', '-nostdinc',
              '-fno-builtin', '-fno-exceptions', '-fno-rtti', '-fno-pie',
              '-fno-stack-protector', '-fno-use-cxa-atexit', '-fno-threadsafe-statics',
              '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-Wno-write-strings', '-Wno-unused-parameter',
              '-Iinclude', *kernel_flags]
    cxx = os.environ.get('CXX', 'g++')
    grub = os.environ.get('GTOS_GRUB_MKRESCUE', 'grub-mkrescue')
    identity = {'probe': probe}
    sources = SOURCES if probe == 'pcnet' else SOURCES[:-1] + (
        'src/net/tcp_client.cpp', 'tests/tcp_client_smoke.cpp')
    for level in LEVELS:
        work = output / level
        work.mkdir()
        objects = []
        for source in sources + ASSEMBLY:
            stem = Path(source).name
            obj = work / (stem + '.o')
            command = ['as', '--32', source] if source in ASSEMBLY else [
                cxx, *common, '-' + level, '-c', source]
            run_command([*command, '-o', obj], work / (stem + '.log'))
            objects.append(obj)
        kernel = work / 'kernel.bin'
        link_map = work / 'kernel.map'
        run_command(['ld', '-melf_i386', '--gc-sections', '-T', 'tests/native_process_smoke.ld',
                     '-Map', link_map, '-o', kernel, *objects], work / 'link.log')
        run_command([sys.executable, 'tools/audit-kernel-instructions.py', '--map', link_map,
                     '--source-root', REPO, kernel], work / 'audit.log')
        tree = work / 'iso'
        config = tree / 'boot/grub/grub.cfg'
        config.parent.mkdir(parents=True)
        config.write_text('set timeout=0\nmenuentry "PCnet" {\n multiboot /boot/pcnet.bin\n boot\n}\n')
        shutil.copyfile(kernel, tree / 'boot/pcnet.bin')
        iso = output / (level + '.iso')
        run_command([grub, '--output=' + str(iso), tree], work / 'grub.log')
        iso.chmod(0o444)
        identity[iso.name] = file_identity(iso)
        print('PCNET BUILD', iso.name, identity[iso.name])
    manifest = output / 'identity.json'
    manifest.write_text(json.dumps(identity, indent=2) + '\n')
    manifest.chmod(0o444)


def verified_iso(publication, identity, level):
    iso = publication / (level + '.iso')
    if iso.is_symlink() or iso.stat().st_mode & 0o222 or file_identity(iso) != identity[iso.name]:
        raise RuntimeError(f'PCnet ISO identity changed: {iso}')
    return iso


def exercise(publication, output, probe):
    output.mkdir()
    identity = json.loads((publication / 'identity.json').read_text())
    if identity.get('probe', 'pcnet') != probe:
        raise RuntimeError('PCnet test publication has a different probe')
    if probe == 'tcp':
        exercise_tcp(publication, output, identity)
        return
    qemu = os.environ.get('GTOS_QEMU_SYSTEM_I386', 'qemu-system-i386')
    data = os.environ.get('GTOS_QEMU_DATA_DIR')
    for level in LEVELS:
        iso = verified_iso(publication, identity, level)
        for connected in (True, False):
            case = level + ('-connected' if connected else '-disconnected')
            guest = output / (case + '.guest.log')
            right_hub = 0 if connected else 1
            command = [qemu, '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max',
                       '-m', '32', '-smp', '1', '-boot', 'd', '-cdrom', iso,
                       '-nic', 'none', '-netdev', 'hubport,id=left,hubid=0',
                       '-device', 'pcnet,addr=0x05,netdev=left,mac=02:00:00:00:00:01,romfile=',
                       '-netdev', f'hubport,id=right,hubid={right_hub}',
                       '-device', 'pcnet,addr=0x06,netdev=right,mac=02:00:00:00:00:02,romfile=',
                       '-display', 'none', '-serial', 'none', '-monitor', 'none',
                       '-debugcon', f'file:{guest}', '-device', 'isa-debug-exit,iobase=0xf4,iosize=4',
                       '-no-reboot']
            if data:
                command.extend(['-L', data])
            run_command(command, output / (case + '.qemu.log'), 33 if connected else 65, timeout=60)
            lines = guest.read_text().splitlines()
            expected = (['PCNET FRAME COUNTS left=00000020 right=00000020',
                         'ping response from c0.00.02.02', 'ping response from c0.00.02.02',
                         'PCNET IPV4 ICMP PASS', 'PCNET GUEST PASS']
                        if connected else ['PCnet frame count', 'PCNET GUEST FAIL'])
            if lines != ['AMD am79c973', 'AMD am79c973', *expected]:
                raise RuntimeError(f'Unexpected PCnet guest transcript: {guest}')
            print('PCNET', case, 'PASS', file_identity(guest))


def exercise_tcp(publication, output, identity):
    qemu = os.environ.get('GTOS_QEMU_SYSTEM_I386', 'qemu-system-i386')
    data = os.environ.get('GTOS_QEMU_DATA_DIR')
    from tcp_http_peer import request, response
    for level in LEVELS:
        iso = verified_iso(publication, identity, level)
        transcript = output / (level + '.http-request.raw')
        address = output / (level + '.sock')
        guest = output / (level + '.guest.log')
        # The only forwarded endpoint is a Unix socket inside this stage.
        isolated = 'user,id=fixture,restrict=on,ipv6=off'
        network = f'{isolated},guestfwd=tcp:10.0.2.100:8080-unix:{address}'
        capture = f'filter-dump,id=wire,netdev=fixture,file={output / (level + ".pcap")}'
        command = [qemu, '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max',
                   '-m', '32', '-smp', '1', '-boot', 'd', '-cdrom', iso,
                   '-nic', 'none', '-netdev', network,
                   '-device', 'pcnet,addr=0x05,netdev=fixture,romfile=',
                   '-object', capture,
                   '-display', 'none', '-serial', 'none', '-monitor', 'none',
                   '-debugcon', f'file:{guest}', '-device', 'isa-debug-exit,iobase=0xf4,iosize=4',
                   '-no-reboot']
        if data:
            command.extend(['-L', data])
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener, \
                (output / (level + '.qemu.log')).open('xb') as log:
            listener.bind(str(address))
            listener.listen(1)
            listener.settimeout(25)
            with subprocess.Popen(command, cwd=REPO, stdout=log, stderr=subprocess.STDOUT) as child:
                try:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(25)
                        received = bytearray()
                        while len(received) < len(request):
                            part = connection.recv(len(request) - len(received))
                            if not part:
                                raise RuntimeError('TCP fixture closed during its HTTP request')
                            received.extend(part)
                        with transcript.open('xb') as trace:
                            trace.write(received)
                        if received != request:
                            raise RuntimeError('TCP peer received different request bytes')
                        connection.sendall(response)
                        result = child.wait(timeout=45)
                    if result != 33:
                        raise RuntimeError(f'TCP guest returned {result}: {guest}')
                finally:
                    if child.poll() is None:
                        child.kill()
        if guest.read_text().splitlines() != [
                'AMD am79c973', 'TCP CONNECTED AFTER ARP', 'TCP HTTP RESPONSE VERIFIED',
                'PCNET TCP HTTP PASS', 'PCNET GUEST PASS']:
            raise RuntimeError(f'Unexpected TCP guest transcript: {guest}')
        print('PCNET TCP', level, 'PASS', file_identity(guest))
        no_peer = output / (level + '-no-peer.guest.log')
        refusal = command.copy()
        refusal[command.index(network)] = isolated
        refusal[command.index(capture)] = f'filter-dump,id=wire,netdev=fixture,file={output / (level + "-no-peer.pcap")}'
        refusal[command.index(f'file:{guest}')] = f'file:{no_peer}'
        run_command(refusal, output / (level + '-no-peer.qemu.log'), expected=65, timeout=60)
        if no_peer.read_text().splitlines() != [
                'AMD am79c973', 'TCP CONNECT ERROR ffffff92', 'PCNET GUEST FAIL']:
            raise RuntimeError(f'Expected TCP connect timeout without a forwarding rule: {no_peer}')
        print('PCNET TCP', level, 'NO PEER PASS', file_identity(no_peer))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', choices=('pcnet', 'tcp'), default='pcnet')
    modes = parser.add_subparsers(dest='mode', required=True)
    modes.add_parser('build').add_argument('output', type=Path)
    run = modes.add_parser('run')
    run.add_argument('publication', type=Path)
    run.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.mode == 'build':
        build(args.output.resolve(), args.probe)
    else:
        exercise(args.publication.resolve(), args.output.resolve(), args.probe)
