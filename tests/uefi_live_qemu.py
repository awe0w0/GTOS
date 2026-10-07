#!/usr/bin/env python3
"""Real UEFI/BIOS live-media qualification with writable private sentinel disks.

Run under Linux/WSL only after the hybrid live builder and ordinary BIOS ISO
builds complete (the existing QMP pipe helpers and disk tools require Linux).
QEMU uses TCG, QMP stdio, official OVMF code and a private writable VARS copy.
Only newly-created regular disk images are attached. All screenshots preserve
the actual GOP/Multiboot framebuffer dimensions; desktop state is never patched.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ['/boot/catch.gtapp', '/boot/native-fault.elf',
           '/boot/native-peer.elf', '/boot/browser-probe.elf']
LIVE_MARKERS = ('LIVE ATA WRITES DISABLED', 'LIVE RAM STORAGE READY',
                'LIVE BOOT PACKAGE INSTALLED', 'SETTINGS RAM SESSION ONLY')
LIVE_ENTRY_TOKENS = [('gtos-live-default', ()), ('gtos-live-bootlog', ('bootlog',)),
                     ('gtos-live-serial', ('bootlog', 'serial'))]


def require(value, message):
    if not value:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*arguments):
    return subprocess.check_output(['git', '-C', str(ROOT)] + list(arguments))


def tree_hashes(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob('*')) if path.is_file()}


def boot_entries(config):
    """Read each generated GRUB entry, preserving gated module command order."""
    lines = config.splitlines()
    entries = []
    for start, line in enumerate(lines):
        if not re.match(r'^\s*menuentry\s+', line):
            continue
        require(line.rstrip().endswith('{'), 'Generated GRUB menu header must open an entry')
        end = start + 1
        while end < len(lines) and lines[end].strip() != '}':
            end += 1
        require(end < len(lines), 'Generated GRUB entry must close')
        body = '\n'.join(lines[start + 1:end])
        identifier = re.search(r'--id(?:=|\s+)["\']?([A-Za-z0-9_-]+)', line)
        modules = re.findall(r'^\s*(?:if\s+)?module\s+([^;\s]+)(?:;|\s|$)', body, re.MULTILINE)
        kernels = re.findall(r'^\s*(?:if\s+)?multiboot\s+([^;\n]+)', body, re.MULTILINE)
        require(modules == MODULES, 'Each entry must load exactly the four original module slots in order')
        entries.append(dict(id=identifier.group(1) if identifier else None, modules=modules,
                            kernel_commands=[command.split() for command in kernels], body=body))
    require(entries, 'A real generated GRUB menu entry is required')
    return entries


def validate_live_entries(config, default_id='gtos-live-default', failure=None):
    entries = boot_entries(config)
    if failure:
        require([entry['id'] for entry in entries] == ['gtos-live-test'],
                'Failure media must have only the explicit diagnostic entry')
        modes = [('gtos-live-test', ('bootlog', 'bootlog-fail') if failure == 'kernel-panic'
                  else ('bootlog',))]
        default_id = 'gtos-live-test'
    else:
        require([entry['id'] for entry in entries] == [name for name, _ in LIVE_ENTRY_TOKENS],
                'Live media must preserve normal, verbose and explicit serial menu entries')
        modes = LIVE_ENTRY_TOKENS
    for entry, (_, additions) in zip(entries, modes):
        expected = [['/boot/GTOS.bin', 'live', 'uefi'] + list(additions),
                    ['/boot/GTOS.bin', 'live'] + list(additions)]
        require(len(entry['kernel_commands']) == 2, 'Each live entry requires exact EFI and BIOS branches')
        for actual, wanted in zip(entry['kernel_commands'], expected):
            require(actual and actual[0] == wanted[0] and len(actual) == len(wanted)
                    and set(actual[1:]) == set(wanted[1:]),
                    'Unexpected kernel command line in entry ' + str(entry['id']))
    defaults = re.findall(r'^\s*set\s+default=["\']?([^"\'\s]+)', config, re.MULTILINE)
    require(len(defaults) == 1, 'Generated live configuration requires one explicit default')
    selected = entries[int(defaults[0])]['id'] if defaults[0].isdigit() and int(defaults[0]) < len(entries) else defaults[0]
    require(selected == default_id, 'Selected live entry differs from requested explicit mode')
    return entries


def settings_record(generation, locale, theme):
    data = bytearray(512)
    data[:8] = b'GTSET01\0'
    struct.pack_into('<5I', data, 8, 1, 512, generation, locale, theme)
    struct.pack_into('<I', data, 508, zlib.crc32(data[:508]) & 0xffffffff)
    return bytes(data)


def read_settings(path):
    records = []
    with path.open('rb') as stream:
        stream.seek(259 * 512)
        for slot in range(2):
            data = stream.read(512)
            if len(data) != 512 or data[:8] != b'GTSET01\0':
                continue
            version, size, generation, locale, theme = struct.unpack_from('<5I', data, 8)
            if (version == 1 and size == 512 and locale in (0, 1) and theme in (0, 1)
                    and struct.unpack_from('<I', data, 508)[0] ==
                    (zlib.crc32(data[:508]) & 0xffffffff)):
                records.append(dict(slot=slot, generation=generation, locale=locale, theme=theme))
    require(records, 'Ordinary BIOS disk has no valid persisted settings record')
    if len(records) == 1:
        return records[0]
    delta = (records[1]['generation'] - records[0]['generation']) & 0xffffffff
    require(delta != 0x80000000, 'Ambiguous settings generations')
    return records[1] if 0 < delta < 0x80000000 else records[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path, help='Completed UEFI live builder evidence directory')
    parser.add_argument('ordinary_iso', type=pathlib.Path, help='Newly-built ordinary BIOS ISO')
    parser.add_argument('output', type=pathlib.Path, help='Fresh evidence directory')
    parser.add_argument('--kernel-sha256', required=True, help='Exact live GTOS.bin SHA-256')
    parser.add_argument('--ordinary-kernel-sha256', help='Exact ordinary GTOS.bin SHA-256 (default: live hash)')
    parser.add_argument('--source-sha256', required=True, type=pathlib.Path,
                        help='JSON of expected precommit repository source hashes')
    parser.add_argument('--source-base', help='Optional expected source base commit')
    parser.add_argument('--firmware-root', type=pathlib.Path,
                        help='Directory containing official OVMF_CODE.fd and OVMF_VARS.fd')
    parser.add_argument('--runtime-root', type=pathlib.Path,
                        help='Optional existing temporary directory for QEMU files')
    parser.add_argument('--qemu', help='Existing UEFI qemu-system-x86_64 executable')
    parser.add_argument('--bios-qemu', help='Existing BIOS qemu-system-i386 executable')
    args = parser.parse_args()
    from PIL import Image

    build = args.build.resolve()
    out = args.output.resolve()
    ordinary_iso = args.ordinary_iso.resolve()
    manifest_path = build / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    live_output = manifest['outputs']['iso']
    live_iso = pathlib.Path(live_output['path']).resolve()
    require(live_iso.is_file() and ordinary_iso.is_file(), 'Both real ISO files are required')
    require(digest(live_iso) == live_output['sha256'], 'Live ISO differs from builder manifest')
    expected_kernel = args.kernel_sha256.lower()
    expected_ordinary_kernel = (args.ordinary_kernel_sha256 or expected_kernel).lower()
    for value in (expected_kernel, expected_ordinary_kernel):
        require(re.fullmatch(r'[0-9a-f]{64}', value), 'Invalid kernel SHA-256')
    revision = git('rev-parse', 'HEAD').decode().strip()
    if args.source_base:
        expected_base = git('rev-parse', '--verify', args.source_base + '^{commit}').decode().strip()
        require(revision == expected_base, 'Expected source base differs from HEAD')
    expected_sources = json.loads(args.source_sha256.read_text())
    require(isinstance(expected_sources, dict) and expected_sources, 'Source SHA-256 proof is required')
    for name in ('src/kernel.cpp', 'src/loader.s', 'src/drivers/ata.cpp',
                 'src/storage/appstore.cpp', 'src/storage/settings.cpp',
                 'include/storage/liveblockdevice.h', 'src/storage/liveblockdevice.cpp',
                 'src/gui/modern_desktop.cpp', 'tools/build-uefi-live.py',
                 'tests/uefi_live_qemu.py'):
        require(name in expected_sources, 'Critical source hash missing: ' + name)
    for name, value in expected_sources.items():
        source = pathlib.PurePosixPath(name)
        require(not source.is_absolute() and '..' not in source.parts and '\\' not in name,
                'Source proof must use repository-relative paths: ' + name)
        require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value),
                'Invalid source SHA-256: ' + name)
        require(digest(ROOT / name) == value, 'Source SHA-256 differs: ' + name)
    require(not out.exists(), 'Choose a fresh evidence directory')
    runtime = out / 'guest'
    if args.runtime_root:
        require(args.runtime_root.is_dir(), '--runtime-root must already exist')
        runtime = args.runtime_root.resolve() / ('uefi-live-' + out.name)
        require(not runtime.exists(), 'Choose a fresh runtime directory')
    firmware = args.firmware_root
    if firmware is None:
        require(os.environ.get('GTOS_RUNTIME'), 'Set GTOS_RUNTIME or --firmware-root')
        firmware = pathlib.Path(os.environ['GTOS_RUNTIME']) / 'root/usr/share/OVMF'
    firmware = firmware.resolve()
    code, vars_template = firmware / 'OVMF_CODE.fd', firmware / 'OVMF_VARS.fd'
    require(code.is_file() and vars_template.is_file(), 'Official OVMF code/VARS template are required')
    require(code.stat().st_size > 0 and vars_template.stat().st_size > 0, 'OVMF files must be nonempty')
    firmware_hashes = {str(code): digest(code), str(vars_template): digest(vars_template)}
    out.mkdir(parents=True)
    runtime.mkdir()
    excluded_roots = (out, runtime, build)

    def source_snapshot():
        tracked = {name.decode() for name in git('ls-files', '-z').split(b'\0') if name}
        added = {name.decode() for name in
                 git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0') if name}
        result = {}
        for name in sorted(tracked | added | set(expected_sources)):
            path = ROOT / name
            if any(path.resolve() == directory or directory in path.resolve().parents
                   for directory in excluded_roots):
                continue
            result[name] = digest(path) if path.is_file() else None
        return result

    before = source_snapshot()
    (out / 'sources-before.json').write_text(json.dumps(before, indent=2) + '\n')
    state = dict(scope='Real x64 UEFI and BIOS live boot, RAM storage and physical disk no-write proof',
                 source_base=revision, expected_source_sha256=expected_sources,
                 builder_manifest_sha256=digest(manifest_path), live_iso_sha256=digest(live_iso),
                 ordinary_iso_sha256=digest(ordinary_iso), kernel_binary_sha256=expected_kernel,
                 ordinary_kernel_binary_sha256=expected_ordinary_kernel,
                 firmware_sha256=firmware_hashes, guest_pass=False,
                 chromium_guest_pass=False, source_before_sha256=digest(out / 'sources-before.json'),
                 cases=[])
    guest = None

    def record():
        state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (out / 'results.json').write_text(json.dumps(state, indent=2) + '\n')

    def run(name, command):
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([str(part) for part in command], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=240)

    def boot_hashes(name, iso):
        target = runtime / (name + '-extracted')
        run(name + '-iso-extract', ['xorriso', '-indev', iso, '-osirrox', 'on', '-extract', '/', target])
        result = tree_hashes(target)
        (out / (name + '-iso-files.json')).write_text(json.dumps(result, indent=2) + '\n')
        grub = (target / 'boot/grub/grub.cfg').read_text()
        boot_entries(grub)
        return target, result, grub

    record()
    try:
        live_tree, live_hashes, live_grub = boot_hashes('live', live_iso)
        ordinary_tree, ordinary_hashes, ordinary_grub = boot_hashes('ordinary', ordinary_iso)
        require(live_hashes['boot/GTOS.bin'] == expected_kernel, 'Actual live ISO kernel differs')
        require(ordinary_hashes['boot/GTOS.bin'] == expected_ordinary_kernel,
                'Actual ordinary BIOS ISO kernel differs')
        state['live_boot_entries'] = validate_live_entries(live_grub)
        require(re.findall(r'^\s*multiboot\s+([^\n]+)', ordinary_grub, re.MULTILINE) == ['/boot/GTOS.bin'],
                'Ordinary BIOS ISO must use the original default kernel command line')
        for item in manifest['input_files']:
            name = item['iso_path'].lstrip('/')
            require(name in live_hashes and live_hashes[name] == item['sha256'],
                    'Actual ISO input differs: ' + name)
            require((live_tree / name).stat().st_size == item['bytes'], 'ISO input size differs: ' + name)
        for name in MODULES:
            require(live_hashes[name.lstrip('/')] == ordinary_hashes[name.lstrip('/')],
                    'Live media must preserve each exact original native/package module: ' + name)
        efi = live_tree / 'EFI/BOOT/BOOTX64.EFI'
        fat = live_tree / 'efi.img'
        require(digest(efi) == manifest['outputs']['efi_loader']['sha256'], 'ISO EFI loader differs')
        require(digest(fat) == manifest['outputs']['efi_fat_image']['sha256'], 'ISO EFI FAT image differs')
        pe = efi.read_bytes()
        require(pe[:2] == b'MZ' and len(pe) >= 64, 'EFI loader must have a DOS/PE header')
        offset = struct.unpack_from('<I', pe, 0x3c)[0]
        require(offset <= len(pe) - 26 and pe[offset:offset + 4] == b'PE\0\0', 'Invalid EFI PE signature')
        machine = struct.unpack_from('<H', pe, offset + 4)[0]
        magic = struct.unpack_from('<H', pe, offset + 24)[0]
        require(machine == 0x8664 and magic == 0x20b, 'Actual EFI loader must be x64 PE32+')
        extracted_efi = runtime / 'efi-fat-BOOTX64.EFI'
        run('efi-fat-extract', ['mcopy', '-i', fat, '::/EFI/BOOT/BOOTX64.EFI', extracted_efi])
        require(extracted_efi.read_bytes() == pe, 'Actual boot FAT loader differs from outer EFI loader')
        run('live-el-torito-report', ['xorriso', '-indev', live_iso, '-report_el_torito', 'plain'])
        catalog = (out / 'live-el-torito-report.log').read_text()
        require(re.search(r'El Torito boot img\s*:\s*\d+\s+BIOS\b', catalog) and
                re.search(r'El Torito boot img\s*:\s*\d+\s+UEFI\b', catalog),
                'Actual hybrid ISO requires both BIOS and UEFI El Torito boot entries')
        state.update(exact_live_iso_boot_files_verified=True, exact_ordinary_iso_boot_files_verified=True,
                     published_modules_unchanged=True, actual_pe64_and_fat_bytes_verified=True,
                     hybrid_boot_catalog_verified=True, pe_machine=machine, pe_optional_magic=magic,
                     live_extracted_files_sha256=live_hashes, ordinary_extracted_files_sha256=ordinary_hashes)
        sys.path.insert(0, str(ROOT / 'tests'))
        from qemu_smoke import Guest
        from desktop_qemu import paddle
        sys.path.insert(0, str(ROOT / 'tools'))
        import disk as disktool
        from package import inspect_package
        from qemu_runtime import qemu_environment
        package = (live_tree / 'boot/catch.gtapp').read_bytes()
        require(inspect_package(package)['id'] == 'catch', 'Use the real shipped Catch package')

        class FirmwareGuest(Guest):
            def __init__(self, case, disk, iso, machine, memory, cpus, efi_boot, variables):
                self.out, self.log, self.sequence = case, case / 'debug.log', 0
                self.buffer = b''
                env = qemu_environment()
                engine = 'qemu-system-x86_64' if efi_boot else 'qemu-system-i386'
                qemu = (args.qemu if efi_boot else args.bios_qemu) or shutil.which(engine)
                if not qemu and os.environ.get('GTOS_RUNTIME'):
                    unpacked_root = pathlib.Path(os.environ['GTOS_RUNTIME']) / 'root'
                    candidate = unpacked_root / 'usr/bin' / engine
                    if candidate.is_file():
                        qemu = str(candidate)
                        env = qemu_environment(unpacked_root, env)
                        env['GTOS_QEMU_DATA_DIR'] = str(unpacked_root / 'usr/share/qemu')
                require(qemu, 'An existing ' + engine + ' executable is required')
                command = [qemu]
                if env.get('GTOS_QEMU_DATA_DIR'):
                    command += ['-L', env['GTOS_QEMU_DATA_DIR']]
                command += ['-machine', machine, '-accel', 'tcg', '-m', str(memory) + 'M',
                            '-smp', str(cpus), '-vga', 'std']
                if efi_boot:
                    command += ['-drive', 'if=pflash,format=raw,unit=0,readonly=on,file=' + str(code),
                                '-drive', 'if=pflash,format=raw,unit=1,file=' + str(variables)]
                # No readonly/snapshot block filter: the no-write proof uses a
                # genuinely writable raw disk on IDE or q35's AHCI controller.
                command += ['-cdrom', str(iso), '-boot', 'd',
                            '-drive', 'id=qualified-disk,file=' + str(disk) + ',format=raw,if=ide,index=0',
                            '-nic', 'none', '-display', 'none', '-qmp', 'stdio',
                            '-no-reboot', '-no-shutdown', '-debugcon', 'file:' + str(self.log),
                            '-global', 'isa-debugcon.iobase=0xe9']
                (case / 'qemu-command.json').write_text(json.dumps(command, indent=2) + '\n')
                self.err = (case / 'qemu.log').open('w')
                try:
                    self.p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                              stderr=self.err, env=env, bufsize=0)
                    self.line(15)
                    self.call('qmp_capabilities')
                except BaseException:
                    if hasattr(self, 'p') and self.p.poll() is None:
                        self.p.kill()
                        self.p.wait(timeout=10)
                    self.err.close()
                    raise
                self.px, self.py = 770, 16

            def screenshot(self, name):
                path = (self.out / (name + '.ppm')).resolve()
                self.call('screendump', {'filename': str(path)})
                image = Image.open(path).convert('RGB')
                require(image.size == (800, 600), 'Actual GOP/Multiboot framebuffer must be 800x600')
                image.save(self.out / (name + '.png'))
                return image

            def move(self, x, y):
                self.mouse(x - self.px, y - self.py)
                self.px, self.py = x, y
                time.sleep(.15)

            def click(self, x, y):
                self.move(x, y)
                self.mouse(0, 0, True)

            def disk_stats(self, name):
                blocks = self.call('query-block')
                stats = self.call('query-blockstats')
                (self.out / (name + '.json')).write_text(json.dumps(dict(blocks=blocks, stats=stats), indent=2) + '\n')
                devices = [block['device'] for block in blocks
                           if block.get('inserted', {}).get('file') == str(self.disk)]
                require(len(devices) == 1, 'QMP must identify exactly the writable sentinel disk')
                selected = [block['stats'] for block in stats if block.get('device') == devices[0]]
                require(len(selected) == 1, 'QMP must report stats for the writable sentinel disk')
                return selected[0]

        def new_disk(path, unknown=False, preinstalled=False, chinese=False):
            require(not path.exists(), 'Refuse to replace any existing disk')
            if unknown:
                data = bytearray((bytes((i * 73 + 19) & 255 for i in range(256))) * 4096)
                data[446:462] = struct.pack('<B3sB3sII', 0, b'\0\2\0', 0xee,
                                            b'\xff\xff\xff', 1, len(data) // 512 - 1)
                data[510:512] = b'\x55\xaa'
                data[512:520] = b'EFI PART'
                with path.open('xb') as stream:
                    stream.write(data)
            else:
                disktool.create(path, size_mib=1)
                image = disktool.Image(path, writable=True)
                try:
                    if preinstalled:
                        image.install(package)
                    image.stream.seek(259 * 512)
                    image.stream.write(settings_record(1, int(chinese), int(chinese)) * 2)
                    image.sync()
                finally:
                    image.close()
            require(path.is_file() and os.access(str(path), os.W_OK), 'Sentinel must be a writable regular image')
            return digest(path)

        def fresh_count(marker, operation, timeout=10):
            count = guest.text().count(marker)
            operation()
            end = time.monotonic() + timeout
            while guest.text().count(marker) <= count and time.monotonic() < end:
                require('PANIC' not in guest.text() and 'RUNTIME FAIL' not in guest.text(),
                        'Guest failed during actual desktop input')
                time.sleep(.03)
            require(guest.text().count(marker) == count + 1,
                    'Actual input must initiate exactly one ' + marker)

        def log_hex(marker):
            values = re.findall(r'^' + re.escape(marker) + r'([0-9A-F]{8})$',
                                guest.text(), re.MULTILINE)
            require(len(values) == 1, 'Exactly one actual kernel value is required: ' + marker)
            return int(values[0], 16)

        def runtime_health():
            require(not any(marker in guest.text() for marker in
                            ('PANIC', 'SELFTEST FAIL', 'RUNTIME FAIL')),
                    'Actual guest reported a kernel/selftest/runtime failure')

        def boot_baseline(live, efi_boot, cpus, expected_count):
            guest.wait('SCHEDULER RUNTIME PASS', 90)
            guest.wait('DESKTOP READY', 20)
            for marker in ('PHYSICAL SELFTEST PASS', 'PAGING PG WP NULL RO PASS', 'HEAP SELFTEST PASS',
                           'SYSCALL ABI PASS', 'DESKTOP MODE FRAMEBUFFER',
                           'APP STORE COUNT %08X' % expected_count):
                require(marker in guest.text(), 'Existing boot baseline missing: ' + marker)
            if live:
                for marker in LIVE_MARKERS:
                    require(marker in guest.text(), 'Required real live-mode marker missing: ' + marker)
            else:
                require(all(marker not in guest.text() for marker in LIVE_MARKERS),
                        'Ordinary BIOS must preserve its default persistent storage policy')
            if efi_boot:
                require('BOOT FIRMWARE UEFI X64' in guest.text(), 'Kernel must confirm the EFI handoff')
                require('PANIC UEFI GOP FRAMEBUFFER UNAVAILABLE' not in guest.text(), 'UEFI requires a valid GOP')
            else:
                require('BOOT FIRMWARE UEFI X64' not in guest.text(), 'BIOS must not report EFI firmware')
            runtime_health()
            detected = log_hex('CPU DETECTED ')
            online = log_hex('CPU ONLINE ')
            require(detected >= 1 and online == 1, 'Actual CPU/BSP scheduler inventory is invalid')
            inventory = re.findall(r'^CPU VENDOR .* SOURCE (.+)$', guest.text(), re.MULTILINE)
            require(len(inventory) == 1, 'Actual firmware processor inventory source is required')
            proof = dict(requested_vcpus=cpus, cpu_detected=detected, cpu_online=online,
                         cpu_inventory_source=inventory[0],
                         ap_initialized=log_hex('AP INITIALIZED '), ap_parked=log_hex('AP PARKED '),
                         ap_failed=log_hex('AP FAILED '), worker_configured=log_hex('WORKER CONFIGURED '),
                         worker_ready=log_hex('WORKER READY '), worker_failed=log_hex('WORKER FAILED '),
                         native_exit_code=None, native_reaped=None,
                         native_process_qualified=False, ap_worker_jobs_pass=False, limitations=[])
            require(proof['ap_failed'] == 0 and proof['worker_failed'] == 0,
                    'Actual AP or worker startup failed')
            limited = 'AP STARTUP LIMITED\n' in guest.text()
            if efi_boot and limited:
                require('AP STARTUP LIMITED\nno firmware processor inventory\n' in guest.text(),
                        'Only the observed missing-firmware-inventory limitation is accepted')
                require(inventory[0] == 'BSP only (firmware topology unavailable)' and detected == 1,
                        'Limited EFI boot must report its real unavailable processor inventory')
                require('AP STARTUP PASS\n' not in guest.text() and 'WORKER POOL READY\n' not in guest.text()
                        and 'WORKER POOL LIMITED\n' in guest.text(), 'Conflicting AP/worker status')
                require(all(proof[name] == 0 for name in
                            ('ap_initialized', 'ap_parked', 'worker_configured', 'worker_ready')),
                        'Limited EFI boot must not fabricate an initialized AP or ready worker')
                proof.update(ap_status='limited', ap_reason='no firmware processor inventory',
                             worker_pool_status='limited')
                proof['limitations'].append('UEFI firmware processor inventory and AP workers are unqualified')
            else:
                require(not limited and 'AP STARTUP PASS\n' in guest.text()
                        and 'WORKER POOL READY\n' in guest.text(), 'Required AP/worker baseline did not pass')
                require(detected == cpus, 'Strict processor inventory differs from configured vCPUs')
                guest.verify_workers(cpus, periodic=True)
                proof.update(ap_status='pass', ap_reason=None, worker_pool_status='ready',
                             ap_worker_jobs_pass=True)
            if efi_boot and 'NATIVE RUNTIME LIMITED\n' in guest.text():
                require(limited, 'Unexpected EFI native limitation without the observed inventory limitation')
                require('NATIVE RUNTIME PASS\n' not in guest.text(), 'Conflicting native runtime status')
                proof['native_runtime_status'] = 'limited'
                # The trace does not diagnose why native admission is limited.
                # Record it separately; do not attribute it to the AP reason.
                proof['native_limitation_reason'] = 'not diagnosed; actual kernel reported NATIVE RUNTIME LIMITED'
                proof['limitations'].append('UEFI native process execution/reaping is unqualified')
            else:
                guest.wait('NATIVE RUNTIME PASS', 90)
                guest.wait('BROWSER PLATFORM PROBE PASS ABI1', 20)
                guest.wait('BROWSER PROBE EXIT 00000000', 20)
                guest.wait('NATIVE REAPED 00000003', 20)
                require('NATIVE RUNTIME LIMITED\n' not in guest.text(), 'Conflicting native runtime status')
                proof.update(native_runtime_status='pass', native_limitation_reason=None,
                             native_exit_code=log_hex('BROWSER PROBE EXIT '),
                             native_reaped=log_hex('NATIVE REAPED '), native_process_qualified=True)
                require(proof['native_exit_code'] == 0 and proof['native_reaped'] == 3,
                        'Actual native probe must exit zero and reap all three processes')
            proof['uefi_native_process_qualified'] = proof['native_process_qualified'] if efi_boot else None
            proof.update(scheduler_runtime_pass=True, required_boot_baseline_pass=True,
                         real_framebuffer_800x600=True)
            runtime_health()
            guest.screenshot('boot-desktop')
            return proof

        def verify_runtime_current(proof):
            runtime_health()
            if proof['ap_worker_jobs_pass']:
                guest.verify_workers(proof['requested_vcpus'], periodic=True)
            else:
                require('AP STARTUP LIMITED\nno firmware processor inventory\n' in guest.text()
                        and 'WORKER POOL LIMITED\n' in guest.text(),
                        'Observed limited EFI AP/worker state changed unexpectedly')
                require(log_hex('AP INITIALIZED ') == 0 and log_hex('WORKER READY ') == 0,
                        'Do not report unqualified EFI workers as a pass')
            if not proof['native_process_qualified']:
                require('NATIVE RUNTIME LIMITED\n' in guest.text()
                        and 'NATIVE RUNTIME PASS\n' not in guest.text(),
                        'Observed limited EFI native state changed unexpectedly')

        def desktop_operations(live):
            guest.key('4')
            fresh_count('UI LANGUAGE CHANGED', lambda: guest.key('c'))
            require(guest.text().rsplit('UI LOCALE ', 1)[1].startswith('zh-CN\n'), 'Actual locale switch must select Chinese')
            fresh_count('UI THEME CHANGED', lambda: guest.key('t'))
            require(guest.text().rsplit('UI THEME ', 1)[1].startswith('light\n'), 'Actual theme switch must select light')
            appearance = guest.screenshot('chinese-light-appearance')
            require(appearance.getpixel((175, 300)) == (242, 245, 247), 'Theme must change real framebuffer pixels')
            guest.key('esc')
            guest.key('3')
            guest.wait('UI APPS', 10)
            fresh_count('APP DISK RELOAD OK', lambda: guest.key('r'))
            fresh_count('APP DISK RELOAD OK', lambda: guest.click(591, 144))
            if live:
                # The preinstalled package is removed, then installed through
                # the real bundled package path; Reload remounts the RAM store.
                guest.key('u')
                fresh_count('APP REMOVE OK', lambda: guest.key('ret'))
                guest.screenshot('chinese-ram-package-removed')
            fresh_count('APP INSTALL OK', lambda: guest.key('i'))
            fresh_count('APP DISK RELOAD OK', lambda: guest.key('r'))
            guest.screenshot('chinese-applications')
            fresh_count('APP LAUNCH OK', lambda: guest.key('ret'))
            before_x = paddle(guest.screenshot('chinese-catch-before'))
            require(before_x is not None, 'The real Catch paddle must be visible')
            guest.key('right', 350)
            after_x = paddle(guest.screenshot('chinese-catch-after-keyboard'))
            require(after_x is not None and after_x > before_x + 10, 'Actual keyboard must move the visible paddle')
            # Existing game titlebar close control. This is real PS/2 mouse input.
            fresh_count('APP CLOSE OK', lambda: guest.click(701, 146))
            guest.screenshot('chinese-catch-mouse-closed')
            if live:
                guest.key('3')
                guest.key('u')
                fresh_count('APP REMOVE OK', lambda: guest.key('ret'))
                guest.screenshot('chinese-ram-final-empty')
            return before_x, after_x

        definitions = [('uefi-pc-128M-4cpu-unknown', True, 'pc', 128, 4, True),
                       ('uefi-q35-128M-1cpu-valid', True, 'q35', 128, 1, False),
                       ('bios-live-64M-4cpu-valid', False, 'pc', 64, 4, False)]
        for name, efi_boot, machine, memory, cpus, unknown in definitions:
            case = runtime / name
            case.mkdir()
            disk = case / 'sentinel.img'
            disk_before = new_disk(disk, unknown=unknown, preinstalled=True, chinese=True)
            shutil.copyfile(disk, case / 'sentinel-before.img')
            variables = case / 'OVMF_VARS.private.fd'
            if efi_boot:
                shutil.copyfile(vars_template, variables)
                require(digest(variables) == firmware_hashes[str(vars_template)], 'Private VARS copy differs')
            item = dict(name=name, firmware='UEFI x64 OVMF' if efi_boot else 'BIOS', machine=machine,
                        memory_mib=memory, vcpus=cpus, live=True, writable_disk=True,
                        disk_kind='unknown MBR/GPT-like sentinel' if unknown else 'valid GTOS Catch and Chinese/light settings',
                        disk_sha256_before=disk_before, guest_pass=False)
            state['cases'].append(item)
            state.update(stage='guest-running', current_case=name)
            record()
            begin = time.monotonic()
            first = case / 'first-boot'
            first.mkdir()
            guest = FirmwareGuest(first, disk, live_iso, machine, memory, cpus, efi_boot, variables)
            guest.disk = disk
            first_proof = boot_baseline(True, efi_boot, cpus, 1)
            item['first_boot_qualification'] = first_proof
            require(guest.text().rsplit('UI LOCALE ', 1)[1].startswith('en\n') and
                    guest.text().rsplit('UI THEME ', 1)[1].startswith('dark\n'),
                    'Live RAM defaults must ignore any physical Chinese/light settings')
            initial_stats = guest.disk_stats('sentinel-stats-after-boot')
            before_x, after_x = desktop_operations(True)
            verify_runtime_current(first_proof)
            final_stats = guest.disk_stats('sentinel-stats-after-input')
            for stats in (initial_stats, final_stats):
                require(stats.get('wr_bytes') == 0 and stats.get('wr_operations') == 0,
                        'Writable physical sentinel received a block write')
            guest.close()
            guest = None
            require(disk.read_bytes() == (case / 'sentinel-before.img').read_bytes(),
                    'Live boot or GUI operations changed any physical sentinel byte')
            second = case / 'second-boot'
            second.mkdir()
            guest = FirmwareGuest(second, disk, live_iso, machine, memory, cpus, efi_boot, variables)
            guest.disk = disk
            second_proof = boot_baseline(True, efi_boot, cpus, 1)
            item['second_boot_qualification'] = second_proof
            require(guest.text().rsplit('UI LOCALE ', 1)[1].startswith('en\n') and
                    guest.text().rsplit('UI THEME ', 1)[1].startswith('dark\n'),
                    'Full reboot must discard live RAM locale/theme changes')
            guest.key('3')
            guest.screenshot('reboot-fresh-ram-applications')
            fresh_count('APP LAUNCH OK', lambda: guest.key('ret'))
            require(paddle(guest.screenshot('reboot-preinstalled-catch')) is not None,
                    'Reboot must restore the bundled Catch removed from the first RAM session')
            reboot_stats = guest.disk_stats('sentinel-stats-after-reboot')
            require(reboot_stats.get('wr_bytes') == 0 and reboot_stats.get('wr_operations') == 0,
                    'Writable sentinel received a block write on reboot')
            verify_runtime_current(second_proof)
            guest.close()
            guest = None
            disk_after = digest(disk)
            require(disk_after == disk_before, 'Live reboot changed physical sentinel bytes')
            item.update(guest_pass=True, native_exit_code=first_proof['native_exit_code'],
                        native_reaped=first_proof['native_reaped'],
                        native_process_qualified=first_proof['native_process_qualified'] and
                                                 second_proof['native_process_qualified'],
                        uefi_native_process_qualified=(first_proof['native_process_qualified'] and
                                                       second_proof['native_process_qualified']) if efi_boot else None,
                        required_boot_baseline_pass=True,
                        real_framebuffer_800x600=True, keyboard_and_mouse_reload_pass=True,
                        real_catch_keyboard_and_mouse_pass=True, chinese_and_light_pixels_pass=True,
                        real_ram_install_remove_reload_pass=True, live_ram_reset_on_reboot_pass=True,
                        physical_disk_bytes_unchanged=True, disk_sha256_after=disk_after,
                        sentinel_stats_after_boot=initial_stats, sentinel_stats_after_input=final_stats,
                        sentinel_stats_after_reboot=reboot_stats, game_paddle_before_x=before_x,
                        game_paddle_after_x=after_x,
                        ap_worker_jobs_pass=first_proof['ap_worker_jobs_pass'] and second_proof['ap_worker_jobs_pass'],
                        seconds=time.monotonic() - begin)
            if efi_boot:
                item.update(private_vars_sha256_before=firmware_hashes[str(vars_template)],
                            private_vars_sha256_after=digest(variables),
                            official_code_read_only=True, no_csm_ovmf=True)
            record()

        case = runtime / 'bios-ordinary-64M-4cpu-persistence'
        case.mkdir()
        disk = case / 'persistent.img'
        disk_before = new_disk(disk)
        shutil.copyfile(disk, case / 'persistent-before.img')
        state.update(stage='ordinary-persistence-running', current_case=case.name)
        item = dict(name=case.name, firmware='BIOS', machine='pc', memory_mib=64, vcpus=4,
                    live=False, writable_disk=True, disk_sha256_before=disk_before, guest_pass=False)
        state['cases'].append(item)
        record()
        first = case / 'first-boot'
        first.mkdir()
        guest = FirmwareGuest(first, disk, ordinary_iso, 'pc', 64, 4, False, None)
        guest.disk = disk
        first_proof = boot_baseline(False, False, 4, 0)
        item['first_boot_qualification'] = first_proof
        before_x, after_x = desktop_operations(False)
        stats = guest.disk_stats('persistent-stats-after-input')
        require(stats.get('wr_bytes', 0) > 0 and stats.get('wr_operations', 0) > 0,
                'Positive ordinary BIOS control must actually write its attached disk')
        guest.close()
        guest = None
        require(digest(disk) != disk_before, 'Ordinary persistent control must change disk bytes')
        image = disktool.Image(disk)
        try:
            require(image.read('catch') == package, 'Actual disk must retain the installed Catch bytes')
        finally:
            image.close()
        persisted = read_settings(disk)
        require(persisted['locale'] == 1 and persisted['theme'] == 1,
                'Actual disk settings must retain Chinese/light values')
        disk_written = digest(disk)
        second = case / 'second-boot'
        second.mkdir()
        guest = FirmwareGuest(second, disk, ordinary_iso, 'pc', 64, 4, False, None)
        guest.disk = disk
        second_proof = boot_baseline(False, False, 4, 1)
        item['second_boot_qualification'] = second_proof
        require(guest.text().rsplit('UI LOCALE ', 1)[1].startswith('zh-CN\n') and
                guest.text().rsplit('UI THEME ', 1)[1].startswith('light\n'),
                'Ordinary BIOS must reload persisted Chinese/light settings on full reboot')
        guest.key('3')
        guest.screenshot('persisted-chinese-applications')
        fresh_count('APP LAUNCH OK', lambda: guest.key('ret'))
        require(paddle(guest.screenshot('persisted-chinese-catch')) is not None,
                'Ordinary BIOS must launch the package persisted by its previous guest')
        reboot_stats = guest.disk_stats('persistent-stats-after-reboot')
        guest.close()
        guest = None
        require(digest(disk) == disk_written, 'Read-only ordinary reboot/game launch must preserve disk bytes')
        item.update(guest_pass=True, native_exit_code=first_proof['native_exit_code'],
                    native_reaped=first_proof['native_reaped'], native_process_qualified=True,
                    required_boot_baseline_pass=True, ap_worker_jobs_pass=True,
                    positive_disk_write_pass=True, package_bytes_persisted=True,
                    settings_records_persisted= persisted, full_reboot_persistence_pass=True,
                    disk_sha256_written=disk_written, disk_sha256_after=digest(disk),
                    persistent_stats_after_input=stats, persistent_stats_after_reboot=reboot_stats,
                    game_paddle_before_x=before_x, game_paddle_after_x=after_x)
        require(before == source_snapshot(), 'Repository sources changed during qualification')
        require(digest(live_iso) == state['live_iso_sha256'] and
                digest(ordinary_iso) == state['ordinary_iso_sha256'], 'ISO changed during qualification')
        require(all(digest(pathlib.Path(path)) == value for path, value in firmware_hashes.items()),
                'Official OVMF code or VARS template changed')
        state.update(stage='passed', guest_pass=True, completed_cases=4,
                     tracked_and_new_source_hashes_unchanged=True, input_isos_unchanged=True,
                     official_firmware_unchanged=True,
                     uefi_native_processes_qualified=all(case['native_process_qualified'] for case in
                                                        state['cases'] if case['firmware'] == 'UEFI x64 OVMF'),
                     uefi_ap_workers_qualified=all(case['ap_worker_jobs_pass'] for case in
                                                  state['cases'] if case['firmware'] == 'UEFI x64 OVMF'))
    except Exception as error:
        state.update(stage='failed', error=repr(error))
        if guest:
            try:
                guest.call('screendump', {'filename': str((guest.out / 'failure-framebuffer.ppm').resolve())})
                Image.open(guest.out / 'failure-framebuffer.ppm').convert('RGB').save(
                    guest.out / 'failure-framebuffer.png')
                state['failure_screenshot'] = str(guest.out / 'failure-framebuffer.png')
            except Exception as capture_error:
                state['failure_screenshot_error'] = repr(capture_error)
        raise
    finally:
        if guest:
            guest.close()
        after = source_snapshot()
        (out / 'sources-after.json').write_text(json.dumps(after, indent=2) + '\n')
        state['tracked_and_new_source_hashes_unchanged'] = before == after
        if runtime != out / 'guest':
            shutil.copytree(runtime, out / 'guest')
        record()
    print(json.dumps(state, indent=2))


if __name__ == '__main__':
    main()
