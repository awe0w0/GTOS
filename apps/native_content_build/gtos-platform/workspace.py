#!/usr/bin/env python3
"""Snapshot and update an incremental private checkout without changing old views."""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import stat
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('candidate',HERE.parent/'port-candidate-r20/candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


def relative(value):
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or '..' in path.parts or str(path)!=value:
        raise ValueError('invalid relative source path: '+value)
    return Path(value)


def private_file(view,rel):
    path = view/relative(rel)
    if path.is_symlink() or not path.is_file() or view not in path.resolve().parents:
        raise ValueError('expected a materialized private source: '+rel)
    return path


def verify_layout(view, directories):
    for rel in directories:
        path = view / relative(rel)
        if not path.is_dir() or path.resolve() != path:
            raise ValueError('workspace directory identity drift: ' + rel)


def snapshot(view,destination,workspace=None,tools=()):
    view = view.resolve()
    workspace = (workspace or view).resolve()
    if candidate.under_shared(view) or candidate.under_shared(workspace):
        raise ValueError('shared sources are not a private workspace')
    if (view/'.codex-workspace/current.json').exists():
        raise ValueError('use a fresh desired source view for further snapshots')
    if (candidate.under_shared(destination.resolve()) or destination.resolve()==view or
            view in destination.resolve().parents):
        raise ValueError('snapshot must be outside the source workspace/shared origins')
    if destination.exists() or destination.is_symlink():
        raise FileExistsError('snapshot already exists: '+str(destination))
    platform = json.loads((view/'gtos-platform.json').read_text())
    directories = platform.get('real_directories', [])
    verify_layout(view, directories)
    composed_path = view.parent/('composed-'+view.name+'.json')
    composed = json.loads(composed_path.read_text())
    entries = {item['path']:dict(item) for item in composed['applied']}
    entries.update({item['path']:dict(item) for item in platform['files']+platform['additions']})
    for rel in tools:
        path = private_file(view,rel)
        if isinstance(tools,dict) and candidate.digest(path)!=tools[rel]:
            raise ValueError('host tool identity drift before snapshot: '+rel)
        entries[rel] = {'path':rel,'sha256':candidate.digest(path),'tool':True}
    for rel,item in entries.items():
        path = private_file(view,rel)
        if candidate.digest(path)!=item['sha256']:
            raise ValueError('source identity drift before snapshot: '+rel)
    destination.mkdir(parents=True,exist_ok=False)
    files = []
    for rel,item in sorted(entries.items()):
        path = private_file(view,rel)
        entry = {'path':rel,'sha256':item['sha256'],'bytes':path.stat().st_size,
                 'mode':stat.S_IMODE(path.stat().st_mode),'tool':bool(item.get('tool'))}
        if 'origin_sha256' in item:
            entry['origin_sha256'] = item['origin_sha256']
        if not entry['tool']:
            archive = destination/'payload'/relative(rel)
            archive.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,archive)
            archive.chmod(0o444)
            if candidate.digest(archive)!=entry['sha256'] or candidate.digest(path)!=entry['sha256']:
                raise ValueError('source changed during snapshot: '+rel)
        files.append(entry)
    for name,path in [('platform-receipt.json',view/'gtos-platform.json'),('composition.json',composed_path)]:
        shutil.copyfile(path,destination/name)
        (destination/name).chmod(0o444)
    record = {'workspace':str(workspace),'source_view':str(view),'files':files,
              'scope':'materialized GTOS overlays and pinned host tools; shared origins stay read-only'}
    if directories:
        record['real_directories'] = directories
    with (destination/'source-record.json').open('x') as handle:
        json.dump(record,handle,indent=2)
        handle.write('\n')
    (destination/'source-record.json').chmod(0o444)
    return record


