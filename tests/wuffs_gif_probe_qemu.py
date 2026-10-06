#!/usr/bin/env python3
"""Run the standalone decoder app in the existing i386 GTOS native loader."""
import argparse, datetime, hashlib, json, pathlib, shutil, subprocess, sys, time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('build',type=pathlib.Path,help='Successful build-wuffs-gif-probe output')
parser.add_argument('output',type=pathlib.Path,help='New evidence directory')
parser.add_argument('--kernel-stage',type=pathlib.Path)
parser.add_argument('--runtime-root',type=pathlib.Path,help='Optional existing temporary directory for QEMU files')
args=parser.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
app=repo/'apps/wuffs_gif_probe'
build=args.build.resolve();out=args.output.resolve()
manifest=json.loads((build/'manifest.json').read_text())
assert manifest['build_pass'] and manifest['host_asan_ubsan_pass']
elf=build/'wuffs-gif-probe.elf'
assert hashlib.sha256(elf.read_bytes()).hexdigest()==manifest['elf_sha256']
kernel_stage=(args.kernel_stage or repo/'obj/iso-native-test').resolve()
assert (kernel_stage/'boot/GTOS.bin').is_file(),'Build make GTOS-native-test.iso first'
assert not out.exists(),'Choose a fresh evidence directory'
out.mkdir(parents=True)
runtime=out/'guest'
if args.runtime_root:
    runtime=args.runtime_root.resolve()/('wuffs-gif-'+out.name)
    assert not runtime.exists(),'Choose a fresh runtime directory'
runtime.mkdir()
tracked=subprocess.check_output(['git','-C',str(repo),'ls-files','-z']).split(b'\0')
def sources():
    return {name.decode():hashlib.sha256((repo/name.decode()).read_bytes()).hexdigest()
            for name in tracked if name and (repo/name.decode()).is_file()}
before=sources()
state=dict(scope='Real Wuffs first-frame GIF decoder in existing GTOS i386 ABI1, not Chromium',
           kernel_revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),
           elf_sha256=manifest['elf_sha256'],guest_pass=False,browser_guest_pass=False,
           kernel_source_changed=False,original_browser_probe_changed=False,elf64_path_used=False,cases=[])
guest=None
def record():
    state['timestamp_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'results.json').write_text(json.dumps(state,indent=2)+'\n')
def run(name,command):
    with (out/(name+'.log')).open('w') as log:
        subprocess.run([str(x) for x in command],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
record()
try:
    host_io=out/'host-io.o';validator=out/'actual-elf32-validator'
    run('host-io-build',['gcc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-c',app/'validation_host.c','-o',host_io])
    run('host-validator-build',['g++','-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',
        '-I'+str(repo/'include'),repo/'src/process/elf32.cpp',app/'validation_host.cpp',host_io,'-o',validator])
    run('actual-elf32-validation',[validator,elf])
    assert 'REAL GTOS ELF32 VALIDATOR PASS' in (out/'actual-elf32-validation.log').read_text()
    stage=out/'iso-stage';shutil.copytree(kernel_stage,stage)
    shutil.copyfile(elf,stage/'boot/browser-probe.elf')
    state['kernel_binary_sha256']=hashlib.sha256((stage/'boot/GTOS.bin').read_bytes()).hexdigest()
    iso=out/'GTOS-wuffs-gif.iso'
    run('iso-build',['grub-mkrescue','--output='+str(iso),stage])
    extracted=runtime/'extracted.elf'
    run('iso-extract',['xorriso','-indev',iso,'-osirrox','on','-extract','/boot/browser-probe.elf',extracted])
    assert extracted.read_bytes()==elf.read_bytes();state['exact_iso_elf_hash_verified']=True
    sys.path.insert(0,str(repo/'tests'));import qemu_smoke
    qemu_smoke.BOOT_ISO=iso
    for memory,cpus in ((64,4),(32,1),(128,8)):
        case=runtime/('%dM-%dcpu'%(memory,cpus));case.mkdir()
        disk=case/'apps.img'
        run('%dM-%dcpu-disk'%(memory,cpus),[sys.executable,repo/'tools/disk.py','create',disk,'--size-mib','8'])
        state.update(stage='guest-running',current_case=dict(memory_mib=memory,vcpus=cpus));record()
        begin=time.monotonic();guest=qemu_smoke.Guest(case,disk,memory,cpus,wait_ready=False)
        guest.wait('GTOS WUFFS GIF START SCALAR 2000 MUTATIONS ABI1',30)
        guest.wait('GTOS WUFFS GIF PASS PIXELS BOUNDS 2000 MUTATIONS ABI1',180)
        guest.wait('BROWSER PROBE EXIT 00000000',20)
        guest.wait('NATIVE RUNTIME PASS',30);guest.wait('DESKTOP READY',30)
        guest.wait('SCHEDULER RUNTIME PASS',10)
        assert 'NATIVE REAPED 00000003' in guest.text()
        guest.verify_workers(cpus,periodic=True)
        guest.key('3');guest.key('i');guest.wait('APP INSTALL OK',10)
        guest.key('ret');guest.wait('APP LAUNCH OK',10);guest.key('right',200)
        ppm=case/'native-gtos-game.ppm';guest.call('screendump',{'filename':str(ppm)})
        with ppm.open('rb') as image:
            assert image.readline().strip()==b'P6'
            assert image.readline().strip()==b'800 600'
            assert image.readline().strip()==b'255'
            assert len(image.read())==800*600*3
        guest.close();guest=None
        state['cases'].append(dict(memory_mib=memory,vcpus=cpus,guest_pass=True,native_exit_code=0,
            native_reaped=3,mutations=2000,desktop_game_input_pass=True,ap_worker_jobs_pass=True,
            seconds=time.monotonic()-begin));record()
    assert before==sources(),'Tracked GTOS source changed during qualification'
    state.update(stage='passed',guest_pass=True,completed_cases=3,tracked_source_hashes_unchanged=True)
except Exception as error:
    state.update(stage='failed',error=repr(error));raise
finally:
    if guest:guest.close()
    if runtime!=out/'guest':shutil.copytree(runtime,out/'guest')
    record()
print(json.dumps(state,indent=2))
