#!/usr/bin/env python3
"""Test the real private-source snapshot/update functions, not a copy model."""
import argparse
import importlib.util
import json
import os
import unittest
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--out',required=True,type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
script = Path(__file__).resolve().parents[1]/'apps/native_content_build/gtos-platform/workspace.py'
spec = importlib.util.spec_from_file_location('workspace',script)
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)


class WorkspaceTest(unittest.TestCase):
    def view(self,name,value='before\n',tool=None,include=True):
        root = args.out/name
        root.mkdir()
        (root/'.gn').write_text('private root\n')
        source = root/'sub/source.rs'
        source.parent.mkdir()
        source.write_text(value)
        entries = [{'path':'sub/source.rs','sha256':w.candidate.digest(source)}] if include else []
        (root/'gtos-platform.json').write_text(json.dumps({'files':entries,'additions':[]}))
        composed = {'applied':[{'path':'.gn','sha256':w.candidate.digest(root/'.gn')}]}
        (root.parent/('composed-'+name+'.json')).write_text(json.dumps(composed))
        if tool is not None:
            (root/'tool').write_text(tool)
            (root/'tool').chmod(0o555)
        return root

    def capture(self,view,name,workspace=None,tools=()):
        target = args.out/name
        record = w.snapshot(view,target,workspace,tools)
        return target/'source-record.json',record

    def test_snapshot_preserves_bytes_and_refuses_reuse_or_source_drift(self):
        view = self.view('snapshot-view')
        path,record = self.capture(view,'snapshot')
        w.verify(view,record)
        saved = path.read_bytes()
        with self.assertRaises(FileExistsError):
            w.snapshot(view,path.parent)
        self.assertEqual(path.read_bytes(),saved)
        (view/'sub/source.rs').write_text('changed\n')
        with self.assertRaisesRegex(ValueError,'identity drift'):
            w.verify(view,record)
        self.assertEqual((path.parent/'payload/sub/source.rs').read_text(),'before\n')

    def test_changed_files_update_while_unchanged_mtime_and_old_bytes_survive(self):
        view = self.view('update-view')
        unchanged = view/'.gn'
        os.utime(unchanged,ns=(1234567890,1234567890))
        before,_ = self.capture(view,'update-old')
        desired = self.view('update-desired','after\n')
        following,record = self.capture(desired,'update-new',view)
        self.assertEqual(w.apply_snapshot(view,before,following),['sub/source.rs'])
        w.verify(view,record)
        self.assertEqual(unchanged.stat().st_mtime_ns,1234567890)
        self.assertEqual((view/'sub/source.rs').read_text(),'after\n')
        self.assertEqual((before.parent/'payload/sub/source.rs').read_text(),'before\n')

    def test_corrupted_snapshot_is_refused_before_any_source_write(self):
        view = self.view('corrupt-view')
        before,_ = self.capture(view,'corrupt-old')
        desired = self.view('corrupt-desired','new\n')
        following,_ = self.capture(desired,'corrupt-new',view)
        payload = following.parent/'payload/sub/source.rs'
        payload.chmod(0o644)
        payload.write_text('bad\n')
        with self.assertRaisesRegex(ValueError,'payload drift'):
            w.apply_snapshot(view,before,following)
        self.assertEqual((view/'sub/source.rs').read_text(),'before\n')

    def test_tools_and_source_removal_cannot_be_smuggled_into_an_update(self):
        view = self.view('tool-view',tool='pinned tool')
        before,_ = self.capture(view,'tool-old',tools=['tool'])
        desired = self.view('tool-desired','new\n',tool='another tool')
        following,_ = self.capture(desired,'tool-new',view,['tool'])
        with self.assertRaisesRegex(ValueError,'tool changes'):
            w.apply_snapshot(view,before,following)
        self.assertEqual((view/'sub/source.rs').read_text(),'before\n')
        desired = self.view('removed-desired',include=False,tool='pinned tool')
        following,_ = self.capture(desired,'removed-new',view,['tool'])
        with self.assertRaisesRegex(ValueError,'source removal'):
            w.apply_snapshot(view,before,following)

    def test_a_new_tool_must_be_supplied_separately_with_exact_identity(self):
        view = self.view('additional-tool-view')
        before,_ = self.capture(view,'additional-tool-old')
        desired = self.view('additional-tool-desired','after\n',tool='reviewed')
        following,record = self.capture(desired,'additional-tool-new',view,['tool'])
        tool = view/'tool'
        with self.assertRaisesRegex(ValueError,'materialized private source'):
            w.apply_snapshot(view,before,following)
        tool.write_text('different')
        tool.chmod(0o555)
        with self.assertRaisesRegex(ValueError,'identity drift'):
            w.apply_snapshot(view,before,following)
        tool.chmod(0o644)
        tool.write_text('reviewed')
        with self.assertRaisesRegex(ValueError,'identity drift'):
            w.apply_snapshot(view,before,following)
        self.assertEqual((view/'sub/source.rs').read_text(),'before\n')
        tool.chmod(0o555)
        original = tool.stat()
        self.assertEqual(w.apply_snapshot(view,before,following),['sub/source.rs'])
        self.assertEqual(tool.stat().st_ino,original.st_ino)
        self.assertEqual(tool.stat().st_mtime_ns,original.st_mtime_ns)
        w.verify(view,record)

    def test_new_overlay_materializes_a_linked_parent_without_touching_its_origin(self):
        view = self.view('linked-view',include=False)
        original = args.out/'shared-fixture'
        (view/'sub/source.rs').unlink()
        (view/'sub').rmdir()
        original.mkdir()
        (original/'source.rs').write_text('origin\n')
        (view/'sub').symlink_to(original,target_is_directory=True)
        before,_ = self.capture(view,'linked-old')
        desired = self.view('linked-desired','patched\n')
        manifest = desired/'gtos-platform.json'
        data = json.loads(manifest.read_text())
        data['files'][0]['origin_sha256'] = w.candidate.digest(original/'source.rs')
        manifest.write_text(json.dumps(data))
        following,record = self.capture(desired,'linked-new',view)
        self.assertEqual(w.apply_snapshot(view,before,following),['sub/source.rs'])
        self.assertFalse((view/'sub').is_symlink())
        self.assertEqual((original/'source.rs').read_text(),'origin\n')
        w.verify(view,record)

    def test_relative_include_directories_survive_snapshot_activation(self):
        view = self.view('layout-view')
        origin = args.out/'layout-origin'
        (origin/'nested').mkdir(parents=True)
        (origin/'source.rs').write_text('shared\n')
        (view/'sub/nested').symlink_to(origin/'nested', target_is_directory=True)
        through_parent = view/'sub/nested/../source.rs'
        self.assertEqual(through_parent.read_text(), 'shared\n')
        before,_ = self.capture(view,'layout-old')
        desired = self.view('layout-desired','private\n')
        (desired/'sub/nested').mkdir()
        receipt = desired/'gtos-platform.json'
        data = json.loads(receipt.read_text())
        data['real_directories'] = ['sub','sub/nested']
        receipt.write_text(json.dumps(data))
        following,record = self.capture(desired,'layout-new',view)
        self.assertEqual(w.apply_snapshot(view,before,following), ['sub/source.rs'])
        self.assertFalse((view/'sub/nested').is_symlink())
        self.assertEqual(through_parent.read_text(), 'private\n')
        self.assertEqual((origin/'source.rs').read_text(), 'shared\n')
        w.verify(view,record)
        (view/'sub/nested').rmdir()
        (view/'sub/nested').symlink_to(origin/'nested', target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'directory identity drift'):
            w.verify(view,record)

    def test_archive_inside_source_and_parent_traversal_are_refused(self):
        view = self.view('escape-view')
        with self.assertRaisesRegex(ValueError,'outside'):
            w.snapshot(view,view/'archive')
        _,record = self.capture(view,'escape-record')
        record['files'][0]['path'] = '../escape'
        with self.assertRaisesRegex(ValueError,'relative source path'):
            w.verify(view,record)

    def test_a_declared_host_tool_hash_is_checked_before_snapshot_creation(self):
        view = self.view('pinned-tool-view',tool='actual tool')
        target = args.out/'pinned-tool-bad'
        with self.assertRaisesRegex(ValueError,'tool identity drift'):
            w.snapshot(view,target,tools={'tool':'0'*64})
        self.assertFalse(target.exists())

    def test_cross_view_snapshot_needs_activation_and_old_versions_cannot_drop_inputs(self):
        view = self.view('version-view')
        previous,old = self.capture(view,'version-old')
        desired = self.view('version-desired')
        following,new = self.capture(desired,'version-new',view)
        with self.assertRaisesRegex(ValueError,'not been activated'):
            w.verify(view,new)
        self.assertEqual(w.apply_snapshot(view,previous,following),[])
        w.verify(view,new)
        with self.assertRaisesRegex(ValueError,'version is stale'):
            w.verify(view,old)
        with self.assertRaisesRegex(ValueError,'fresh desired source'):
            w.snapshot(view,args.out/'version-invalid-snapshot')


unittest.main(argv=['gtos_workspace_test'],verbosity=2)
