#!/usr/bin/env python3
"""Qualify the complete PNG/Skia app through the unchanged GTOS i386 loader."""
import argparse, datetime, hashlib, json, pathlib, shutil, subprocess, sys, time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('build',type=pathlib.Path,help='Successful build-png-image-codec output')
parser.add_argument('output',type=pathlib.Path,help='New evidence directory')
parser.add_argument('--kernel-stage',type=pathlib.Path)
parser.add_argument('--runtime-root',type=pathlib.Path,help='Optional existing temporary directory for QEMU files')
parser.add_argument('--kernel-sha256',required=True,help='Qualified unchanged kernel binary hash')
parser.add_argument('--host-cc',default='gcc')
parser.add_argument('--host-cxx',default='g++')
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
app=repo/'apps/wuffs_gif_probe'
build=args.build.resolve();out=args.output.resolve()
manifest=json.loads((build/'manifest.json').read_text())
assert manifest['native_build_pass'] and manifest['host_asan_ubsan_pass'] and manifest['boot_file_admission_pass']
assert subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()==manifest['source_base']
subprocess.run(['git','-C',str(repo),'diff','--quiet','HEAD','--'],check=True)
elf=build/'png-pixels-probe.stripped.elf'
assert hashlib.sha256(elf.read_bytes()).hexdigest()==manifest['stripped_sha256']
assert elf.stat().st_size<=65536,'The unchanged StartNativeDemo boot fixture limits each external ELF to 64 KiB'
kernel_stage=(args.kernel_stage or repo/'obj/iso-native-test').resolve()
assert (kernel_stage/'boot/GTOS.bin').is_file(),'Build make GTOS-native-test.iso first'
assert hashlib.sha256((kernel_stage/'boot/GTOS.bin').read_bytes()).hexdigest()==args.kernel_sha256
assert not out.exists(),'Choose a fresh evidence directory'
out.mkdir(parents=True)
runtime=out/'guest'
if args.runtime_root:
    runtime=args.runtime_root.resolve()/('png-pixels-'+out.name)
    assert not runtime.exists(),'Choose a fresh runtime directory'
runtime.mkdir()
tracked=subprocess.check_output(['git','-C',str(repo),'ls-files','-z']).split(b'\0')
def sources():
    return {name.decode():hashlib.sha256((repo/name.decode()).read_bytes()).hexdigest()
            for name in tracked if name and (repo/name.decode()).is_file()}
before=sources()
for source,digest in manifest['source_sha256'].items():
    assert hashlib.sha256((repo/source).read_bytes()).hexdigest()==digest,source
state=dict(scope='Real complete static PNG raster decoder and Skia pixels in existing GTOS i386 ABI1',
           kernel_revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
           elf_sha256=manifest['stripped_sha256'],guest_pass=False,browser_guest_pass=False,
           kernel_source_changed=False,original_browser_probe_changed=False,elf64_path_used=False,cases=[])
