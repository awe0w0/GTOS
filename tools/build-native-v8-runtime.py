#!/usr/bin/env python3
"""Build the pinned GTOS IA32 diagnostic runtime without a host C/C++ runtime."""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess
import traceback

repo = pathlib.Path(__file__).resolve().parents[1]
module = repo / "apps/native_v8_runtime"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--clang", required=True, type=pathlib.Path)
parser.add_argument("--libcxx-source", required=True, type=pathlib.Path)
parser.add_argument("--memory-cxx", required=True, type=pathlib.Path)
parser.add_argument("--enable-diagnostic", action="store_true")
args = parser.parse_args()
assert args.enable_diagnostic, "This private ABI requires --enable-diagnostic"
out = args.output.resolve()
assert not out.exists(), "Choose a fresh build directory; caches are never removed"
out.mkdir(parents=True)
lock = json.loads((module / "source-lock.json").read_text())
state = dict(scope=lock["scope"], native_build_pass=False,
             native_isolate_pass=False, full_v8_backend=False,
             browser_guest_pass=False, video_guest_pass=False, html5_guest_pass=False,
             production_image_changed=False, capacity_changed=False,
             inputs={}, commands=[], objects=[], qualification_reused=False)

def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def bind(path):
    path = pathlib.Path(path).resolve()
    value = sha(path)
    if str(path) in state["inputs"]:
        assert state["inputs"][str(path)] == value, "Changed build input"
    state["inputs"][str(path)] = value

def run(name, command):
    command = [str(x) for x in command]
    with (out / (name + ".log")).open("wb") as stream:
        result = subprocess.run(command, cwd=repo, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=180)
    state["commands"].append(dict(name=name, argv=command, exit_code=result.returncode))
    assert result.returncode == 0, name + " failed; inspect its log"
    return (out / (name + ".log")).read_text()

try:
    bind(pathlib.Path(__file__))
    bind(module / "source-lock.json")
    for relative, item in lock["files"].items():
        assert ".." not in pathlib.PurePosixPath(relative).parts
        path = module / relative
        assert sha(path) == item["sha256"], "Changed locked source: " + relative
        bind(path)
    clang = args.clang.resolve()
    libcxx = args.libcxx_source.resolve()
    memory_cxx = args.memory_cxx.resolve()
    assert sha(clang) == lock["clang_sha256"], "Wrong pinned Clang executable"
    revision = subprocess.check_output(["git", "-C", str(libcxx), "rev-parse", "HEAD"], text=True).strip()
    assert revision == lock["libcxx_revision"], "Wrong libc++ source revision"
    version = subprocess.check_output([str(memory_cxx), "-dumpfullversion"], text=True).strip()
    assert version == "13.3.0", "Use the qualified GCC13.3 memory compiler"
    for path in (clang, memory_cxx):
        bind(path)
    includes = ["-isystem", module / "sdk", "-isystem", libcxx / "include",
                "-isystem", module / "c-sdk", "-isystem", clang.parent.parent / "lib/clang/24/include",
                "-I", module / "include", "-I", module / "runtime"]
    flags = lock["qualified_native_flags"]
    objects = []
    for source in sorted((module / "runtime").glob("*.cc")):
        obj = out / (source.stem + ".o")
        dep = out / (source.stem + ".d")
        if source.name == "memory.cc":
            command = [memory_cxx, *lock["qualified_memory_flags"], "-O2",
                       "-I", module / "include", "-c", source, "-o", obj]
            run(source.stem, command)
        else:
            # Expanded dependencies are recorded before compiling and must stay
            # within the locked SDK/module, pinned libc++ or compiler resources.
            text = run(source.stem + "-dependencies",
                       [clang, *flags, *includes, "-M", "-MF", dep, "-MT", "runtime", source])
            del text
            dependencies = dep.read_text().replace("\\\n", " ").split(":", 1)[1].split()
            assert dependencies
            for value in dependencies:
                p = pathlib.Path(value).resolve()
                relative = None
                for root in (module, libcxx / "include", clang.parent.parent / "lib/clang/24/include"):
                    try:
                        relative = p.relative_to(root)
                        break
                    except ValueError:
                        pass
                assert relative is not None, "Unexpected target header: " + str(p)
                bind(p)
            run(source.stem, [clang, *flags, *includes, "-MD", "-MF", dep, "-c", source, "-o", obj])
        assert sha(obj) == lock["qualified_export_object_hashes"][source.name], "Object differs from executed qualification: " + source.name
        raw = obj.read_bytes()
        assert raw[:7] == b"\x7fELF\x01\x01\x01" and struct.unpack_from("<HH", raw, 16) == (1, 3)
        objects.append(obj)
        state["objects"].append(dict(name=source.name, bytes=len(raw), sha256=sha(obj)))
    archive = out / "libgtos_native_runtime_diagnostic.a"
    ar = pathlib.Path(shutil.which("ar")).resolve()
    ld = pathlib.Path(shutil.which("ld")).resolve()
    nm = pathlib.Path(shutil.which("nm")).resolve()
    for path in (ar, ld, nm):
        bind(path)
    run("archive", [ar, "rcsD", archive, *objects])
    closure = out / "runtime-closure.o"
    run("closure", [ld, "-melf_i386", "-r", "-o", closure, "--whole-archive", archive, "--no-whole-archive"])
    assert not run("undefined", [nm, "-u", closure]).strip(), "Runtime subset has unresolved symbols"
    symbols = run("symbols", [nm, "-n", closure])
    for name in ("__emutls_get_address", "__cxa_thread_atexit", "__dso_handle",
                 "gtos_native_exit_with_tls", "gtos_v8_clock_read_microseconds"):
        assert re.search(r"\b" + name + r"$", symbols, re.M), "Missing actual runtime symbol"
    assert all(sha(path) == value for path, value in state["inputs"].items())
    state.update(native_build_pass=True, source_before_after_identical=True, qualification_reused=True,
                 archive_sha256=sha(archive), closure_sha256=sha(closure),
                 llvm_sdk_header_declarations_only=True,
                 qualification="Build/link proof only; original executed cohorts remain in source-lock.json")
except BaseException as error:
    state["failure"] = str(error)
    (out / "exception.log").write_text(traceback.format_exc())
finally:
    state["timestamp_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / "manifest.json").write_text(json.dumps(state, indent=2) + "\n")
    print(json.dumps({k: v for k, v in state.items() if k not in ("inputs", "commands")}, indent=2))
raise SystemExit(0 if state["native_build_pass"] else 1)
