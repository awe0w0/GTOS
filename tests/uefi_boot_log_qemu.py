#!/usr/bin/env python3
"""Qualify real UEFI boot logs, the desktop viewer, COM1 and stopped errors.

Run with Linux/WSL Python and Pillow after source freeze and private live media
builds. No existing test main is called. Screenshots come from QEMU's
actual framebuffer. E9 is an emulator debug port; COM1 uses a separate actual
guest UART capture. Loader-only L02/L03 screenshots require explicit visual
review because loader console text is not a kernel log or an OCR result.
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

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
PHASES = ['BOOT B01 HANDOFF', 'BOOT B02 MEMORY', 'BOOT B03 GDT IDT SCHEDULER',
          'BOOT B04 RAM STORAGE', 'BOOT B05 GRAPHICS', 'BOOT B06 PAGING',
          'BOOT B07 PS2 PIT IRQ', 'BOOT B08 DESKTOP']
LIVE_MARKERS = ['LIVE ATA WRITES DISABLED', 'LIVE RAM STORAGE READY',
                'LIVE BOOT PACKAGE INSTALLED', 'SETTINGS RAM SESSION ONLY']
PAUSED = 'BOOT LOG PAUSED - PRESS ANY KEY FOR DESKTOP'
SERIAL = 'BOOT SERIAL COM1 115200 8N1'
PANIC = 'PANIC E99 PHASE B02 SAFE DIAGNOSTIC FAILURE'


def require(value, message):
    if not value:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_hashes(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob('*')) if path.is_file()}


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('normal_build', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path, help='New evidence directory')
    parser.add_argument('--verbose-build', required=True, type=pathlib.Path)
    parser.add_argument('--serial-build', required=True, type=pathlib.Path)
    parser.add_argument('--panic-build', required=True, type=pathlib.Path)
    parser.add_argument('--loader-fail-build', required=True, type=pathlib.Path)
    parser.add_argument('--module-fail-build', type=pathlib.Path,
                        help='Optional sixth private missing-module build for visible L03')
    parser.add_argument('--source-root', type=pathlib.Path, default=ROOT)
    parser.add_argument('--source-sha256', required=True, type=pathlib.Path)
    parser.add_argument('--source-base')
    parser.add_argument('--kernel-sha256', required=True)
    parser.add_argument('--firmware-root', type=pathlib.Path)
    parser.add_argument('--runtime-root', type=pathlib.Path)
    parser.add_argument('--qemu', help='Existing qemu-system-x86_64 executable')
    parser.add_argument('--loader-settle-seconds', type=float, default=45,
                        help='Bounded time for EFI loader-only error (15..90 seconds)')
    args = parser.parse_args()
    from PIL import Image, ImageChops
    root = args.source_root.resolve()
    out = args.output.resolve()
    require(not out.exists(), 'Choose a fresh evidence directory')
    require(15 <= args.loader_settle_seconds <= 90, 'Loader settle time must be 15..90 seconds')
    expected_kernel = args.kernel_sha256.lower()
    require(re.fullmatch(r'[0-9a-f]{64}', expected_kernel), 'Exact kernel SHA-256 is required')
    expected_sources = json.loads(args.source_sha256.read_text())
    require(isinstance(expected_sources, dict) and expected_sources, 'Exact source proof is required')
    for name in ('src/kernel.cpp', 'src/gui/modern_desktop.cpp', 'src/gui/modern_render.cpp',
                 'tools/build-uefi-live.py', 'tests/uefi_live_qemu.py', 'tests/uefi_boot_log_qemu.py'):
        require(name in expected_sources, 'Critical source hash missing: ' + name)
    for name, value in expected_sources.items():
        relative = pathlib.PurePosixPath(name)
        require(not relative.is_absolute() and '..' not in relative.parts and '\\' not in name,
                'Source paths must be repository relative: ' + name)
        require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value), 'Invalid source SHA: ' + name)
        require(digest(root / name) == value, 'Source proof differs: ' + name)
    require(digest(pathlib.Path(__file__).resolve()) == expected_sources['tests/uefi_boot_log_qemu.py'],
            'Executed log driver differs from the qualified repository source')
    revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD']).decode().strip()
    if args.source_base:
        expected_base = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--verify',
                                                args.source_base + '^{commit}']).decode().strip()
        require(revision == expected_base, 'Source base differs')
    firmware = args.firmware_root
    if firmware is None:
        require(os.environ.get('GTOS_RUNTIME'), 'Set GTOS_RUNTIME or --firmware-root')
        firmware = pathlib.Path(os.environ['GTOS_RUNTIME']) / 'root/usr/share/OVMF'
    firmware = firmware.resolve()
    code, vars_template = firmware / 'OVMF_CODE.fd', firmware / 'OVMF_VARS.fd'
    require(code.is_file() and vars_template.is_file(), 'Existing official OVMF files are required')
    firmware_hashes = {str(code): digest(code), str(vars_template): digest(vars_template)}
    builds = [('normal', args.normal_build.resolve(), 'gtos-live-default', None),
              ('verbose', args.verbose_build.resolve(), 'gtos-live-bootlog', None),
              ('serial', args.serial_build.resolve(), 'gtos-live-serial', None),
              ('kernel-panic', args.panic_build.resolve(), 'gtos-live-test', 'kernel-panic'),
              ('loader-missing-kernel', args.loader_fail_build.resolve(), 'gtos-live-test', 'missing-kernel')]
    if args.module_fail_build:
        builds.append(('loader-missing-module', args.module_fail_build.resolve(),
                       'gtos-live-test', 'missing-module'))
    runtime = out / 'guest'
    if args.runtime_root:
        require(args.runtime_root.is_dir(), 'Runtime parent must already exist')
        runtime = args.runtime_root.resolve() / ('uefi-boot-log-' + out.name)
        require(not runtime.exists(), 'Choose a fresh runtime directory')
    out.mkdir(parents=True)
    runtime.mkdir()
    excluded = [out, runtime] + [build for _, build, _, _ in builds]

    def sources():
        names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--cached', '--others',
                                         '--exclude-standard', '-z']).decode().split('\0')
        result = {}
        for name in sorted(set(filter(None, names)) | set(expected_sources)):
            path = root / name
            resolved = path.resolve()
            if any(resolved == directory or directory in resolved.parents for directory in excluded):
                continue
            result[name] = digest(path) if path.is_file() else None
        return result

    before = sources()
    save(out / 'sources-before.json', before)
    state = dict(schema='gtos-uefi-boot-log-guest-v1', source_base=revision,
                 expected_source_sha256=expected_sources, kernel_binary_sha256=expected_kernel,
                 official_firmware_sha256=firmware_hashes,
                 scope='Real phased kernel log/viewer/COM1 and stopped loader/kernel errors',
                 e9_transport='Emulator debug port 0xE9, not physical COM1',
                 serial_transport='Separate QEMU COM1 UART file device',
                 firmware_boundary='OVMF starts before the GTOS loader; kernel logs begin after loader handoff',
                 automated_checks_pass=False, guest_pass=False, chromium_guest_pass=False,
                 loader_visual_review_pending=True, cases=[])
    guest = None

    def record():
        state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save(out / 'results.json', state)

    def run(name, command):
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([str(part) for part in command], stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=240)

    sys.path.insert(0, str(root / 'tests'))
    from uefi_live_qemu import MODULES, validate_live_entries
    from qemu_smoke import Guest
    sys.path.insert(0, str(root / 'tools'))
    from qemu_runtime import qemu_environment

    class LogGuest(Guest):
        def __init__(self, case, iso, disk, variables):
            self.out, self.log, self.sequence = case, case / 'debug-e9.log', 0
            self.buffer = b''
            self.disk, self.serial = disk, case / 'serial-com1.log'
            env = qemu_environment()
            qemu = args.qemu or shutil.which('qemu-system-x86_64')
            if not qemu and os.environ.get('GTOS_RUNTIME'):
                unpacked = pathlib.Path(os.environ['GTOS_RUNTIME']) / 'root'
                candidate = unpacked / 'usr/bin/qemu-system-x86_64'
                if candidate.is_file():
                    qemu = str(candidate)
                    env = qemu_environment(unpacked, env)
                    env['GTOS_QEMU_DATA_DIR'] = str(unpacked / 'usr/share/qemu')
            require(qemu, 'Existing qemu-system-x86_64 is required')
            command = [qemu]
            if env.get('GTOS_QEMU_DATA_DIR'):
                command += ['-L', env['GTOS_QEMU_DATA_DIR']]
            command += ['-machine', 'pc', '-accel', 'tcg', '-m', '128M', '-smp', '4', '-vga', 'std',
                        '-drive', 'if=pflash,format=raw,unit=0,readonly=on,file=' + str(code),
                        '-drive', 'if=pflash,format=raw,unit=1,file=' + str(variables),
                        '-cdrom', str(iso), '-boot', 'd',
                        '-drive', 'id=qualified-disk,file=' + str(disk) + ',format=raw,if=ide,index=0',
                        '-serial', 'file:' + str(self.serial), '-nic', 'none', '-display', 'none',
                        '-qmp', 'stdio', '-no-reboot', '-no-shutdown',
                        '-debugcon', 'file:' + str(self.log), '-global', 'isa-debugcon.iobase=0xe9']
            save(case / 'qemu-command.json', command)
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

        def capture(self, name, kernel_frame=True):
            path = (self.out / (name + '.ppm')).resolve()
            self.call('screendump', {'filename': str(path)})
            image = Image.open(path).convert('RGB')
            if kernel_frame:
                require(image.size == (800, 600), 'Actual kernel GOP framebuffer must be 800x600')
            image.save(self.out / (name + '.png'))
            return image

        def uart(self):
            return self.serial.read_bytes().decode('utf-8', errors='replace') if self.serial.exists() else ''

        def key_event(self, key, down):
            event = {'type': 'key', 'data': {'down': down,
                                           'key': {'type': 'qcode', 'data': key}}}
            self.call('input-send-event', {'events': [event]})
            return event

        def block_stats(self):
            blocks, stats = self.call('query-block'), self.call('query-blockstats')
            save(self.out / 'writable-sentinel-block-stats.json', dict(blocks=blocks, stats=stats))
            matching = [block['device'] for block in blocks
                        if block.get('inserted', {}).get('file') == str(self.disk)]
            require(len(matching) == 1, 'Identify exactly the attached writable private sentinel')
            found = [block['stats'] for block in stats if block.get('device') == matching[0]]
            require(len(found) == 1, 'Actual sentinel QMP statistics are required')
            require(found[0].get('wr_bytes') == 0 and found[0].get('wr_operations') == 0,
                    'Physical sentinel received a guest block write')
            return found[0]

    def wait_for(marker, timeout=90, expected_panic=False):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            text = guest.text()
            require('SELFTEST FAIL' not in text and 'RUNTIME FAIL' not in text, 'Guest selftest/runtime failed')
            if not expected_panic:
                require('PANIC' not in text, 'Unexpected kernel panic')
            if marker in text:
                return text
            require(guest.p.poll() is None, 'QEMU exited before ' + marker)
            time.sleep(.03)
        raise TimeoutError('Expected actual log marker: ' + marker + '\n' + guest.text())

    def phase_order(text):
        positions = [text.find(marker) for marker in PHASES]
        require(all(position >= 0 for position in positions) and positions == sorted(positions)
                and len(set(positions)) == len(positions), 'Actual boot phases must occur in B01..B08 order')

    def desktop_baseline():
        wait_for('DESKTOP READY')
        wait_for('SCHEDULER RUNTIME PASS')
        text = guest.text()
        phase_order(text)
        for marker in LIVE_MARKERS + ['BOOT FIRMWARE UEFI X64', 'DESKTOP MODE FRAMEBUFFER',
                                      'PAGING PG WP NULL RO PASS', 'HEAP SELFTEST PASS',
                                      'PHYSICAL SELFTEST PASS', 'SYSCALL ABI PASS', 'APP STORE COUNT 00000001']:
            require(marker in text, 'Mandatory real desktop/live baseline missing: ' + marker)
        inventory = re.findall(r'^CPU DETECTED ([0-9A-F]{8})$', text, re.MULTILINE)
        require(len(inventory) == 1, 'Actual detected processor inventory is required')
        result = dict(cpu_requested=4, cpu_detected=int(inventory[0], 16),
                      native_runtime_status='pending', native_process_qualified=False,
                      native_exit_code=None, native_reaped=None, ap_worker_jobs_pass=False)
        if 'AP STARTUP LIMITED\n' in text:
            require('AP STARTUP LIMITED\nno firmware processor inventory\n' in text and
                    'SOURCE BSP only (firmware topology unavailable)\n' in text,
                    'Unexpected EFI AP limitation')
            for marker in ('CPU DETECTED 00000001', 'CPU ONLINE 00000001', 'AP INITIALIZED 00000000',
                           'AP PARKED 00000000', 'AP FAILED 00000000', 'WORKER CONFIGURED 00000000',
                           'WORKER READY 00000000', 'WORKER FAILED 00000000', 'WORKER POOL LIMITED'):
                require(marker in text, 'Observed limited AP/worker state differs: ' + marker)
            result.update(ap_status='limited', ap_reason='no firmware processor inventory')
        else:
            require('AP STARTUP PASS' in text and 'WORKER POOL READY' in text and result['cpu_detected'] == 4,
                    'Unexpected AP/worker baseline')
            guest.verify_workers(4, periodic=True)
            result.update(ap_status='pass', ap_reason=None, ap_worker_jobs_pass=True)
        if 'NATIVE RUNTIME LIMITED\n' in text:
            require('NATIVE RUNTIME PASS\n' not in text, 'Conflicting native runtime status')
            result['native_runtime_status'] = 'limited'
            result['native_limitation_reason'] = 'not diagnosed by this logging qualification'
        else:
            wait_for('NATIVE RUNTIME PASS')
            wait_for('BROWSER PLATFORM PROBE PASS ABI1')
            require('BROWSER PROBE EXIT 00000000' in guest.text() and 'NATIVE REAPED 00000003' in guest.text(),
                    'Native process pass requires its real successful exit and reaping')
            result.update(native_runtime_status='pass', native_process_qualified=True,
                          native_exit_code=0, native_reaped=3)
        guest.capture('actual-desktop')
        return result

    def fresh_marker(marker, action):
        count = guest.text().count(marker)
        action()
        end = time.monotonic() + 10
        while guest.text().count(marker) <= count and time.monotonic() < end:
            require('PANIC' not in guest.text() and 'RUNTIME FAIL' not in guest.text(), 'Guest failed during input')
            time.sleep(.03)
        require(guest.text().count(marker) == count + 1, 'Actual input must produce exactly one ' + marker)

    def viewer():
        guest.key('2')
        wait_for('UI HARDWARE')
        hardware = guest.capture('monitor-hardware')
        fresh_marker('UI BOOT LOG OPEN', lambda: guest.key('b'))
        first = guest.capture('boot-log-viewer-first-page')
        body = (129, 204, 671, 454)
        footer = (602, 460, 650, 482)
        require(hardware.crop(body).tobytes() != first.crop(body).tobytes(),
                'Log viewer must replace actual monitor pixels with logged content')
        for _ in range(8):
            guest.key('down')
        scrolled = guest.capture('boot-log-viewer-scrolled')
        require(first.crop(body).tobytes() != scrolled.crop(body).tobytes(),
                'Real Down keys must change visible retained log lines')
        require(first.crop(footer).tobytes() != scrolled.crop(footer).tobytes(),
                'Real scrolling must change the visible first-line index')
        for _ in range(8):
            guest.key('up')
        restored = guest.capture('boot-log-viewer-restored-first-page')
        require(first.crop(body).tobytes() == restored.crop(body).tobytes() and
                first.crop(footer).tobytes() == restored.crop(footer).tobytes(),
                'Real Up keys must restore the same retained first page and line index')
        fresh_marker('UI BOOT LOG CLOSE', lambda: guest.key('b'))
        closed = guest.capture('monitor-after-log-viewer')
        require(closed.crop(body).tobytes() != restored.crop(body).tobytes(),
                'Actual B key must return to monitor hardware content')
        return dict(actual_keyboard_viewer_open_close_pass=True, actual_keyboard_scroll_pass=True,
                    full_body_restore_pass=True, body_pixel_rect=list(body), line_index_pixel_rect=list(footer),
                    screenshot_sha256={path.name: digest(path) for path in guest.out.glob('*.png')})

    record()
    input_proofs = []
    normal_input_hashes = None
    try:
        for name, build, entry, failure in builds:
            manifest_path = build / 'manifest.json'
            manifest = json.loads(manifest_path.read_text())
            require(manifest.get('build_pass') is True, 'A successful private builder manifest is required: ' + name)
            require(manifest.get('diagnostic_failure') == failure,
                    'Private failure selector differs from requested case: ' + name)
            iso = pathlib.Path(manifest['outputs']['iso']['path']).resolve()
            require(digest(iso) == manifest['outputs']['iso']['sha256'], 'Actual ISO differs: ' + name)
            target = runtime / (name + '-iso-files')
            run(name + '-iso-extract', ['xorriso', '-indev', iso, '-osirrox', 'on', '-extract', '/', target])
            hashes = file_hashes(target)
            save(out / (name + '-iso-files.json'), hashes)
            config = (target / 'boot/grub/grub.cfg').read_text()
            menus = validate_live_entries(config, default_id=entry, failure=failure)
            omitted = manifest.get('diagnostic_omitted_paths', [])
            expected_omissions = {'missing-kernel': ['/boot/GTOS.bin'],
                                  'missing-module': ['/boot/native-fault.elf']}.get(failure, [])
            require(omitted == expected_omissions,
                    'Only the intended private kernel/module file may be omitted')
            input_hashes = {}
            for item in manifest['input_files']:
                relative = item['iso_path'].lstrip('/')
                input_hashes[relative] = item['sha256']
                present = item.get('present_in_media', True)
                require(present == (item['iso_path'] not in omitted), 'Manifest media presence is inconsistent')
                if present:
                    require(hashes.get(relative) == item['sha256'] and
                            (target / relative).stat().st_size == item['bytes'],
                            'Actual media input differs: ' + relative)
                else:
                    require(not (target / relative).exists(), 'Diagnostic omission was not actual: ' + relative)
            require(input_hashes.get('boot/GTOS.bin') == expected_kernel, 'Every variant must bind the same real kernel')
            if normal_input_hashes is None:
                normal_input_hashes = input_hashes
            else:
                require(input_hashes == normal_input_hashes,
                        'Log variants may select/omit files, never substitute kernel or published modules')
            for module in MODULES:
                require(module.lstrip('/') in input_hashes, 'Original module proof missing: ' + module)
            loader, fat = target / 'EFI/BOOT/BOOTX64.EFI', target / 'efi.img'
            require(digest(loader) == manifest['outputs']['efi_loader']['sha256'] and
                    digest(fat) == manifest['outputs']['efi_fat_image']['sha256'], 'EFI/FAT bytes differ')
            pe = loader.read_bytes()
            require(len(pe) >= 64 and pe[:2] == b'MZ', 'Actual PE64 EFI loader required')
            offset = struct.unpack_from('<I', pe, 0x3c)[0]
            require(offset <= len(pe) - 26 and pe[offset:offset + 4] == b'PE\0\0' and
                    struct.unpack_from('<H', pe, offset + 4)[0] == 0x8664 and
                    struct.unpack_from('<H', pe, offset + 24)[0] == 0x20b, 'Actual loader must be AMD64 PE32+')
            extracted = runtime / (name + '-fat-loader.EFI')
            run(name + '-fat-loader-extract', ['mcopy', '-i', fat, '::/EFI/BOOT/BOOTX64.EFI', extracted])
            require(extracted.read_bytes() == pe, 'Actual boot FAT and ISO EFI loader bytes differ')
            case = runtime / name
            case.mkdir()
            variables = case / 'OVMF_VARS.private.fd'
            shutil.copyfile(vars_template, variables)
            require(digest(variables) == firmware_hashes[str(vars_template)], 'Private VARS copy differs')
            disk = case / 'writable-sentinel.img'
            data = bytearray(bytes((i * 73 + 19) & 255 for i in range(256)) * 4096)
            data[510:512] = b'\x55\xaa'
            data[512:520] = b'EFI PART'
            with disk.open('xb') as stream:
                stream.write(data)
            require(os.access(str(disk), os.W_OK), 'Sentinel must actually be writable')
            disk_before = digest(disk)
            item = dict(name=name, selected_boot_entry=entry, diagnostic_failure=failure,
                        iso_sha256=digest(iso), manifest_sha256=digest(manifest_path),
                        extracted_files_sha256=hashes, actual_boot_entries=menus,
                        exact_kernel_and_modules_verified=True, actual_pe64_and_fat_verified=True,
                        omitted_paths=omitted, real_framebuffer_capture=False,
                        private_writable_sentinel=True, sentinel_sha256_before=disk_before,
                        automated_case_pass=False)
            state['cases'].append(item)
            input_proofs.append((iso, digest(iso), manifest_path, digest(manifest_path)))
            state.update(stage='guest-running', current_case=name)
            record()
            guest = LogGuest(case, iso, disk, variables)
            if failure in ('missing-kernel', 'missing-module'):
                end = time.monotonic() + args.loader_settle_seconds
                while time.monotonic() < end:
                    require(guest.p.poll() is None, 'EFI loader stopped before its error console could be captured')
                    require('GTOS 0.3 PROTECTED DESKTOP BOOT' not in guest.text() and
                            not any(phase in guest.text() for phase in PHASES),
                            'Stopped loader error case must never enter the GTOS kernel')
                    time.sleep(.1)
                loader_code = 'L02' if failure == 'missing-kernel' else 'L03'
                capture_name = 'loader-' + loader_code + '-stopped-console'
                screen = guest.capture(capture_name, kernel_frame=False)
                require(screen.width >= 640 and screen.height >= 400, 'Real EFI console screenshot is required')
                require(len(screen.getcolors(screen.width * screen.height)) > 1,
                        'Loader screenshot must contain actual visible console content')
                stopped_log = guest.text()
                for key in ('ret', 'spc', 'a', 'ret', 'spc', 'a'):
                    guest.key(key)
                after_input = guest.capture(capture_name + '-after-real-keys', kernel_frame=False)
                require(after_input.size == screen.size, 'Stopped loader console dimensions changed after input')
                require(guest.text() == stopped_log and
                        'GTOS 0.3 PROTECTED DESKTOP BOOT' not in guest.text() and
                        not any(phase in guest.text() for phase in PHASES),
                        'Loader error input must never produce a kernel handoff')
                difference = ImageChops.difference(screen, after_input)
                difference_bounds = difference.getbbox()
                unchanged_screen = difference_bounds is None
                # A changed image is not automatically classified as a cursor
                # blink. Preserve both actual frames and its precise difference
                # for explicit visual review, without claiming stable text.
                changed_pixels = sum(pixel != (0, 0, 0) for pixel in difference.getdata())
                item.update(loader_no_kernel_handoff_pass=True, real_framebuffer_capture=True,
                            actual_framebuffer_size=list(screen.size),
                            expected_visible_loader_error=('[GTOS LOADER L02] kernel load failed: /boot/GTOS.bin'
                                                           if failure == 'missing-kernel' else
                                                           '[GTOS LOADER L03] boot module load failed: /boot/native-fault.elf'),
                            loader_visual_review_pending=True, visible_error_automatically_verified=False,
                            loader_console_screenshot=str(case / (capture_name + '.png')),
                            actual_loader_keys=['ret', 'spc', 'a', 'ret', 'spc', 'a'],
                            loader_input_no_kernel_handoff_pass=True,
                            loader_input_whole_frame_bytes_equal=unchanged_screen,
                            loader_input_screen_stability_review_pending=not unchanged_screen,
                            loader_input_difference_bounds=list(difference_bounds) if difference_bounds else None,
                            loader_input_changed_pixels=changed_pixels,
                            loader_console_after_keys_screenshot=str(case / (capture_name + '-after-real-keys.png')))
            elif failure == 'kernel-panic':
                wait_for(PANIC, expected_panic=True)
                text_before = guest.text()
                require(text_before.count('PANIC ') == 1 and text_before.count(PANIC) == 1,
                        'Safe E99 case must report one nonrecursive panic')
                require(PHASES[0] in text_before and PHASES[1] in text_before,
                        'Safe failure must enter kernel handoff and memory phase B02')
                require(not any(phase in text_before for phase in PHASES[2:]) and
                        'DESKTOP READY' not in text_before and 'SCHEDULER RUNTIME PASS' not in text_before,
                        'Safe B02 failure must stop before later boot/desktop execution')
                first = guest.capture('kernel-E99-visible-error')
                time.sleep(2)
                second = guest.capture('kernel-E99-stable-error')
                require(first.tobytes() == second.tobytes() and guest.text() == text_before,
                        'Halted E99 framebuffer and kernel log must remain stable')
                item.update(safe_kernel_failure_E99_pass=True, panic_phase='B02',
                            panic_occurrences=1, stopped_before_desktop=True,
                            stable_actual_framebuffer_pass=True, stable_kernel_log_pass=True,
                            real_framebuffer_capture=True)
            else:
                if name in ('verbose', 'serial'):
                    wait_for(PAUSED)
                    phase_order(guest.text())
                    startup = guest.capture('verbose-phased-startup-paused')
                    require(len(startup.getcolors(startup.width * startup.height)) > 1,
                            'Verbose startup must produce visible real framebuffer content')
                    require('BOOT LOG RESUME' not in guest.text(), 'Diagnostic pause must wait for real keyboard input')
                    require('DESKTOP READY' not in guest.text(),
                            'Diagnostic pause must withhold desktop handoff until the real resume key')
                    if name == 'verbose':
                        # Send real PS/2 key-down repeats while the resume key
                        # is still held. They must remain consumed through the
                        # matching key-up; a fresh press must work afterward.
                        events = [guest.key_event('i', True)]
                        wait_for('BOOT LOG RESUME')
                        for _ in range(3):
                            events.append(guest.key_event('i', True))
                            time.sleep(.05)
                        wait_for('DESKTOP READY')
                        time.sleep(.2)
                        require('APP INSTALL OK' not in guest.text() and 'UI APPS' not in guest.text(),
                                'Held/repeated resume I key must not leak into desktop actions')
                        events.append(guest.key_event('i', False))
                        time.sleep(.15)
                        save(case / 'resume-key-actual-events.json', events)
                        fresh_marker('APP INSTALL OK', lambda: guest.key('i'))
                        item.update(resume_key_repeat_consumption_pass=True,
                                    actual_repeated_keydowns=3, fresh_key_after_release_pass=True)
                    else:
                        guest.key('spc')
                        wait_for('BOOT LOG RESUME')
                    item.update(actual_verbose_startup_pause_pass=True,
                                actual_keyboard_resume_pass=True, real_framebuffer_capture=True)
                else:
                    require(PAUSED not in guest.text(), 'Default normal boot must not request verbose pause')
                item['actual_runtime_observation'] = desktop_baseline()
                if name == 'normal':
                    require(PAUSED not in guest.text() and SERIAL not in guest.text(),
                            'Default normal boot must leave verbose and serial opt-in disabled')
                    item['actual_log_viewer'] = viewer()
                uart = guest.uart()
                if name == 'serial':
                    require(SERIAL in uart, 'Explicit serial mode must send the actual COM1 setup marker')
                    phase_order(uart)
                    require('BOOT LOG RESUME' in uart and 'DESKTOP READY' in uart,
                            'Actual COM1 must carry later kernel output after resume')
                    item.update(actual_COM1_uart_pass=True, serial_capture_bytes=guest.serial.stat().st_size,
                                serial_capture_sha256=digest(guest.serial))
                else:
                    require(SERIAL not in uart and not any(phase in uart for phase in PHASES),
                            'Serial-off mode must not emit kernel boot phases to COM1')
                    item.update(serial_off_kernel_phase_absence_pass=True,
                                firmware_or_loader_uart_bytes=len(uart.encode('utf-8')))
                require('PANIC' not in guest.text() and 'RUNTIME FAIL' not in guest.text(), 'Normal/diagnostic desktop failed')
                item.update(real_kernel_phase_order_pass=True, real_framebuffer_capture=True,
                            scheduler_and_desktop_pass=True)
            item['writable_sentinel_block_stats'] = guest.block_stats()
            item['e9_debug_capture_sha256'] = digest(guest.log)
            item['e9_debug_capture_bytes'] = guest.log.stat().st_size
            if guest.serial.is_file():
                item['COM1_capture_sha256'] = digest(guest.serial)
                item['COM1_capture_bytes'] = guest.serial.stat().st_size
            guest.close()
            guest = None
            require(digest(disk) == disk_before, 'Any boot/log/input/error operation changed physical sentinel bytes')
            item.update(sentinel_sha256_after=digest(disk), all_physical_sentinel_bytes_unchanged=True,
                        private_vars_sha256_after=digest(variables), automated_case_pass=True,
                        screenshot_sha256={path.name: digest(path) for path in case.glob('*.png')})
            record()
        require(before == sources(), 'Repository sources changed during log qualification')
        require(all(digest(iso) == iso_hash and digest(manifest) == manifest_hash
                    for iso, iso_hash, manifest, manifest_hash in input_proofs), 'Input build artifacts changed')
        require(all(digest(pathlib.Path(path)) == value for path, value in firmware_hashes.items()),
                'Official OVMF code or VARS template changed')
        # A human reviews the real loader console capture; an absent kernel log
        # and the intended missing file cannot alone prove visible L02/L03 text.
        state.update(stage='automated-checks-passed-awaiting-loader-screen-review', automated_checks_pass=True,
                     completed_cases=len(builds), tracked_and_new_sources_unchanged=True,
                     input_build_artifacts_unchanged=True, official_firmware_unchanged=True,
                     required_visual_review='Inspect each loader-L02/L03-stopped-console.png and confirm its exact error text',
                     guest_pass=False, loader_visual_review_pending=True)
    except BaseException as error:
        state.update(stage='failed', error=repr(error))
        if guest:
            try:
                guest.capture('failure-framebuffer', kernel_frame=False)
                state['failure_screenshot'] = str(guest.out / 'failure-framebuffer.png')
            except BaseException as capture_error:
                state['failure_screenshot_error'] = repr(capture_error)
        raise
    finally:
        if guest:
            guest.close()
        after = sources()
        save(out / 'sources-after.json', after)
        state['tracked_and_new_sources_unchanged'] = before == after
        if runtime != out / 'guest':
            shutil.copytree(runtime, out / 'guest')
        record()
    print(json.dumps(state, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