def version(record):
    return hashlib.sha256(json.dumps(record,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def _verify_files(view,record):
    view = view.resolve()
    if candidate.under_shared(view) or str(view)!=record['workspace']:
        raise ValueError('source record names another workspace')
    verify_layout(view, record.get('real_directories', []))
    seen = set()
    for entry in record['files']:
        rel = entry['path']
        if rel in seen:
            raise ValueError('duplicate source record: '+rel)
        seen.add(rel)
        path = private_file(view,rel)
        if (path.stat().st_size!=entry['bytes'] or candidate.digest(path)!=entry['sha256'] or
                stat.S_IMODE(path.stat().st_mode)!=entry['mode']):
            raise ValueError('workspace source identity drift: '+rel)


def verify(view,record):
    view = view.resolve()
    _verify_files(view,record)
    state = view/'.codex-workspace/current.json'
    if state.exists() or state.is_symlink():
        active = json.loads(private_file(view,'.codex-workspace/current.json').read_text())
        if active['version']!=version(record):
            raise ValueError('workspace source version is stale')
    elif record['source_view']!=record['workspace']:
        raise ValueError('source snapshot has not been activated')


def apply_snapshot(view,previous,following):
    view = view.resolve()
    old = json.loads(previous.read_text())
    new = json.loads(following.read_text())
    verify(view,old)
    if new['workspace']!=str(view):
        raise ValueError('snapshot names another workspace')
    old_files = {entry['path']:entry for entry in old['files']}
    new_files = {entry['path']:entry for entry in new['files']}
    if len(new_files)!=len(new['files']) or not old_files.keys()<=new_files.keys():
        raise ValueError('source removal/duplicate requires a fresh workspace')
    if {key:item for key,item in old_files.items() if item['tool']} != {key:item for key,item in new_files.items() if item['tool']}:
        raise ValueError('host tool changes require a fresh workspace')
    old_dirs = set(old.get('real_directories', []))
    new_dirs = set(new.get('real_directories', []))
    if len(new_dirs) != len(new.get('real_directories', [])) or not old_dirs <= new_dirs:
        raise ValueError('source directory removal/duplicate requires a fresh workspace')
    for rel in new_dirs - old_dirs:
        if not (view / relative(rel)).is_dir():
            raise ValueError('new source directory is missing: ' + rel)
    for rel,entry in new_files.items():
        relative(rel)
        if entry['tool']:
            continue
        payload = private_file(following.parent/'payload',rel)
        if payload.stat().st_size!=entry['bytes'] or candidate.digest(payload)!=entry['sha256']:
            raise ValueError('snapshot payload drift: '+rel)
        target = view/rel
        if rel not in old_files and (target.exists() or target.is_symlink()):
            if candidate.digest(target)!=entry.get('origin_sha256'):
                raise ValueError('new overlay origin changed: '+rel)
    metadata = view/'.codex-workspace'
    versions = metadata/'versions'
    for directory in [metadata,versions]:
        if directory.is_symlink() or (directory.exists() and not directory.is_dir()):
            raise ValueError('workspace version metadata is not a private directory')
    receipt = versions/(version(new)+'.json')
    if receipt.exists() or receipt.is_symlink():
        raise FileExistsError('source version receipt already exists')
    versions.mkdir(parents=True,exist_ok=True)
    for rel in sorted(new_dirs - old_dirs, key=lambda path: (path.count('/'), path)):
        candidate.materialize_parents(view, rel + '/.gtos-layout')
    changed = []
    for rel,entry in new_files.items():
        if entry['tool']:
            continue
        prior = old_files.get(rel)
        if prior and all(prior[key]==entry[key] for key in ['sha256','bytes','mode']):
            continue
        parent = candidate.materialize_parents(view,rel)
        if parent.resolve()!=view and view not in parent.resolve().parents:
            raise ValueError('workspace parent escaped: '+rel)
        target = parent/Path(rel).name
        temporary = target.with_name('.gtos-new-'+target.name)
        with temporary.open('xb') as handle:
            handle.write((following.parent/'payload'/relative(rel)).read_bytes())
        temporary.chmod(entry['mode'])
        os.replace(temporary,target)
        changed.append(rel)
    _verify_files(view,new)
    with receipt.open('x') as handle:
        json.dump({'previous':version(old),'version':version(new),'record':str(following.resolve()),
                   'changed':changed},handle,indent=2)
    temporary = metadata/'.current-new'
    with temporary.open('x') as handle:
        json.dump({'version':version(new),'receipt':str(receipt)},handle)
    os.replace(temporary,metadata/'current.json')
    verify(view,new)
    return changed


if __name__=='__main__':
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command',required=True)
    create = commands.add_parser('snapshot')
    create.add_argument('--view',required=True,type=Path)
    create.add_argument('--out',required=True,type=Path)
    create.add_argument('--workspace',type=Path)
    create.add_argument('--tools',default='[]')
    check = commands.add_parser('verify')
    check.add_argument('--view',required=True,type=Path)
    check.add_argument('--record',required=True,type=Path)
    update = commands.add_parser('apply')
    update.add_argument('--view',required=True,type=Path)
    update.add_argument('--previous',required=True,type=Path)
    update.add_argument('--following',required=True,type=Path)
    args = parser.parse_args()
    if args.command=='snapshot':
        result = snapshot(args.view,args.out,args.workspace,json.loads(args.tools))
        print('WORKSPACE_SNAPSHOT',len(result['files']),args.out/'source-record.json')
    elif args.command=='verify':
        verify(args.view,json.loads(args.record.read_text()))
        print('WORKSPACE_SOURCE_VERIFIED',args.record)
    else:
        print('WORKSPACE_UPDATED',json.dumps(apply_snapshot(args.view,args.previous,args.following)))
