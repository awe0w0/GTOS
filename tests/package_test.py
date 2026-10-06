#!/usr/bin/env python3
"""Host-side protocol and bytecode game tests; no third-party dependencies."""

import copy
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gtos_package_tool", ROOT / "tools/package.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


def minimal(code=None, **overrides):
    source = {"id": "example", "title": "Example", "summary": "A test package",
              "code": [["halt"]] if code is None else code}
    source.update(overrides)
    return source


def repaired(data):
    result = bytearray(data)
    result[120:124] = b"\0" * 4
    struct.pack_into("<I", result, 120, zlib.crc32(result) & 0xFFFFFFFF)
    return bytes(result)


class AssemblerTests(unittest.TestCase):
    def test_header_layout_checksum_and_determinism(self):
        source = minimal([["movi", "r31", -123], ["halt"]])
        result = package.assemble(source)
        self.assertEqual(result, package.assemble(source))
        self.assertEqual(result[:8], b"GTAPP01\0")
        self.assertEqual(struct.unpack_from("<6I", result, 8), (1, 128, 144, 2, 0, 0))
        self.assertEqual(result[32:56], b"example" + b"\0" * 17)
        self.assertEqual(result[56:80], b"Example" + b"\0" * 17)
        self.assertEqual(result[80:120], b"A test package" + b"\0" * 26)
        self.assertEqual(result[124:128], b"\0" * 4)
        self.assertEqual(result[128:136], struct.pack("<BBBBi", 1, 31, 0, 0, -123))
        self.assertEqual(result, repaired(result))
        self.assertEqual(package.inspect_package(result)["instructions"], 2)

    def test_labels_comments_and_entry(self):
        source = minimal([
            {"comment": "Labels and comments take no space."},
            ["halt"], {"label": "start"}, ["movi", "r0", 1],
            ["jnz", "r0", "end"], ["jmp", "start"],
            {"label": "end"}, ["yield"], ["jmp", "start"],
        ], entry="start")
        info = package.inspect_package(package.assemble(source))
        self.assertEqual(info["entry"], 1)
        self.assertEqual(info["code"][2], ["jnz", "r0", 4])
        self.assertEqual(info["code"][3], ["jmp", 1])

    def test_every_opcode_roundtrips(self):
        rows = [
            ["halt"], ["movi", "r1", -(1 << 31)], ["mov", "r1", "r2"],
            *[[name, "r1", "r2", "r3"] for name in
              ("add", "sub", "mul", "mod", "and", "lt", "eq")],
            ["jmp", 0], ["jnz", "r31", 0], ["input", "r1", 3],
            ["random", "r31", 1000000], ["clear", 255],
            ["rect", "r1", "r2", "r3", "r31", 255],
            ["text", "r1", "r2", 1], ["number", "r1", "r2", "r3", 255],
            ["yield"],
        ]
        self.assertEqual(package.inspect_package(package.assemble(minimal(rows)))["code"], rows)

    def test_maximum_package_size(self):
        source = minimal([["halt"]] * package.MAX_INSTRUCTIONS)
        self.assertEqual(len(package.assemble(source)), 8192)
        with self.assertRaises(package.PackageError):
            package.assemble(minimal(source["code"] + [["halt"]]))

    def test_empty_code_rejected(self):
        with self.assertRaises(package.PackageError):
            package.assemble(minimal([]))

    def test_invalid_manifest_rejected(self):
        for overrides in ({"id": ""}, {"id": "Upper"}, {"id": "a/b"},
                          {"id": "a" * 24}, {"id": True}, {"title": ""},
                          {"title": "a" * 24}, {"title": "bad\0title"},
                          {"title": "bad\ntitle"}, {"title": "caf\u00e9"},
                          {"summary": "x" * 40}, {"summary": 4},
                          {"extra": "unknown"}):
            with self.subTest(overrides=overrides), self.assertRaises(package.PackageError):
                package.assemble(minimal(**overrides))

    def test_longest_manifest_and_empty_summary(self):
        data = package.assemble(minimal(id="a" * 23, title="B" * 23, summary=""))
        self.assertEqual(package.inspect_package(data)["summary"], "")
        data = package.assemble(minimal(summary="c" * 39))
        self.assertEqual(package.inspect_package(data)["summary"], "c" * 39)

    def test_missing_and_wrong_source_fields(self):
        for source in ([], None, {}, {"id": "x"}, minimal(code="halt")):
            with self.subTest(source=source), self.assertRaises(package.PackageError):
                package.assemble(source)

    def test_bad_operands_rejected(self):
        rows = [["oops"], ["halt", 1], [], "halt", [1],
                ["movi", "r32", 1], ["movi", "r-1", 1], ["movi", 0, 1],
                ["movi", "r0", True], ["movi", "r0", 1.5],
                ["movi", "r0", 1 << 31], ["movi", "r0", -(1 << 31) - 1],
                ["jmp", -1], ["jmp", 1], ["jmp", "missing"],
                ["input", "r0", 4], ["random", "r0", 0],
                ["random", "r0", 1000001], ["clear", -1], ["clear", 256],
                ["rect", "r0", "r0", "r0", "r32", 1],
                ["text", "r0", "r1", 2], ["number", "r0", "r1", "r2", 256]]
        for row in rows:
            with self.subTest(row=row), self.assertRaises(package.PackageError):
                package.assemble(minimal([row]))

    def test_invalid_labels_rejected(self):
        sources = [
            minimal([{"label": "x"}, {"label": "x"}, ["halt"]]),
            minimal([{"label": "bad name"}, ["halt"]]),
            minimal([{"label": 1}, ["halt"]]),
            minimal([{"label": "x", "comment": "bad"}, ["halt"]]),
            minimal([{"comment": 1}, ["halt"]]),
            minimal([["jmp", "end"], {"label": "end"}]),
            minimal([["halt"], {"label": "end"}], entry="end"),
            minimal(entry=True), minimal(entry="missing"),
        ]
        for source in sources:
            with self.subTest(source=source), self.assertRaises(package.PackageError):
                package.assemble(source)

    def test_duplicate_json_keys_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            path.write_text('{"id":"a","id":"b"}', encoding="utf-8")
            with self.assertRaisesRegex(package.PackageError, "duplicate JSON key"):
                package.load_source(path)

    def test_input_is_not_mutated(self):
        source = minimal([["movi", "r0", 1], ["yield"], ["jmp", 0]])
        snapshot = copy.deepcopy(source)
        package.assemble(source)
        self.assertEqual(source, snapshot)


