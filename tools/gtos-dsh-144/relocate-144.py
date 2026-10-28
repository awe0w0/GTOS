"""Copy a real tree to a new absolute location, then optionally take named files out of the copy.

This exists so that a relocation test moves real bytes rather than describing a move. Two properties matter and both are checked
after the copy rather than assumed: every file arrives with the same bytes and the same permission bits, and the destination did
not exist before, so nothing is merged into a tree that was already there.

The copy is made on the Linux side, through the same guarded path as every other stage, because a copy made through the Windows
share does not carry POSIX permission bits and would make a permission check meaningless.

A second mode takes files out of an already relocated tree. That is how the negative is built: the same reader is pointed at the
same relocated tree, once complete and once with a real file genuinely absent, so the difference in its verdict is caused by the
absence and by nothing else.
"""

import hashlib
import json
import os
import shutil
import stat
import sys


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def walk_files(root):
    """Every ordinary file under root, as (relative path, absolute path), without following symlinks."""
    found = []
    for current, directories, files in os.walk(root, followlinks=False):
        directories.sort()
        for name in sorted(files):
            absolute = os.path.join(current, name)
            info = os.lstat(absolute)
            if not stat.S_ISREG(info.st_mode):
                continue
            found.append((os.path.relpath(absolute, root), absolute))
    return found


def main():
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        control = json.load(handle)

    stage = control["stage"]
    report = {
        "task": control.get("task"),
        "stage": stage,
        "mode": control["mode"],
        "source": control["source"],
        "destination": control["destination"],
        "problems": [],
    }

    source = control["source"]
    destination = control["destination"]

    if not os.path.isdir(source):
        report["problems"].append({"why": "the source is not a directory", "path": source})
        report["ok"] = False
        write(report, control)
        return 1

    if control["mode"] == "tree":
        # A destination that already exists is refused rather than merged: a merged tree cannot be described as a copy of
        # anything, and a relocation test that silently inherits a previous copy proves nothing about the bytes it moved.
        if os.path.lexists(destination):
            report["problems"].append({"why": "the destination already exists", "path": destination})
            report["ok"] = False
            write(report, control)
            return 1
        os.makedirs(destination, exist_ok=False)
        copied = []
        for relative, absolute in walk_files(source):
            target = os.path.join(destination, relative)
            parent = os.path.dirname(target)
            if parent:
                os.makedirs(parent, exist_ok=True)
            before = os.lstat(absolute)
            shutil.copyfile(absolute, target)
            os.chmod(target, stat.S_IMODE(before.st_mode))
            after = os.lstat(target)
            row = {
                "relative": relative,
                "bytes": after.st_size,
                "sha256": sha256_of(target),
                "mode_octal": format(stat.S_IMODE(after.st_mode), "04o"),
                "source_bytes": before.st_size,
                "source_sha256": sha256_of(absolute),
                "source_mode_octal": format(stat.S_IMODE(before.st_mode), "04o"),
            }
            row["identical"] = (
                row["bytes"] == row["source_bytes"]
                and row["sha256"] == row["source_sha256"]
                and row["mode_octal"] == row["source_mode_octal"]
            )
            if not row["identical"]:
                report["problems"].append({"why": "the copy differs from the original", "relative": relative})
            copied.append(row)
        report["files"] = copied
        report["counted"] = {
            "files": len(copied),
            "bytes": sum(row["bytes"] for row in copied),
            "identical": sum(1 for row in copied if row["identical"]),
            "executables": sum(1 for row in copied if row["mode_octal"] == "0755"),
        }
        report["source_counted"] = {
            "files": len(walk_files(source)),
            "bytes": sum(os.lstat(absolute).st_size for _, absolute in walk_files(source)),
        }
        report["ok"] = (
            not report["problems"]
            and report["counted"]["files"] == report["source_counted"]["files"]
            and report["counted"]["bytes"] == report["source_counted"]["bytes"]
        )

    elif control["mode"] == "remove":
        # Files are taken out of the relocated copy, and each removal is confirmed against the filesystem afterwards. A removal
        # that silently did nothing would turn the negative into a second positive, which is the one failure mode that would make
        # this test lie.
        removed = []
        for relative in control.get("remove", []):
            target = os.path.join(destination, relative)
            if not os.path.isfile(target):
                report["problems"].append({"why": "the file to remove is not there", "relative": relative})
                continue
            size = os.lstat(target).st_size
            os.unlink(target)
            if os.path.lexists(target):
                report["problems"].append({"why": "the file is still there after removal", "relative": relative})
                continue
            removed.append({"relative": relative, "bytes_removed": size})
        report["removed"] = removed
        report["ok"] = len(removed) == len(control.get("remove", [])) and not report["problems"]

    else:
        report["problems"].append({"why": "unknown mode", "mode": control["mode"]})
        report["ok"] = False

    write(report, control)
    return 0 if report["ok"] else 1


def write(report, control):
    path = control["report_path"]
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")


if __name__ == "__main__":
    sys.exit(main())
