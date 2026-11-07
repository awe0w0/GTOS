#!/usr/bin/env python3
"""Exercise the pinned Content generators against GTOS and host controls."""

import importlib.util
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ResourceGeneratorsTest(unittest.TestCase):
    def test_download_policy_prefers_exact_gtos_then_any_then_default(self):
        platforms = self.safe_browsing.PlatformTypes()
        default = self.proto.DownloadFileType.PlatformSettings(auto_open_hint=1)
        for platform, expected in [('gtos', 1), ('linux', 0), ('mac', 0)]:
            file_type = self.proto.DownloadFileType(extension='example')
            file_type.platform_settings.add(platform=0, auto_open_hint=0)
            file_type.platform_settings.add(platform=platforms['gtos'],
                                             auto_open_hint=1)
            self.safe_browsing.PrunePlatformSettings(
                file_type, default, platforms[platform])
            self.assertEqual(len(file_type.platform_settings), 1)
            self.assertEqual(file_type.platform_settings[0].auto_open_hint,
                             expected)
            self.assertFalse(file_type.platform_settings[0].HasField('platform'))
        file_type = self.proto.DownloadFileType(extension='example')
        file_type.platform_settings.add(platform=platforms['linux'],
                                         auto_open_hint=0)
        self.safe_browsing.PrunePlatformSettings(
            file_type, default, platforms['gtos'])
        self.assertEqual(file_type.platform_settings[0].auto_open_hint, 1)

    def test_download_policy_all_platforms_writes_gtos_resource(self):
        from google.protobuf import text_format

        source = Path(self.safe_browsing.__file__).with_name(
            'download_file_types.asciipb')
        config = text_format.Parse(source.read_text(),
                                   self.proto.DownloadFileTypeConfig())
        with tempfile.TemporaryDirectory() as directory:
            opts = SimpleNamespace(type=None, outdir=directory,
                                   outbasename='download_file_types.pb')
            self.safe_browsing.DownloadFileTypeProtoGenerator().ProcessPb(
                opts, config)
            root = Path(directory)
            self.assertEqual({path.name for path in root.iterdir()},
                             set(self.safe_browsing.PlatformTypes()))
            native = self.proto.DownloadFileTypeConfig.FromString(
                (root / 'gtos' / opts.outbasename).read_bytes())
            self.assertEqual(native.version_id, config.version_id)
            self.assertEqual(len(native.file_types), len(config.file_types))
            self.assertGreater(len(native.file_types), 0)

    def test_variations_uses_only_the_requested_platform(self):
        config = {'Trial': [
            {'platforms': ['gtos'], 'experiments': [{'name': 'Native'}]},
            {'platforms': ['linux'], 'experiments': [{'name': 'Linux'}]},
        ]}
        for platform, group in [('gtos', 'Native'), ('linux', 'Linux')]:
            trial, = self.variations.ConfigToStudies(config, [platform])
            experiment, = trial['experiments']
            self.assertEqual(experiment['name'], group)
            self.assertEqual(experiment['platforms'],
                             ['Study::PLATFORM_' + platform.upper()])
        self.assertEqual(self.variations.ConfigToStudies(config, ['mac']), [])

    def test_cloud_policy_import_tracks_emitted_fields(self):
        common = 'import "policy_common_definitions.proto";'

        def generate(policies):
            output = io.StringIO()
            self.policy._WriteCloudPolicyProtobuf(
                policies, [], 'gtos', output, None, True, False)
            return output.getvalue()

        ignored = SimpleNamespace(is_supported=True, is_device_only=True)
        self.assertNotIn(common, generate([ignored]))
        for field_id in [10, 2000]:
            policy = SimpleNamespace(is_supported=True, is_device_only=False,
                                     id=field_id, policy_protobuf_type='Boolean',
                                     name='Example')
            generated = generate([ignored, policy])
            self.assertEqual(generated.count(common), 1)
            self.assertIn('optional BooleanPolicyProto Example', generated)
            if field_id == 2000:
                self.assertIn('message CloudPolicySubProto2 {', generated)
                self.assertIn('optional CloudPolicySubProto2 subProto2',
                              generated)


if __name__ == '__main__':
    view, build = (Path(arg).resolve() for arg in sys.argv[1:])
    sys.path[:0] = [str(build / 'pyproto'), str(
        build / 'pyproto/components/safe_browsing/content/common/proto')]
    ResourceGeneratorsTest.safe_browsing = load(
        'gen_file_type_proto',
        view / 'components/safe_browsing/content/resources/gen_file_type_proto.py')
    ResourceGeneratorsTest.safe_browsing.DownloadFileTypeProtoGenerator().ImportProtoModule()
    ResourceGeneratorsTest.proto = ResourceGeneratorsTest.safe_browsing.download_file_types_pb2
    ResourceGeneratorsTest.variations = load(
        'fieldtrial_to_struct', view / 'tools/variations/fieldtrial_to_struct.py')
    ResourceGeneratorsTest.policy = load(
        'generate_policy_source',
        view / 'components/policy/tools/generate_policy_source.py')
    unittest.main(argv=[sys.argv[0]], verbosity=2)
