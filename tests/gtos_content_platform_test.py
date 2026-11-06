#!/usr/bin/env python3
"""Execute the patched GN/GRIT code. This is configuration, not guest proof."""
import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--gn', required=True)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
sys.path.insert(0, str(args.view / 'tools/grit'))
from grit.node.base import Node


class GritIdentityTest(unittest.TestCase):
    def test_target_identity_is_exact_and_not_host_posix(self):
        for platform in ['gtos', 'linux', 'win32', 'darwin', 'gtos-other', '']:
            with self.subTest(platform=platform):
                self.assertEqual(Node.EvaluateExpression('is_gtos', {}, platform), platform == 'gtos')
        self.assertTrue(Node.EvaluateExpression(
            'is_gtos and os == "gtos" and not is_linux and not is_posix', {}, 'gtos'))

    def test_cached_expression_does_not_cache_a_platform(self):
        for platform in ['gtos', 'linux', 'gtos', 'win32']:
            self.assertEqual(Node.EvaluateExpression('is_gtos', {}, platform), platform == 'gtos')

    def test_unknown_condition_still_refuses(self):
        with self.assertRaisesRegex(AssertionError, 'undefined Grit variable'):
            Node.EvaluateExpression('is_gtos_typo', {}, 'gtos')


class NasmConfigurationTest(unittest.TestCase):
    def configure(self, name, platform, cpu):
        root = args.out / name
        root.mkdir()

        def write(rel, text):
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

        write('.gn', 'buildconfig = "//config.gn"\n')
        # Only the surrounding GN toolchain/action infrastructure is a fixture.
        # The full NASM template itself is the actual patched production file.
        values = {
            'is_gtos': platform == 'gtos', 'is_posix': platform == 'linux',
            'is_win': platform == 'win32', 'is_mac': False, 'is_ios': False,
            'is_fuchsia': False, 'host_toolchain_is_msan': False,
        }
        config = '\n'.join('%s = %s' % (key, str(value).lower()) for key, value in values.items())
        write('config.gn', config + '\ncurrent_cpu = "' + cpu + '"\n'
              'set_default_toolchain("//:toolchain")\n')
        for rel in ['build/config/ios/config.gni', 'build/config/ios/ios_sdk_overrides.gni',
                    'build/config/sanitizers/sanitizers.gni']:
            write(rel, '')
        write('build/compiled_action.gni', '''template("compiled_action_foreach") {
  action_foreach(target_name) {
    forward_variables_from(invoker, "*", ["tool"])
    script = "//action.py"
  }
}
''')
        write('action.py', '')
        write('sample.asm', 'section .text\n')
        shutil.copyfile(args.view / 'third_party/nasm/nasm_assemble.gni', root / 'nasm.gni')
        write('BUILD.gn', '''import("//nasm.gni")
toolchain("toolchain") {
  tool("stamp") { command = "touch {{output}}" }
  tool("alink") {
    command = "ar rcs {{output}} {{inputs}}"
    outputs = [ "{{target_out_dir}}/{{target_output_name}}.a" ]
  }
}
nasm_assemble("sample") { sources = [ "sample.asm" ] }
group("default") { deps = [ ":sample" ] }
''')
        result = subprocess.run([args.gn, 'gen', str(root / 'out'), '--root=' + str(root)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / 'gen.log').write_text(result.stdout)
        return root, result

    def test_i386_uses_elf_without_linux_or_windows_defines(self):
        root, result = self.configure('gtos-x86', 'gtos', 'x86')
        self.assertEqual(result.returncode, 0, result.stdout)
        command = subprocess.check_output(
            [args.gn, 'desc', str(root / 'out'), '//:sample_action', 'args', '--format=json', '--root=' + str(root)],
            text=True)
        flags = json.loads(command)['//:sample_action']['args']
        self.assertIn('-felf32', flags)
        self.assertNotIn('-DPREFIX', flags)
        self.assertNotIn('-DPIC', flags)
        self.assertIn('--reproducible', flags)
        self.assertIn('-MD', flags)

    def test_unsupported_gtos_architecture_refuses(self):
        _, result = self.configure('gtos-x64', 'gtos', 'x64')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('The GTOS NASM target is i386', result.stdout)

    def test_existing_linux_and_windows_formats(self):
        for platform, expected in [('linux', '-felf32'), ('win32', '-fwin32')]:
            root, result = self.configure(platform, platform, 'x86')
            self.assertEqual(result.returncode, 0, result.stdout)
            command = subprocess.check_output(
                [args.gn, 'desc', str(root / 'out'), '//:sample_action', 'args', '--format=json', '--root=' + str(root)],
                text=True)
            self.assertIn(expected, json.loads(command)['//:sample_action']['args'])

    def test_root_pattern_retains_dependencies_without_unrelated_targets(self):
        root, result = self.configure('root-pattern', 'gtos', 'x86')
        self.assertEqual(result.returncode, 0, result.stdout)
        with (root / 'BUILD.gn').open('a') as handle:
            handle.write('group("unrelated") { deps = [ "//missing:target" ] }\n')
        result = subprocess.run(
            [args.gn, 'gen', str(root / 'focused'), '--root=' + str(root),
             '--root-target=//:sample', '--root-pattern=//:sample'],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)
        deps = subprocess.check_output(
            [args.gn, 'desc', str(root / 'focused'), '//:sample', 'deps', '--root=' + str(root),
             '--root-target=//:sample', '--root-pattern=//:sample'], text=True)
        self.assertIn('//:sample_action', deps)
        # A real dependency may not be hidden by the same root-pattern filter.
        text = (root / 'BUILD.gn').read_text().replace(
            'sources = [ "sample.asm" ]', 'sources = [ "sample.asm" ]\n deps = [ "//missing:target" ]')
        (root / 'BUILD.gn').write_text(text)
        result = subprocess.run(
            [args.gn, 'gen', str(root / 'missing-dependency'), '--root=' + str(root),
             '--root-target=//:sample', '--root-pattern=//:sample'],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing/BUILD.gn', result.stdout)


class CompilerRuntimeConfigurationTest(unittest.TestCase):
    def configure(self, name, cpu='x86', archive='/qualified/libscalar_integer.a', coverage=False):
        root = args.out / name
        (root / 'build/config/clang').mkdir(parents=True)
        (root / '.gn').write_text('buildconfig = "//config.gn"\n')
        values = {key: False for key in ['is_ios', 'is_mac', 'is_win', 'is_wasm',
                  'clang_use_raw_ptr_plugin', 'clang_use_chrome_plugins', 'clang_use_unsafe_buffers_plugin']}
        values.update(is_gtos=True, toolchain_has_rust=True, use_clang_coverage=coverage)
        (root / 'config.gn').write_text(
            '\n'.join('%s = %s' % (k, str(v).lower()) for k, v in values.items()) +
            '\nchrome_pgo_phase = 0\ncurrent_cpu = "' + cpu +
            '"\nclang_base_path = "//clang"\nset_default_toolchain("//:tools")\n')
        for rel in ['build/config/pch.gni', 'build/config/rust.gni', 'build/config/clang/clang.gni']:
            (root / rel).write_text('')
        shutil.copyfile(args.view / 'build/config/clang/BUILD.gn', root / 'build/config/clang/BUILD.gn')
        (root / 'BUILD.gn').write_text('''toolchain("tools") {
  tool("cxx") {
    command = "unused {{source}} {{output}}"
    outputs = [ "{{source_out_dir}}/{{source_name_part}}.o" ]
  }
  tool("stamp") { command = "unused {{output}}" }
}
source_set("sample") {
  sources = [ "sample.cc" ]
  configs = [ "//build/config/clang:compiler_builtins", "//build/config/clang:compiler_profile" ]
}
group("default") { deps = [ ":sample" ] }
''')
        (root / 'sample.cc').write_text('int value;\n')
        result = subprocess.run([args.gn, 'gen', str(root / 'out'), '--root=' + str(root),
                                 '--args=gtos_compiler_builtins="' + archive + '"'],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (root / 'gen.log').write_text(result.stdout)
        return root, result

    def test_explicit_archive_is_the_only_runtime_library(self):
        root, result = self.configure('runtime-archive')
        self.assertEqual(result.returncode, 0, result.stdout)
        output = subprocess.check_output(
            [args.gn, 'desc', str(root / 'out'), '//:sample', 'libs', '--format=json', '--root=' + str(root)], text=True)
        self.assertEqual(json.loads(output)['//:sample']['libs'], ['/qualified/libscalar_integer.a'])

    def test_missing_archive_refuses(self):
        _, result = self.configure('runtime-missing', archive='')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Set the qualified GTOS builtins archive', result.stdout)

    def test_wrong_architecture_refuses(self):
        _, result = self.configure('runtime-x64', cpu='x64')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('only provides native IA32 integer builtins', result.stdout)

    def test_unimplemented_profiling_refuses(self):
        _, result = self.configure('runtime-coverage', coverage=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('GTOS profiling runtime is not implemented', result.stdout)


class PrivatePatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / 'apps/native_content_build/gtos-platform/apply.py'
        spec = importlib.util.spec_from_file_location('platform_apply', path)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_shared_source_refuses_before_writing(self):
        with self.assertRaisesRegex(SystemExit, 'existing private'):
            self.module.apply(self.module.candidate.FULL)

    def test_generation_is_not_overwritten(self):
        before = (args.view / 'gtos-platform.json').read_bytes()
        with self.assertRaisesRegex(SystemExit, 'overwrite'):
            self.module.apply(args.view)
        self.assertEqual((args.view / 'gtos-platform.json').read_bytes(), before)

    def test_changed_input_refuses_before_modification(self):
        root = args.out / 'changed-input'
        root.mkdir()
        (root / '.gn').write_text('')
        rel = next(iter(self.module.INPUTS))
        target = root / rel
        target.parent.mkdir(parents=True)
        target.write_text('changed input\n')
        with self.assertRaises(SystemExit) as refused:
            self.module.apply(root)
        self.assertEqual(str(refused.exception), 'platform input changed: ' + rel)
        self.assertEqual(target.read_text(), 'changed input\n')
        self.assertFalse((root / 'gtos-platform.json').exists())


unittest.main(argv=[sys.argv[0]], verbosity=2)