class PackageValidationTests(unittest.TestCase):
    def setUp(self):
        self.valid = package.assemble(minimal([["movi", "r0", 1], ["yield"], ["jmp", 0]]))

    def test_truncated_extended_and_oversized_packages_rejected(self):
        for data in (b"", self.valid[:128], self.valid[:-1], self.valid + b"\0", b"x" * 8193):
            with self.subTest(size=len(data)), self.assertRaises(package.PackageError):
                package.inspect_package(data)

    def test_corruption_rejected(self):
        for offset in (0, 8, 32, 56, 80, 120, 132):
            data = bytearray(self.valid)
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(package.PackageError):
                package.inspect_package(data)

    def test_forged_header_rejected_even_with_valid_crc(self):
        for offset, value in ((8, 2), (12, 120), (16, len(self.valid) + 8),
                              (20, 4), (20, 0), (24, 3), (28, 1), (124, 1)):
            data = bytearray(self.valid)
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(package.PackageError):
                package.inspect_package(repaired(data))

    def test_bad_binary_manifest_rejected(self):
        for start, content in ((32, b"A"), (32, b"x" * 24), (40, b"x"),
                               (56, b"\x01"), (56, b"x" * 24), (80, b"\xff")):
            data = bytearray(self.valid)
            data[start:start + len(content)] = content
            with self.subTest(start=start, content=content), self.assertRaises(package.PackageError):
                package.inspect_package(repaired(data))

    def test_invalid_instruction_payload_rejected(self):
        rows = [(255, 0, 0, 0, 0), (1, 32, 0, 0, 1), (1, 0, 1, 0, 1),
                (10, 0, 0, 0, -1), (10, 0, 0, 0, 3), (12, 0, 0, 0, 4),
                (13, 0, 0, 0, 0), (14, 0, 0, 0, 256),
                (15, 0, 0, 0, 32), (15, 0, 0, 0, 65536),
                (16, 0, 0, 0, 2), (18, 1, 0, 0, 0)]
        for row in rows:
            data = bytearray(self.valid)
            package.INSTRUCTION.pack_into(data, 128, *row)
            with self.subTest(row=row), self.assertRaises(package.PackageError):
                package.inspect_package(repaired(data))

    def test_cli_build_inspect_and_disassemble(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "demo.json"
            output = Path(directory) / "demo.gtapp"
            source.write_text(json.dumps(minimal()), encoding="utf-8")
            build = subprocess.run([sys.executable, str(ROOT / "tools/package.py"),
                                    "build", str(source), str(output)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([sys.executable, str(ROOT / "tools/package.py"),
                                     "inspect", str(output), "--disassemble"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["code"], [["halt"]])
            self.assertEqual(list(Path(directory).glob(".demo.gtapp.*")), [])

    def test_failed_build_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "bad.json", Path(directory) / "old.gtapp"
            source.write_text("not JSON", encoding="utf-8")
            output.write_bytes(b"previous package")
            result = subprocess.run([sys.executable, str(ROOT / "tools/package.py"),
                                     "build", str(source), str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("package:", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(output.read_bytes(), b"previous package")

    def test_build_cannot_overwrite_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "demo.json"
            original = json.dumps(minimal())
            path.write_text(original, encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "tools/package.py"),
                                     "build", str(path), str(path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_text(encoding="utf-8"), original)


class ReferenceMachine:
    """Tiny independent test interpreter for ABI and game behavior assertions.

    The actual guest executes C++ VirtualMachine, not this test helper. This
    interpreter contains no Catch-specific behavior; it executes package bytes.
    """
    def __init__(self, binary):
        metadata = package.inspect_package(binary)
        self.code = [package.INSTRUCTION.unpack_from(binary, 128 + index * 8)
                     for index in range(metadata["instructions"])]
        self.pc = metadata["entry"]
        self.regs = [0] * 32
        self.draws = []
        self.seed = 1
        self.maximum_step = 0

    @staticmethod
    def signed(value):
        return (value + (1 << 31)) % (1 << 32) - (1 << 31)

    def step(self, keys=0, ticks=0):
        r = self.regs
        for instruction_number in range(1, 2049):
            if not 0 <= self.pc < len(self.code):
                raise RuntimeError("instruction pointer out of bounds")
            op, a, b, c, imm = self.code[self.pc]
            self.pc += 1
            if op == 0:
                return False
            elif op == 1:
                r[a] = imm
            elif op == 2:
                r[a] = r[b]
            elif op == 3:
                r[a] = self.signed(r[b] + r[c])
            elif op == 4:
                r[a] = self.signed(r[b] - r[c])
            elif op == 5:
                r[a] = self.signed(r[b] * r[c])
            elif op == 6:
                if not r[c]:
                    raise RuntimeError("remainder by zero")
                r[a] = (abs(r[b]) % abs(r[c])) * (-1 if r[b] < 0 else 1)
            elif op == 7:
                r[a] = self.signed(r[b] & r[c])
            elif op == 8:
                r[a] = int(r[b] < r[c])
            elif op == 9:
                r[a] = int(r[b] == r[c])
            elif op == 10:
                self.pc = imm
            elif op == 11:
                if r[a]:
                    self.pc = imm
            elif op == 12:
                r[a] = self.signed((keys, ticks, 272, 128)[imm])
            elif op == 13:
                self.seed = (1664525 * self.seed + 1013904223) & 0xFFFFFFFF
                r[a] = self.seed % imm
            elif op == 14:
                self.draws.append(("clear", imm))
            elif op == 15:
                self.draws.append(("rect", r[a], r[b], r[c], r[imm & 255], (imm >> 8) & 255))
            elif op == 16:
                self.draws.append(("text", r[a], r[b], imm))
            elif op == 17:
                self.draws.append(("number", r[a], r[b], r[c], imm))
            elif op == 18:
                self.maximum_step = max(self.maximum_step, instruction_number)
                return True
            else:
                raise RuntimeError("unknown opcode")
        raise RuntimeError("instruction budget exceeded")


class CatchGameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = package.assemble(package.load_source(ROOT / "apps/catch.json"))

    def setUp(self):
        self.vm = ReferenceMachine(self.binary)
        self.assertTrue(self.vm.step())

    def test_shipped_package_is_reproducible(self):
        self.assertEqual((ROOT / "apps/catch.gtapp").read_bytes(), self.binary)
        self.assertEqual(package.inspect_package(self.binary)["id"], "catch")

    def test_initial_game_renders_without_host_game_logic(self):
        self.assertEqual(self.vm.regs[0], 120)
        self.assertEqual(self.vm.regs[2], 24)
        self.assertEqual(self.vm.regs[3], 0)
        self.assertIn(("rect", 120, 105, 32, 5, 8), self.vm.draws)
        self.assertIn(("text", 8, 117, 1), self.vm.draws)
        self.assertIn(("number", 232, 7, 0, 15), self.vm.draws)

    def test_tick_gate_prevents_cpu_dependent_speed(self):
        initial = self.vm.regs[2]
        self.vm.step(ticks=4)
        self.assertEqual(self.vm.regs[2], initial)
        self.vm.step(ticks=5)
        self.assertEqual(self.vm.regs[2], initial + 3)
        self.vm.step(ticks=5)
        self.assertEqual(self.vm.regs[2], initial + 3)
        self.vm.step(ticks=100)
        self.assertEqual(self.vm.regs[2], initial + 6)

    def test_tick_wraparound(self):
        self.vm.regs[6] = 0x7FFFFFFD
        self.vm.step(ticks=0x80000002)
        self.assertEqual(self.vm.regs[2], 27)
        self.vm.regs[6] = -3
        self.vm.step(ticks=2)
        self.assertEqual(self.vm.regs[2], 30)

    def test_movement_both_keys_and_boundaries(self):
        self.vm.step(keys=1, ticks=5)
        self.assertEqual(self.vm.regs[0], 110)
        self.vm.step(keys=2, ticks=10)
        self.assertEqual(self.vm.regs[0], 120)
        self.vm.step(keys=3, ticks=15)
        self.assertEqual(self.vm.regs[0], 120)
        self.vm.regs[0] = 2
        self.vm.step(keys=1, ticks=20)
        self.assertEqual(self.vm.regs[0], 0)
        self.vm.regs[0] = 238
        self.vm.step(keys=2, ticks=25)
        self.assertEqual(self.vm.regs[0], 240)

    def test_catch_increases_score_and_speed_then_respawns(self):
        self.vm.regs[0:4] = [100, 108, 96, 7]
        self.vm.step(ticks=5)
        self.assertEqual(self.vm.regs[3], 8)
        self.assertEqual(self.vm.regs[15], 4)
        self.assertEqual(self.vm.regs[2], 24)
        self.assertTrue(0 <= self.vm.regs[1] <= 266)

    def test_speed_is_capped(self):
        self.vm.regs[0:4] = [100, 108, 96, 7]
        self.vm.regs[15] = 6
        self.vm.step(ticks=5)
        self.assertEqual(self.vm.regs[15], 6)
        self.assertEqual(self.vm.regs[3], 8)

    def test_miss_resets_score_and_speed(self):
        self.vm.regs[0:4] = [100, 200, 96, 7]
        self.vm.regs[15] = 6
        self.vm.step(ticks=5)
        self.assertEqual(self.vm.regs[3], 0)
        self.assertEqual(self.vm.regs[15], 3)
        self.assertEqual(self.vm.regs[2], 24)

    def test_collision_requires_pixel_overlap(self):
        for ball_x, expected in ((94, 0), (95, 1), (131, 1), (132, 0)):
            with self.subTest(ball_x=ball_x):
                vm = ReferenceMachine(self.binary)
                vm.step()
                vm.regs[0:4] = [100, ball_x, 96, 0]
                vm.step(ticks=5)
                self.assertEqual(vm.regs[3], expected)

    def test_action_restart_is_edge_triggered(self):
        self.vm.regs[0:4] = [30, 100, 70, 9]
        self.vm.step(keys=16, ticks=5)
        self.assertEqual(self.vm.regs[0], 120)
        self.assertEqual(self.vm.regs[2], 24)
        self.assertEqual(self.vm.regs[3], 0)
        self.vm.regs[3] = 4
        self.vm.step(keys=16, ticks=10)
        self.assertEqual(self.vm.regs[3], 4)
        self.assertEqual(self.vm.regs[2], 27)
        self.vm.step(keys=0, ticks=15)
        self.vm.step(keys=16, ticks=20)
        self.assertEqual(self.vm.regs[3], 0)
        self.assertEqual(self.vm.regs[2], 24)

    def test_long_run_yields_within_budget_and_stays_on_canvas(self):
        for ticks in range(1, 5001):
            keys = 1 if (ticks // 50) % 2 else 2
            if ticks % 777 == 0:
                keys |= 16
            self.assertTrue(self.vm.step(keys=keys, ticks=ticks))
            self.assertTrue(0 <= self.vm.regs[0] <= 240)
            self.assertTrue(0 <= self.vm.regs[1] <= 266)
            self.assertTrue(24 <= self.vm.regs[2] < 105)
        self.assertLess(self.vm.maximum_step, 128)


if __name__ == "__main__":
    unittest.main()
