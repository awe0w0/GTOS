#!/usr/bin/env python3
"""Test the guarded Content volume tool against real LittleFS assets."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test(root, tool, name, expected, diagnostic, *args, large_blocks=False):
    output = root / name
    profile = ["--large-blocks"] if large_blocks else []
    result = subprocess.run([tool, *profile, output, *args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    print(name, f"rc={result.returncode}", result.stdout, sep="\n", flush=True)
    assert result.returncode == expected and diagnostic in result.stdout
    return output


def main():
    root, tool, icu = map(Path, sys.argv[1:4])
    tool_sha = sys.argv[4]
    assert sha(tool) == tool_sha and not tool.stat().st_mode & 0o222
    asset = icu / "icudtl.dat"
    asset_sha = sha(asset)
    page = root / "index.html"
    empty = root / "empty"
    with page.open("x") as handle:
        handle.write("<!doctype html><title>GTOS</title><button>Press</button>\n")
    empty.touch(exist_ok=False)
    page_arg = f"index.html={page}"
    icu_arg = f"icudtl.dat={asset}"
    valid = test(root, tool, "assets.img", 0,
                 "CONTENT VOLUME VERIFIED assets=3 first_sector=8 sectors=32768",
                 "16", icu_arg, page_arg, f"empty={empty}")
    assert valid.stat().st_size == 4096 + 16 * 1024 * 1024
    before = sha(valid)
    large = test(root, tool, "large-blocks.img", 0,
                 "CONTENT VOLUME VERIFIED assets=3 first_sector=8 sectors=32768 block_bytes=32768",
                 "16", icu_arg, page_arg, f"empty={empty}", large_blocks=True)
    assert large.stat().st_size == valid.stat().st_size and sha(large) != before
    test(root, tool, "assets.img", 1, "cannot create fresh image", "16", page_arg)
    assert sha(valid) == before
    assert not test(root, tool, "duplicate.img", 1, "duplicate asset name",
                    "16", page_arg, f"index.html={empty}").exists()
    assert not test(root, tool, "escape.img", 1, "invalid asset name",
                    "16", f"../escape={page}").exists()
    assert not test(root, tool, "missing.img", 1, "expected a readable regular input",
                    "16", f"missing={root / 'absent'}").exists()
    assert not test(root, tool, "bad-size.img", 1, "invalid volume size",
                    "0", page_arg).exists()
    assert test(root, tool, "too-small.img", 1, "cannot write asset",
                "1", icu_arg).is_file()
    assert sha(tool) == tool_sha and sha(asset) == asset_sha

    valid.chmod(0o444)
    result = {
        "path": str(valid), "bytes": valid.stat().st_size, "sha256": before,
        "icu_sha256": asset_sha, "icu_bytes": asset.stat().st_size,
        "asset_dir": "/content", "first_sector": 8, "sector_count": 32768,
    }
    with (root / "identity.json").open("x") as handle:
        json.dump(result, handle, indent=2)
    print("CONTENT_VOLUME_TEST_PASS", json.dumps(result))


if __name__ == "__main__":
    main()
