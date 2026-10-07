#!/usr/bin/env python3
"""Build a fresh BIOS + x64 UEFI live image from the normal i386 artifacts.

No installer, mount, disk device, NVRAM or firmware operation is performed.
Packaging checks are recorded separately from the required guest boot evidence.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys


MARKER_PATH = "/boot/gtos-live-v1.marker"
EFI_MODULES = (
    "normal configfile multiboot search search_fs_file iso9660 fat "
    "part_gpt part_msdos efi_gop sleep test echo terminal serial"
)
# Keep serial available in the memdisk, but initialize it only for its menu item.
EFI_PRELOAD_MODULES = " ".join(name for name in EFI_MODULES.split() if name != "serial")
INPUT_FILES = (
    ("GTOS.bin", "/boot/GTOS.bin"),
    ("apps/catch.gtapp", "/boot/catch.gtapp"),
    ("obj/native/fault.elf", "/boot/native-fault.elf"),
    ("obj/native/peer.elf", "/boot/native-peer.elf"),
    ("obj/browser-probe/browser-probe.elf", "/boot/browser-probe.elf"),
    ("docs/desktop-font-license.txt", "/boot/licenses/desktop-font-license.txt"),
    ("apps/fonts/OFL.txt", "/boot/licenses/Noto-OFL.txt"),
    ("apps/fonts/README.md", "/boot/licenses/noto-font-provenance.md"),
    ("apps/fonts/han_manifest.json", "/boot/licenses/han_manifest.json"),
)

BOOT_MODULES = tuple(path for _, path in INPUT_FILES[1:5])
MENU_ENTRIES = (
    ("gtos-live-default", "GTOS Live (RAM only)", ()),
    ("gtos-live-bootlog", "GTOS Live (boot log; press any key after logs)", ("bootlog",)),
    ("gtos-live-serial", "GTOS Live (boot log + COM1; serial hardware required)",
     ("bootlog", "serial")),
)
DEFAULT_ENTRY = "gtos-live-default"
DIAGNOSTIC_OMISSIONS = {
    "missing-kernel": "/boot/GTOS.bin",
    "missing-module": "/boot/native-fault.elf",
}
LOADER_HOLD = """function gtos_loader_hold {
 terminal_output console
 echo "[GTOS LOADER STOP] No kernel handoff. Power off or reset to retry."
 while [ 1 = 1 ]; do
  sleep 86400
 done
}
"""


def boot_entries(diagnostic_failure=None):
    """Every menu entry preserves the four native module slots and live policy."""
    if diagnostic_failure:
        extra = ("bootlog", "bootlog-fail") if diagnostic_failure == "kernel-panic" else ("bootlog",)
        selected = (("gtos-live-test", "GTOS Live (DIAGNOSTIC TEST ONLY: %s)" % diagnostic_failure, extra),)
    else:
        selected = MENU_ENTRIES
    return [{"id": ident, "title": title,
             "kernel_arguments": {"bios": ["live"] + list(extra),
                                  "efi": ["live", "uefi"] + list(extra)},
             "module_slots": list(BOOT_MODULES)} for ident, title, extra in selected]


def module_gates(indent="  ", index=0):
    if index == len(BOOT_MODULES):
        return [indent + 'echo "[GTOS LOADER L30] handoff to ELF32 kernel"',
                indent + "boot",
                indent + 'echo "[GTOS LOADER L04] kernel handoff returned without boot"',
                indent + "gtos_loader_hold"]
    path = BOOT_MODULES[index]
    return ([indent + "if module %s; then" % path] +
            module_gates(indent + " ", index + 1) +
            [indent + "else",
             indent + ' echo "[GTOS LOADER L03] boot module load failed: %s"' % path,
             indent + " gtos_loader_hold", indent + "fi"])


def live_config(diagnostic_failure=None, default_entry=DEFAULT_ENTRY):
    entries = boot_entries(diagnostic_failure)
    selected = "gtos-live-test" if diagnostic_failure else default_entry
    lines = ["set timeout=3", "set default=" + selected, "terminal_output console",
             LOADER_HOLD.rstrip()]
    for entry in entries:
        lines += ['menuentry "%s" --id %s {' % (entry["title"], entry["id"]),
                  " terminal_output console"]
        if "serial" in entry["kernel_arguments"]["bios"]:
            lines += [" if insmod serial; then",
                      "  if serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1; then",
                      "   terminal_output console serial",
                      '   echo "[GTOS LOADER L22] optional COM1 logging enabled (115200 8N1)"',
                      "  else",
                      '   echo "[GTOS LOADER L23] COM1 unavailable; console logging remains active"',
                      "  fi", " else",
                      '  echo "[GTOS LOADER L23] serial module unavailable; console logging remains active"',
                      " fi"]
        lines += [' echo "[GTOS LOADER L20] loading ELF32 kernel: /boot/GTOS.bin"',
                  " set gtos_kernel_loaded=0", ' if [ "$grub_platform" = "efi" ]; then',
                  "  if insmod efi_gop; then",
                  "   if multiboot /boot/GTOS.bin %s; then" % " ".join(entry["kernel_arguments"]["efi"]),
                  "    set gtos_kernel_loaded=1", "   else",
                  '    echo "[GTOS LOADER L02] kernel load failed: /boot/GTOS.bin"',
                  "    gtos_loader_hold", "   fi", "  else",
                  '   echo "[GTOS LOADER L05] EFI GOP loader module unavailable"',
                  "   gtos_loader_hold", "  fi", " else", "  if insmod vbe; then",
                  "   if multiboot /boot/GTOS.bin %s; then" % " ".join(entry["kernel_arguments"]["bios"]),
                  "    set gtos_kernel_loaded=1", "   else",
                  '    echo "[GTOS LOADER L02] kernel load failed: /boot/GTOS.bin"',
                  "    gtos_loader_hold", "   fi", "  else",
                  '   echo "[GTOS LOADER L05] BIOS VBE loader module unavailable"',
                  "   gtos_loader_hold", "  fi", " fi",
                  ' if [ "$gtos_kernel_loaded" = "1" ]; then',
                  "  set gfxpayload=800x600x32,640x480x32",
                  '  echo "[GTOS LOADER L21] loading four boot modules"']
        lines += module_gates()
        lines += [" else", '  echo "[GTOS LOADER L02] kernel was not loaded; refusing handoff"',
                  "  gtos_loader_hold", " fi", "}"]
    return "\n".join(lines) + "\n"


EFI_CONFIG = """# EFI modules remain in the standalone memdisk, never the BIOS directory.
set prefix=(memdisk)/boot/grub
terminal_output console
echo "[GTOS LOADER L01] x64 EFI entry"
echo "[GTOS LOADER L10] searching live medium: /boot/gtos-live-v1.marker"
""" + LOADER_HOLD + """if search --no-floppy --file --set=root /boot/gtos-live-v1.marker; then
 if [ -f ($root)/boot/gtos-live-v1.marker ]; then
  echo "[GTOS LOADER L11] live medium found"
  if [ -f ($root)/boot/grub/grub.cfg ]; then
   echo "[GTOS LOADER L12] live configuration found"
   configfile ($root)/boot/grub/grub.cfg
   echo "[GTOS LOADER L92] live configuration returned without kernel handoff"
   gtos_loader_hold
  else
   echo "[GTOS LOADER L91] live configuration missing: /boot/grub/grub.cfg"
   gtos_loader_hold
  fi
 else
  echo "[GTOS LOADER L90] live medium marker missing: /boot/gtos-live-v1.marker"
  gtos_loader_hold
 fi