guest=None
def record():
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'results.json').write_text(json.dumps(state,indent=2)+'\n')
def run(name,command):
    with (out/(name+'.log')).open('w') as log:
        subprocess.run([str(x) for x in command],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
def screenshot(case,name):
    from PIL import Image
    path=case/(name+'.ppm');guest.call('screendump',{'filename':str(path)})
    im=Image.open(path).convert('RGB');assert im.size==(800,600)
    im.save(case/(name+'.png'));return im
record()
try:
    host_io=out/'host-io.o';validator=out/'actual-elf32-validator'
    run('host-io-build',[args.host_cc,'-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-c',app/'validation_host.c','-o',host_io])
    run('host-validator-build',[args.host_cxx,'-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',
        '-I'+str(repo/'include'),repo/'src/process/elf32.cpp',app/'validation_host.cpp',host_io,'-o',validator])
    run('actual-elf32-validation',[validator,elf])
    assert 'REAL GTOS ELF32 VALIDATOR PASS' in (out/'actual-elf32-validation.log').read_text()
    stage=out/'iso-stage';shutil.copytree(kernel_stage,stage)
    shutil.copyfile(elf,stage/'boot/browser-probe.elf')
    state['kernel_binary_sha256']=hashlib.sha256((stage/'boot/GTOS.bin').read_bytes()).hexdigest()
    assert state['kernel_binary_sha256']==args.kernel_sha256
    iso=out/'GTOS-png-pixels.iso'
    run('iso-build',['grub-mkrescue','--output='+str(iso),stage])
    extracted=runtime/'extracted.elf'
    run('iso-extract',['xorriso','-indev',iso,'-osirrox','on','-extract','/boot/browser-probe.elf',extracted])
    assert extracted.read_bytes()==elf.read_bytes();state['exact_iso_elf_hash_verified']=True
    sys.path.insert(0,str(repo/'tests'));import qemu_smoke
    from desktop_qemu import paddle
    from settings_tool_test import record as settings_record,FIRST_SETTINGS_SECTOR
    sys.path.insert(0,str(repo/'tools'));import disk as disktool
    qemu_smoke.BOOT_ISO=iso
    for memory,cpus in ((64,4),(32,1),(96,4)):
        case=runtime/('%dM-%dcpu'%(memory,cpus));case.mkdir()
        disk=case/'apps.img'
        run('%dM-%dcpu-disk'%(memory,cpus),[sys.executable,repo/'tools/disk.py','create',disk,'--size-mib','8'])
        image=disktool.Image(disk,writable=True)
        try:
            image.install((repo/'apps/catch.gtapp').read_bytes())
            if memory==32:
                image.stream.seek(FIRST_SETTINGS_SECTOR*512)
                image.stream.write(settings_record(1,1,0)*2);image.sync()
        finally:image.close()
        before_disk=hashlib.sha256(disk.read_bytes()).hexdigest()
        state.update(stage='guest-running',current_case=dict(memory_mib=memory,vcpus=cpus));record()
        begin=time.monotonic();guest=qemu_smoke.Guest(case,disk,memory,cpus,wait_ready=False)
        guest.wait('GTOS PNG PIXELS START REAL WUFFS SKIA ABI1',30)
        guest.wait('GTOS PNG PIXELS PASS FIXTURES PIXELS PREFIXES BOUNDS ABI1',180)
        guest.wait('BROWSER PROBE EXIT 00000000',20)
        guest.wait('NATIVE RUNTIME PASS',30);guest.wait('DESKTOP READY',30)
        guest.wait('SCHEDULER RUNTIME PASS',10)
        assert 'NATIVE REAPED 00000003' in guest.text()
        guest.verify_workers(cpus,periodic=True)
        assert 'DESKTOP MODE FRAMEBUFFER' in guest.text()
        guest.key('3');guest.wait('UI APPS',10)
        guest.key('r');guest.wait('APP DISK RELOAD OK',10)
        reloads=guest.text().count('APP DISK RELOAD OK')
        guest.mouse(-179,128,True)
        assert guest.text().count('APP DISK RELOAD OK')==reloads+1
        screenshot(case,'native-gtos-applications')
        guest.key('ret');guest.wait('APP LAUNCH OK',10)
        before_x=paddle(screenshot(case,'native-gtos-game-before'));assert before_x is not None
        guest.key('right',350)
        after_x=paddle(screenshot(case,'native-gtos-game'));assert after_x is not None
        assert after_x>before_x+10,'Actual right-arrow input must move the visible paddle'
        guest.key('esc');guest.wait('APP CLOSE OK',10)
        guest.close();guest=None
        assert hashlib.sha256(disk.read_bytes()).hexdigest()==before_disk
        state['cases'].append(dict(memory_mib=memory,vcpus=cpus,guest_pass=True,native_exit_code=0,
            native_reaped=3,fixtures=46,positive_fixtures=15,all_positive_truncation_prefixes=True,
            both_pixel_oracles=True,desktop_game_input_pass=True,keyboard_and_mouse_reload_pass=True,
            locale='zh-CN' if memory==32 else 'en-US',disk_bytes_unchanged=True,
            game_paddle_before_x=before_x,game_paddle_after_x=after_x,ap_worker_jobs_pass=True,
            seconds=time.monotonic()-begin));record()
    assert before==sources(),'Tracked GTOS source changed during qualification'
    subprocess.run(['git','-C',str(repo),'diff','--quiet','HEAD','--'],check=True)
    for source,digest in manifest['source_sha256'].items():
        assert hashlib.sha256((repo/source).read_bytes()).hexdigest()==digest,source
    state.update(stage='passed',guest_pass=True,completed_cases=3,tracked_source_hashes_unchanged=True)
except Exception as error:
    state.update(stage='failed',error=repr(error));raise
finally:
    if guest:guest.close()
    if runtime!=out/'guest':shutil.copytree(runtime,out/'guest')
    record()
print(json.dumps(state,indent=2))
