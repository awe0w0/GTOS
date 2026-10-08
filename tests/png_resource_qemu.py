#!/usr/bin/env python3
"""Qualify the bounded PNG resource consumer with the ordinary GTOS i386 kernel.

Run with /usr/bin/python3 (Pillow required) after the resource builder and normal
make GTOS.iso succeed. The dedicated ISO changes only boot module 3, the existing
browser-probe.elf slot. All guest disks and evidence directories must be new.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
START = 'GTOS PNG RESOURCE START V1'
PASS = 'GTOS PNG RESOURCE PASS READ DECODE PIXELS V1'
MODULES = ['/boot/catch.gtapp', '/boot/native-fault.elf',
           '/boot/native-peer.elf', '/boot/browser-probe.elf']


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path,
                        help='Successful PNG resource builder output')
    parser.add_argument('output', type=pathlib.Path, help='New evidence directory')
    parser.add_argument('--kernel-stage', type=pathlib.Path,
                        help='Ordinary make GTOS.iso stage (default: obj/iso)')
    parser.add_argument('--runtime-root', type=pathlib.Path,
                        help='Optional existing temporary directory for QEMU files')
    parser.add_argument('--kernel-sha256', required=True,
                        help='Exact newly built ordinary GTOS.bin SHA-256')
    parser.add_argument('--source-base', help='Optional expected source_base commit')
    parser.add_argument('--source-sha256', type=pathlib.Path,
                        help='Optional JSON of expected precommit repository source hashes')
    parser.add_argument('--host-cc', default='gcc')
    parser.add_argument('--host-cxx', default='g++')
    args = parser.parse_args()
    from PIL import Image

    build = args.build.resolve()
    out = args.output.resolve()
    kernel_stage = (args.kernel_stage or ROOT / 'obj/iso').resolve()
    expected_kernel = args.kernel_sha256.lower()
    require(re.fullmatch(r'[0-9a-f]{64}', expected_kernel), 'Invalid kernel SHA-256')
    manifest = json.loads((build / 'manifest.json').read_text())
    require(manifest.get('native_build_pass') is True, 'Native build proof is required')
    require(manifest.get('boot_file_admission_pass') is True,
            'Boot file admission proof is required')
    revision = git('rev-parse', 'HEAD').decode().strip()
    require(revision == manifest['source_base'], 'Manifest source_base differs from HEAD')
    if args.source_base:
        expected_base = git('rev-parse', '--verify', args.source_base + '^{commit}').decode().strip()
        require(revision == expected_base, 'Expected source_base differs from HEAD')
    expected_sources = dict(manifest['source_sha256'])
    require(expected_sources, 'Manifest source SHA-256 proof is required')
    if args.source_sha256:
        extra_sources = json.loads(args.source_sha256.read_text())
        for name, value in extra_sources.items():
            require(name not in expected_sources or expected_sources[name] == value,
                    'Conflicting source SHA-256: ' + name)
            expected_sources[name] = value
    for name in ('include/process/resource_abi.h', 'include/process/resources.h',
                 'src/process/resources.cpp', 'src/process/resources_png.inc',
                 'src/process/native_runtime.cpp','src/process/native_realtime.cpp', 'apps/png_image_codec/png_decode.c',
                 'apps/png_image_codec/png_pixel.cc'):
        require(name in expected_sources, 'Required resource/codec source proof missing: ' + name)
    require(any(name.startswith('apps/png_resource_probe/') for name in expected_sources),
            'Resource consumer source proof is required')
    for name, value in expected_sources.items():
        source = pathlib.PurePosixPath(name)
        require(not source.is_absolute() and '..' not in source.parts and '\\' not in name,
                'Source proof must use repository-relative paths: ' + name)
        require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value),
                'Invalid source SHA-256: ' + name)
        require(digest(ROOT / name) == value, 'Source SHA-256 differs: ' + name)
    elf = build / 'resource-probe.stripped.elf'
    require(digest(elf) == manifest['stripped_sha256'], 'Consumer ELF differs from manifest')
    require(elf.stat().st_size <= 65536, 'Existing boot fixture limits external ELF to 64 KiB')
    require((ROOT / 'GTOS.iso').is_file(), 'Build the ordinary make GTOS.iso first')
    require(digest(ROOT / 'GTOS.bin') == expected_kernel, 'Ordinary GTOS.bin differs')
    require(digest(kernel_stage / 'boot/GTOS.bin') == expected_kernel, 'Staged kernel differs')
    grub = (kernel_stage / 'boot/grub/grub.cfg').read_text()
    require(re.findall(r'^\s*module\s+(\S+)\s*$', grub, re.MULTILINE) == MODULES,
            'Ordinary GRUB stage must have exactly the four existing module slots')
    require(re.findall(r'^\s*multiboot\s+([^\n]+)', grub, re.MULTILINE) == ['/boot/GTOS.bin'],
            'Use the ordinary kernel command line')
    for staged, original in (('native-fault.elf', 'obj/native/fault.elf'),
                             ('native-peer.elf', 'obj/native/peer.elf'),
                             ('browser-probe.elf', 'obj/browser-probe/browser-probe.elf')):
        require(digest(kernel_stage / 'boot' / staged) == digest(ROOT / original),
                'Ordinary boot module differs: ' + staged)
    require(not out.exists(), 'Choose a fresh evidence directory')
    runtime = out / 'guest'
    if args.runtime_root:
        require(args.runtime_root.is_dir(), '--runtime-root must already exist')
        runtime = args.runtime_root.resolve() / ('png-resource-' + out.name)
        require(not runtime.exists(), 'Choose a fresh runtime directory')
    out.mkdir(parents=True)
    runtime.mkdir()

    # A dirty precommit checkout is allowed. Snapshot tracked files, new source
    # files (including the new kernel module), and every expected source path.
    excluded_roots = (out, runtime, build)

    def source_snapshot():
        tracked = {name.decode() for name in git('ls-files', '-z').split(b'\0') if name}
        added = {name.decode() for name in
                 git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0') if name}
        names = tracked | added | set(expected_sources)
        result = {}
        for name in sorted(names):
            path = ROOT / name
            if any(path.resolve() == directory or directory in path.resolve().parents
                   for directory in excluded_roots):
                continue
            result[name] = digest(path) if path.is_file() else None
        return result

    before = source_snapshot()
    stage_before = tree_hashes(kernel_stage)
    (out / 'sources-before.json').write_text(json.dumps(before, indent=2) + '\n')
    state = dict(scope='Bounded immutable native PNG resource read, decode and pixel proof',
                 source_base=revision, expected_source_sha256=expected_sources,
                 kernel_binary_sha256=expected_kernel,
                 ordinary_iso_sha256=digest(ROOT / 'GTOS.iso'),
                 elf_sha256=manifest['stripped_sha256'],
                 manifest_sha256=digest(build / 'manifest.json'),
                 native_build_pass=True, boot_file_admission_pass=True,
                 host_asan_ubsan_pass=manifest.get('host_asan_ubsan_pass'),
                 guest_pass=False, browser_guest_pass=False,
                 module_slot=3, ordinary_kernel_command_line=True, cases=[])
    guest = None

    def record():
        state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (out / 'results.json').write_text(json.dumps(state, indent=2) + '\n')

    def run(name, command):
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([str(part) for part in command], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=240)

    def screenshot(case, name):
        path = case / (name + '.ppm')
        guest.call('screendump', {'filename': str(path)})
        image = Image.open(path).convert('RGB')
        require(image.size == (800, 600), 'Actual framebuffer must be 800x600')
        image.save(case / (name + '.png'))
        return image

    def reload_once(mouse=False):
        count = guest.text().count('APP DISK RELOAD ')
        if mouse:
            # Initial cursor (770,16), existing Reload control (591,144).
            guest.mouse(-179, 128, True)
        else:
            guest.key('r')
        end = time.monotonic() + 10
        while guest.text().count('APP DISK RELOAD ') <= count and time.monotonic() < end:
            time.sleep(.03)
        text = guest.text()
        require(text.count('APP DISK RELOAD ') == count + 1,
                'Actual input must initiate exactly one disk reload')
        require(text.rsplit('APP DISK RELOAD ', 1)[1].startswith('OK\n'),
                'Actual disk reload must succeed')

    record()
    try:
        validator_app = ROOT / 'apps/wuffs_gif_probe'
        host_io = out / 'host-io.o'
        validator = out / 'actual-elf32-validator'
        run('host-io-build', [args.host_cc, '-std=c11', '-O1', '-g',
                             '-fsanitize=address,undefined', '-fno-pie', '-c',
                             validator_app / 'validation_host.c', '-o', host_io])
        run('host-validator-build', [args.host_cxx, '-std=c++11', '-O1', '-g',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
            '-I' + str(ROOT / 'include'), ROOT / 'src/process/elf32.cpp',
            validator_app / 'validation_host.cpp', host_io, '-o', validator])
        run('actual-elf32-validation', [validator, elf])
        require('REAL GTOS ELF32 VALIDATOR PASS' in
                (out / 'actual-elf32-validation.log').read_text(),
                'Actual GTOS ELF32 validator must admit the consumer')
        state['actual_elf32_validation_pass'] = True
        normal_boot = runtime / 'ordinary-iso-boot'
        run('ordinary-iso-extract', ['xorriso', '-indev', ROOT / 'GTOS.iso',
            '-osirrox', 'on', '-extract', '/boot', normal_boot])
        for name, value in stage_before.items():
            if name.startswith('boot/'):
                require(digest(normal_boot / name[len('boot/'):]) == value,
                        'Ordinary ISO differs from the normal stage: ' + name)
        state['ordinary_iso_boot_files_verified'] = True
        stage = out / 'iso-stage'
        shutil.copytree(kernel_stage, stage)
        shutil.copyfile(elf, stage / 'boot/browser-probe.elf')
        expected_stage = dict(stage_before)
        expected_stage['boot/browser-probe.elf'] = manifest['stripped_sha256']
        require(tree_hashes(stage) == expected_stage,
                'Dedicated stage may change only the existing browser-probe ELF')
        iso = out / 'GTOS-png-resource.iso'
        run('iso-build', ['grub-mkrescue', '--output=' + str(iso), stage])
        extracted = runtime / 'extracted-boot'
        run('iso-extract', ['xorriso', '-indev', iso, '-osirrox', 'on',
                           '-extract', '/boot', extracted])
        for name, value in expected_stage.items():
            if name.startswith('boot/'):
                require(digest(extracted / name[len('boot/'):]) == value,
                        'Actual ISO boot file differs: ' + name)
        require(digest(extracted / 'GTOS.bin') == expected_kernel, 'Actual ISO kernel differs')
        require((extracted / 'browser-probe.elf').read_bytes() == elf.read_bytes(),
                'Actual ISO consumer bytes differ')
        state.update(exact_iso_elf_hash_verified=True, exact_iso_kernel_hash_verified=True,
                     exact_iso_boot_files_verified=True, only_module3_replaced=True,
                     dedicated_iso_sha256=digest(iso))
        sys.path.insert(0, str(ROOT / 'tests'))
        import qemu_smoke
        from desktop_qemu import paddle
        from settings_tool_test import record as settings_record, FIRST_SETTINGS_SECTOR
        sys.path.insert(0, str(ROOT / 'tools'))
        import disk as disktool
        qemu_smoke.BOOT_ISO = iso
        for memory, cpus in ((64, 4), (32, 1), (96, 4)):
            case = runtime / ('%dM-%dcpu' % (memory, cpus))
            case.mkdir()
            disk = case / 'apps.img'
            run('%dM-%dcpu-disk' % (memory, cpus),
                [sys.executable, ROOT / 'tools/disk.py', 'create', disk, '--size-mib', '8'])
            image = disktool.Image(disk, writable=True)
            try:
                image.install((ROOT / 'apps/catch.gtapp').read_bytes())
                if memory == 32:
                    image.stream.seek(FIRST_SETTINGS_SECTOR * 512)
                    image.stream.write(settings_record(1, 1, 0) * 2)
                    image.sync()
            finally:
                image.close()
            disk_before = digest(disk)
            state.update(stage='guest-running', current_case=dict(memory_mib=memory, vcpus=cpus))
            record()
            begin = time.monotonic()
            guest = qemu_smoke.Guest(case, disk, memory, cpus, wait_ready=False)
            guest.wait(START, 30)
            guest.wait(PASS, 180)
            guest.wait('BROWSER PROBE EXIT 00000000', 20)
            guest.wait('NATIVE REAPED 00000003', 20)
            guest.wait('NATIVE RUNTIME PASS', 30)
            guest.wait('DESKTOP READY', 30)
            guest.wait('SCHEDULER RUNTIME PASS', 10)
            locale = 'zh-CN' if memory == 32 else 'en'
            guest.wait('UI LOCALE ' + locale, 10)
            for marker in ('PHYSICAL SELFTEST PASS', 'PAGING PG WP NULL RO PASS',
                           'HEAP SELFTEST PASS', 'SYSCALL ABI PASS',
                           'DESKTOP MODE FRAMEBUFFER', 'CPU DETECTED %08X' % cpus):
                require(marker in guest.text(), 'Existing boot baseline missing: ' + marker)
            require('GTOS PNG RESOURCE FAIL' not in guest.text(), 'Resource consumer failed')
            guest.verify_workers(cpus, periodic=True)
            screenshot(case, 'native-gtos-desktop')
            guest.key('3')
            guest.wait('UI APPS', 10)
            reload_once()
            reload_once(mouse=True)
            screenshot(case, 'native-gtos-applications')
            guest.key('ret')
            guest.wait('APP LAUNCH OK', 10)
            before_x = paddle(screenshot(case, 'native-gtos-game-before'))
            require(before_x is not None, 'Actual Catch paddle must be visible')
            guest.key('right', 350)
            after_x = paddle(screenshot(case, 'native-gtos-game'))
            require(after_x is not None and after_x > before_x + 10,
                    'Actual right-arrow input must move the visible Catch paddle')
            guest.verify_workers(cpus, periodic=True)
            guest.key('esc')
            guest.wait('APP CLOSE OK', 10)
            guest.close()
            guest = None
            disk_after = digest(disk)
            require(disk_after == disk_before, 'Guest changed app-disk bytes')
            state['cases'].append(dict(memory_mib=memory, vcpus=cpus, guest_pass=True,
                native_exit_code=0, native_reaped=3, resource_read_decode_pixels_pass=True,
                existing_boot_baseline_pass=True, desktop_game_input_pass=True,
                keyboard_and_mouse_reload_pass=True, locale='zh-CN' if memory == 32 else 'en-US',
                persisted_locale_verified=True, disk_bytes_unchanged=True,
                disk_sha256_before=disk_before, disk_sha256_after=disk_after,
                game_paddle_before_x=before_x, game_paddle_after_x=after_x,
                ap_worker_jobs_pass=True, seconds=time.monotonic() - begin))
            record()
        require(before == source_snapshot(), 'Tracked or new repository source changed during qualification')
        require(tree_hashes(kernel_stage) == stage_before, 'Ordinary boot stage changed during qualification')
        require(digest(ROOT / 'GTOS.bin') == expected_kernel, 'Ordinary kernel changed during qualification')
        require(digest(ROOT / 'GTOS.iso') == state['ordinary_iso_sha256'],
                'Ordinary ISO changed during qualification')
        require(digest(elf) == manifest['stripped_sha256'], 'Consumer changed during qualification')
        state.update(stage='passed', guest_pass=True, completed_cases=3,
                     tracked_and_new_source_hashes_unchanged=True, ordinary_stage_unchanged=True)
    except Exception as error:
        state.update(stage='failed', error=repr(error))
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