else
 echo "[GTOS LOADER L90] live medium marker missing: /boot/gtos-live-v1.marker"
 gtos_loader_hold
fi
"""
LIVE_CONFIG = live_config()


def check_module_dependencies(folder, requested):
    """Resolve official moddep.lst without altering modules or installation."""
    dependencies = {}
    for line in (folder / "moddep.lst").read_text().splitlines():
        name, separator, values = line.partition(":")
        if separator:
            dependencies[name.strip()] = values.split()
    resolved = set()
    def visit(name):
        if name in resolved:
            return
        if not (folder / (name + ".mod")).is_file() or name not in dependencies:
            raise ValueError("missing GRUB module/dependency:%s" % (folder / (name + ".mod")))
        resolved.add(name)
        for dependency in dependencies[name]:
            visit(dependency)
    for name in requested:
        visit(name)
    return sorted(resolved)


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def file_record(path):
    path = Path(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": digest.hexdigest()}


def write_new(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def copy_new(source, destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with Path(source).open("rb") as src, destination.open("xb") as dst:
        shutil.copyfileobj(src, dst)


def inspect_kernel(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("kernel must be a little-endian ELF32 image")
    fields = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    elf_type, machine, version, entry, phoff = fields[:5]
    phentsize, phnum = fields[8:10]
    if (elf_type, machine, version) != (2, 3, 1) or phentsize != 32:
        raise ValueError("kernel must be an executable i386 ELF32")
    if not phnum or phoff + phentsize * phnum > len(data):
        raise ValueError("kernel program headers are out of bounds")
    loads = []
    for i in range(phnum):
        values = struct.unpack_from("<IIIIIIII", data, phoff + i * phentsize)
        kind, offset, virtual, physical, filesz, memsz, flags, align = values
        if kind != 1:
            continue
        if (filesz > memsz or offset + filesz > len(data) or
                physical + memsz > 0x100000000 or virtual != physical):
            raise ValueError("kernel load segment does not fit identity-addressed i386")
        loads.append({"offset": offset, "address": physical,
                      "file_bytes": filesz, "memory_bytes": memsz,
                      "flags": flags, "alignment": align})
    if not any(s["address"] <= entry < s["address"] + s["memory_bytes"]
               and s["flags"] & 1 for s in loads):
        raise ValueError("kernel entry is outside an executable load segment")
    header = None
    for offset in range(0, min(8192, len(data)) - 11, 4):
        magic, flags, checksum = struct.unpack_from("<III", data, offset)
        if magic == 0x1BADB002 and (magic + flags + checksum) & 0xFFFFFFFF == 0:
            header = {"file_offset": offset, "magic": magic, "flags": flags,
                      "checksum": checksum}
            break
    if header is None or header["flags"] != 7:
        raise ValueError("expected published Multiboot1 header flags7")
    return {"class": "ELF32", "machine": "i386", "entry": entry,
            "multiboot_version": 1, "header": header, "load_segments": loads}


def inspect_efi(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("EFI loader lacks the DOS/PE header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("EFI loader PE header is out of bounds")
    machine, sections, _, _, _, optional_bytes, characteristics = \
        struct.unpack_from("<HHIIIHH", data, pe + 4)
    optional = pe + 24
    if optional_bytes < 152 or optional + optional_bytes > len(data):
        raise ValueError("EFI loader optional header is out of bounds")
    magic = struct.unpack_from("<H", data, optional)[0]
    subsystem = struct.unpack_from("<H", data, optional + 68)[0]
    directories = struct.unpack_from("<I", data, optional + 108)[0]
    security_offset, security_bytes = struct.unpack_from("<II", data, optional + 144)
    if (machine, magic, subsystem) != (0x8664, 0x20B, 10) or not sections:
        raise ValueError("EFI loader must be an AMD64 PE32+ EFI application")
    if directories < 5 or security_offset or security_bytes:
        raise ValueError("this builder expects an unsigned EFI application")
    return {"machine": "AMD64", "machine_code": machine, "format": "PE32+",
            "subsystem": "EFI application", "subsystem_code": subsystem,
            "section_count": sections, "characteristics": characteristics,
            "secure_boot_signature_present": False}


def parse_catalog(text):
    images = []
    for line in text.splitlines():
        match = re.search(r"El Torito boot img\s*:\s*(\d+)\s+(BIOS|UEFI)\s+(\S+)", line)
        if match:
            images.append({"number": int(match.group(1)),
                           "platform": match.group(2), "bootable": match.group(3)})
    if not any(x["platform"] == "BIOS" and x["bootable"] == "y" for x in images):
        raise ValueError("ISO does not contain a bootable BIOS El Torito entry")
    if not any(x["platform"] == "UEFI" and x["bootable"] == "y" for x in images):
        raise ValueError("ISO does not contain a bootable EFI El Torito entry")
    # xorriso's report identifies the GPT EFI System Partition by its full GUID.
    guid_text = text.lower().replace("-", "")
    if not any(guid in guid_text for guid in (
            "c12a7328f81f11d2ba4b00a0c93ec93b",
            "28732ac11ff8d211ba4b00a0c93ec93b")):
        raise ValueError("ISO GPT does not advertise an EFI System Partition")
    return {"images": images, "bios_platform_id": 0, "efi_platform_id": 239,
            "gpt_efi_system_partition_present": True}


class Builder:
    def __init__(self, evidence, manifest):
        self.evidence = evidence
        self.manifest = manifest

    def run(self, name, argv, timeout=180, version=False):
        index = len(self.manifest["commands"])
        log = self.evidence / "commands" / ("%02d-%s.log" % (index, name))
        command = {"name": name, "argv": [str(x) for x in argv],
                   "started_utc": utc_now(), "log": str(log)}
        self.manifest["commands"].append(command)
        result = subprocess.run(command["argv"], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        write_new(log, result.stdout)
        command.update(returncode=result.returncode, finished_utc=utc_now(),
                       log_sha256=file_record(log)["sha256"])
        if result.returncode:
            raise RuntimeError("%s exited%d; see%s" % (name, result.returncode, log))
        output = result.stdout.decode("utf-8", errors="replace")
        if version:
            command["version_output"] = output.strip()
        return output


def env_path(name, fallback=None):
    value = os.environ.get(name)
    return value if value else fallback


def main():
    runtime_value = os.environ.get("GTOS_RUNTIME")
    runtime = Path(runtime_value) if runtime_value else None
    runtime_root = runtime / "root" if runtime else None

    def common_tool(name):
        return str(runtime_root / "usr/bin" / name) if runtime_root else name

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--output", default="GTOS-live.iso")
    parser.add_argument("--evidence-dir")
    parser.add_argument("--uefi-media-dir")
    parser.add_argument("--default-entry", choices=[item[0] for item in MENU_ENTRIES],
                        default=DEFAULT_ENTRY,
                        help="Select a diagnostic default only for a separate private test image")
    parser.add_argument("--diagnostic-failure", choices=("missing-kernel", "missing-module", "kernel-panic"),
                        help="Build an isolated, non-deliverable negative-test image")
    parser.add_argument("--grub-mkstandalone", default=env_path("GTOS_UEFI_MKSTANDALONE", common_tool("grub-mkstandalone")))
    parser.add_argument("--grub-mkrescue", default=env_path("GTOS_UEFI_MKRESCUE", common_tool("grub-mkrescue")))
    parser.add_argument("--grub-mkimage", default=env_path("GTOS_UEFI_MKIMAGE", common_tool("grub-mkimage")))
    parser.add_argument("--grub-script-check", default=env_path("GTOS_UEFI_SCRIPT_CHECK", common_tool("grub-script-check")))
    parser.add_argument("--bios-modules", default=env_path("GTOS_UEFI_BIOS_MODULES", str(runtime_root / "usr/lib/grub/i386-pc") if runtime_root else "/usr/lib/grub/i386-pc"))
    parser.add_argument("--efi-modules", default=env_path("GTOS_UEFI_EFI_MODULES", str(runtime_root / "usr/lib/grub/x86_64-efi") if runtime_root else "/usr/lib/grub/x86_64-efi"))
    parser.add_argument("--xorriso", default=env_path("GTOS_UEFI_XORRISO", common_tool("xorriso")))
    parser.add_argument("--mformat", default=env_path("GTOS_UEFI_MFORMAT", "mformat"))
    parser.add_argument("--mcopy", default=env_path("GTOS_UEFI_MCOPY", "mcopy"))
    args = parser.parse_args()
    source = Path(args.source_root).resolve()
    output = Path(args.output).resolve()
    if args.diagnostic_failure:
        if args.default_entry != DEFAULT_ENTRY:
            raise ValueError("negative-test entry selection cannot be combined with --default-entry")
        if output == source / "GTOS-live.iso":
            raise ValueError("diagnostic failure requires a separate explicit --output outside the normal ISO path")
    omitted = [DIAGNOSTIC_OMISSIONS[args.diagnostic_failure]] if args.diagnostic_failure in DIAGNOSTIC_OMISSIONS else []
    selected_default = "gtos-live-test" if args.diagnostic_failure else args.default_entry
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    evidence = Path(args.evidence_dir).resolve() if args.evidence_dir else source / "obj" / ("uefi-live-%s-%d" % (stamp, os.getpid()))
    media = Path(args.uefi_media_dir).resolve() if args.uefi_media_dir else evidence / "UEFI-media"
    for path in (output, evidence, media):
        if path.exists():
            raise ValueError("refusing to overwrite existing output:%s" % path)
    for value in (args.bios_modules, args.efi_modules):
        module_dir = Path(value)
        for required in ("modinfo.sh", "moddep.lst", "kernel.img", "multiboot.mod", "normal.mod"):
            if not (module_dir / required).is_file():
                raise ValueError("missing GRUB module input:%s" % (module_dir / required))
    dependency_closures = {
        "bios": check_module_dependencies(Path(args.bios_modules),
                  [name for name in EFI_MODULES.split() if name != "efi_gop"] + ["vbe"]),
        "efi": check_module_dependencies(Path(args.efi_modules), EFI_MODULES.split()),
    }
    for relative, _ in INPUT_FILES:
        path = source / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError("missing normal build input:%s" % path)
    kernel = inspect_kernel(source / "GTOS.bin")
    evidence.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": "gtos-uefi-live-build-v1", "created_utc": utc_now(),
                "source_root": str(source), "kernel_header": kernel,
                "uefi_loader_architecture": "x86_64-efi", "kernel_architecture": "i386",
                "secure_boot": "unsigned; no Secure Boot qualification",
                "boot_qualification": "pending", "build_pass": False,
                "diagnostic_failure": args.diagnostic_failure,
                "diagnostic_omitted_paths": omitted,
                "boot_menu_default": selected_default,
                "boot_entries": boot_entries(args.diagnostic_failure),
                "delivery_eligible": not args.diagnostic_failure and args.default_entry == DEFAULT_ENTRY,
                "loader_diagnostics": {"stage": "GRUB before kernel handoff",
                    "failure_hold": "console with permanent non-echoing sleep loop; no automatic reboot",
                    "kernel_failure_code": "L02", "module_failure_code": "L03",
                    "serial": "optional COM1 115200 8N1; selected serial entry only",
                    "firmware_logs": "firmware output is outside GTOS control"},
                "efi_modules_installed": EFI_MODULES.split(),
                "efi_modules_preloaded": EFI_PRELOAD_MODULES.split(),
                "commands": [], "input_files": [], "helper_files": [],
                "grub_inputs": {}, "outputs": {}}
    builder = Builder(evidence, manifest)
    try:
        for name, executable, flag in (
            ("grub-mkstandalone-version", args.grub_mkstandalone, "--version"),
            ("grub-mkrescue-version", args.grub_mkrescue, "--version"),
            ("grub-mkimage-version", args.grub_mkimage, "--version"),
            ("grub-script-check-version", args.grub_script_check, "--version"),
            ("xorriso-version", args.xorriso, "-version"),
            ("mformat-version", args.mformat, "-V"),
            ("mcopy-version", args.mcopy, "-V"),
        ):
            builder.run(name, [executable, flag], timeout=30, version=True)
            resolved = shutil.which(executable)
            if not resolved:
                raise ValueError("helper executable not found:%s" % executable)
            manifest["helper_files"].append({"command": executable,
                                             "file": file_record(Path(resolved).resolve())})
        for platform, value in (("bios", args.bios_modules), ("efi", args.efi_modules)):
            folder = Path(value).resolve()
            files = sorted(x for x in folder.iterdir() if x.is_file())
            manifest["grub_inputs"][platform] = {
                "directory": str(folder), "modinfo": (folder / "modinfo.sh").read_text(),
                "resolved_module_dependencies": dependency_closures[platform],
                "files": [file_record(x) for x in files]}
        stage = evidence / "iso-stage"
        stage.mkdir()
        for relative, iso_path in INPUT_FILES:
            original = source / relative
            destination = stage / iso_path.lstrip("/")
            before = file_record(original)
            present = iso_path not in omitted
            if present:
                copy_new(original, destination)
                if file_record(destination)["sha256"] != before["sha256"]:
                    raise ValueError("staged source hash changed:%s" % original)
            manifest["input_files"].append({"source": str(original), "iso_path": iso_path,
                                            "bytes": before["bytes"], "sha256": before["sha256"],
                                            "present_in_media": present})
        marker = ("GTOS live v1\nkernel-sha256:%s\n" % manifest["input_files"][0]["sha256"]).encode("ascii")
        if args.diagnostic_failure:
            marker += ("DIAGNOSTIC TEST ONLY; NOT A DELIVERY IMAGE\ndiagnostic-failure:%s\n" % args.diagnostic_failure).encode("ascii")
        write_new(stage / MARKER_PATH.lstrip("/"), marker)
        config = stage / "boot/grub/grub.cfg"
        embedded = evidence / "embedded-efi.cfg"
        write_new(config, live_config(args.diagnostic_failure, args.default_entry).encode("ascii"))
        write_new(embedded, EFI_CONFIG.encode("ascii"))
        builder.run("live-config-syntax", [args.grub_script_check, str(config)], timeout=30)
        builder.run("embedded-config-syntax", [args.grub_script_check, str(embedded)], timeout=30)
        # Qualify each generated grammar, including isolated negative-test variants.
        variants = [("normal", None, DEFAULT_ENTRY),
                    ("bootlog-default", None, "gtos-live-bootlog"),
                    ("serial-default", None, "gtos-live-serial")]
        variants += [(failure, failure, DEFAULT_ENTRY) for failure in
                     ("missing-kernel", "missing-module", "kernel-panic")]
        for name, failure, default in variants:
            sample = evidence / "config-syntax" / (name + ".cfg")
            write_new(sample, live_config(failure, default).encode("ascii"))
            builder.run(name + "-config-syntax", [args.grub_script_check, str(sample)], timeout=30)
        efi = stage / "EFI/BOOT/BOOTX64.EFI"
        efi.parent.mkdir(parents=True)
        builder.run("efi-standalone", [args.grub_mkstandalone, "-O", "x86_64-efi",
                    "-d", args.efi_modules, "--grub-mkimage=" + args.grub_mkimage,
                    "--locales=", "--fonts=", "--themes=", "--install-modules=" + EFI_MODULES,
                    "--modules=" + EFI_PRELOAD_MODULES, "-o", str(efi), "boot/grub/grub.cfg=" + str(embedded)])
        manifest["pe_header"] = inspect_efi(efi)
        # One fixed-size FAT image; no host device paths or formatting utility.
        fat = stage / "efi.img"
        builder.run("efi-fat-format", [args.mformat, "-C", "-t", "128", "-h", "16", "-n", "8",
                                      "-i", str(fat), "::"], timeout=30)
        if fat.stat().st_size != 8 * 1024 * 1024:
            raise ValueError("EFI FAT image has an unexpected size")
        builder.run("efi-fat-copy", [args.mcopy, "-s", "-i", str(fat), str(stage / "EFI"), "::/"], timeout=30)
        extracted = evidence / "efi-fat-extracted.EFI"
        builder.run("efi-fat-extract", [args.mcopy, "-i", str(fat), "::/EFI/BOOT/BOOTX64.EFI", str(extracted)], timeout=30)
        if file_record(extracted)["sha256"] != file_record(efi)["sha256"]:
            raise ValueError("FAT EFI loader bytes differ from the qualified PE input")
        internal_iso = evidence / "GTOS-live.iso"
        builder.run("bios-efi-iso", [args.grub_mkrescue, "-d", args.bios_modules,
                    "--grub-mkimage=" + args.grub_mkimage, "--xorriso=" + args.xorriso,
                    "-o", str(internal_iso), str(stage), "--efi-boot", "efi.img",
                    "-efi-boot-part", "--efi-boot-image"], timeout=300)
        reports = builder.run("boot-catalog", [args.xorriso, "-indev", str(internal_iso),
                              "-report_el_torito", "plain", "-report_system_area", "plain"], timeout=30)
        manifest["boot_catalog"] = parse_catalog(reports)
        extraction = evidence / "iso-extracted"
        builder.run("iso-extract", [args.xorriso, "-osirrox", "on", "-indev", str(internal_iso),
                                  "-extract", "/", str(extraction)], timeout=60)
        expected = [(relative, path) for relative, path in INPUT_FILES if path not in omitted] + [(None, MARKER_PATH), (None, "/boot/grub/grub.cfg"),
                                       (None, "/EFI/BOOT/BOOTX64.EFI"), (None, "/efi.img")]
        for _, iso_path in expected:
            staged = stage / iso_path.lstrip("/")
            actual = extraction / iso_path.lstrip("/")
            if file_record(staged)["sha256"] != file_record(actual)["sha256"]:
                raise ValueError("ISO extracted bytes differ:%s" % iso_path)
        for iso_path in omitted:
            if (stage / iso_path.lstrip("/")).exists() or (extraction / iso_path.lstrip("/")).exists():
                raise ValueError("negative-test omitted file is unexpectedly present:%s" % iso_path)
        media.mkdir(parents=True, exist_ok=False)
        for folder in ("EFI", "boot"):
            shutil.copytree(stage / folder, media / folder)
        manifest["outputs"]["uefi_media_directory"] = str(media)
        manifest["outputs"]["uefi_media_files"] = [file_record(x) for x in sorted(media.rglob("*")) if x.is_file()]
        manifest["outputs"]["efi_loader"] = file_record(efi)
        manifest["outputs"]["efi_fat_image"] = file_record(fat)
        manifest["checks"] = {"elf32_multiboot1": True, "unsigned_amd64_efi_application": True,
                              "bounded_fat_loader_round_trip": True,
                              "bios_and_efi_boot_catalog_with_gpt_esp": True,
                              "iso_payload_round_trip": True,
                              "all_generated_grub_configs_syntax": True,
                              "official_module_dependency_closure_present": True,
                              "diagnostic_omissions_verified": bool(omitted),
                              "actual_ovmf_boot": False, "actual_bios_boot": False}
        # Recheck inputs after packaging; do not mix concurrently rebuilt bytes.
        for item in manifest["input_files"]:
            if file_record(item["source"])["sha256"] != item["sha256"]:
                raise ValueError("source artifact changed during packaging:%s" % item["source"])
        copy_new(internal_iso, output)
        manifest["outputs"]["iso"] = file_record(output)
        manifest["build_pass"] = True
        manifest["finished_utc"] = utc_now()
    except Exception as exc:
        manifest["failure"] = {"type": type(exc).__name__, "message": str(exc), "utc": utc_now()}
        write_new(evidence / "manifest.json", (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
        raise
    write_new(evidence / "manifest.json", (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    print(json.dumps({"build_pass": True, "boot_qualification": "pending",
                      "delivery_eligible": manifest["delivery_eligible"],
                      "diagnostic_failure": args.diagnostic_failure,
                      "manifest": str(evidence / "manifest.json"),
                      "iso": manifest["outputs"]["iso"], "uefi_media_directory": str(media)}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("UEFI live image build failed:%s" % error, file=sys.stderr)
        sys.exit(1)
