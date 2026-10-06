#!/usr/bin/env python3
"""Create/inspect dedicated GTOS app-store images. Never formats existing files."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import stat
import struct
import sys
import zlib

from package import inspect_package, PackageError

SECTOR = 512
SLOTS = 16
SLOT_SECTORS = 16
MAX_APPS = 8
DATA_START = 3
TOTAL_SECTORS = DATA_START + SLOTS * SLOT_SECTORS
SUPER_MAGIC = b"GTSTOR1\0"
DIR_MAGIC = b"GTDIR01\0"


class StoreError(ValueError):
    pass


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def superblock():
    data = bytearray(SECTOR)
    data[:8] = SUPER_MAGIC
    struct.pack_into('<7I', data, 8, 1, SECTOR, TOTAL_SECTORS, SLOTS,
                     SLOT_SECTORS, MAX_APPS, DATA_START)
    struct.pack_into('<I', data, 508, crc(data[:508]))
    return bytes(data)


def directory(generation=0, entries=()):
    if len(entries) > MAX_APPS:
        raise StoreError('store is full')
    data = bytearray(SECTOR)
    data[:8] = DIR_MAGIC
    struct.pack_into('<II', data, 8, generation & 0xffffffff, len(entries))
    for index, entry in enumerate(entries):
        offset = 24 + index * 60
        struct.pack_into('<24s24sIII', data, offset, entry['id'].encode('ascii'),
                         entry['title'].encode('ascii'), entry['slot'],
                         entry['bytes'], entry['checksum'])
    struct.pack_into('<I', data, 508, crc(data[:508]))
    return bytes(data)


def decode_directory(data):
    if len(data) != SECTOR or data[:8] != DIR_MAGIC:
        raise StoreError('bad directory magic')
    generation, count = struct.unpack_from('<II', data, 8)
    if count > MAX_APPS or any(data[16:24]) or any(data[504:508]):
        raise StoreError('invalid directory header')
    if struct.unpack_from('<I', data, 508)[0] != crc(data[:508]):
        raise StoreError('directory checksum mismatch')
    entries, used, ids = [], set(), set()
    for index in range(MAX_APPS):
        offset = 24 + index * 60
        raw = data[offset:offset + 60]
        if index >= count:
            if any(raw):
                raise StoreError('nonzero unused directory entry')
            continue
        id_bytes, title_bytes, slot, size, checksum = struct.unpack('<24s24sIII', raw)
        from package import decode_field, ID_RE
        app_id = decode_field(id_bytes, 0, 24, 'id', True)
        title = decode_field(title_bytes, 0, 24, 'title', True)
        if not ID_RE.fullmatch(app_id) or app_id in ids:
            raise StoreError('invalid or repeated app id')
        if slot >= SLOTS or slot in used or not 136 <= size <= 8192 or (size - 128) % 8:
            raise StoreError('invalid package extent')
        ids.add(app_id)
        used.add(slot)
        entries.append(dict(id=app_id, title=title, slot=slot, bytes=size, checksum=checksum))
    return generation, entries


def create(path, size_mib=8):
    if not 1 <= size_mib <= 1024:
        raise StoreError('size must be 1..1024 MiB')
    # Exclusive create rejects existing files, symlinks, devices, and live images.
    with open(path, 'xb') as stream:
        stream.truncate(size_mib * 1024 * 1024)
        stream.write(superblock())
        stream.write(directory())
        stream.write(directory())
        stream.flush()
        os.fsync(stream.fileno())


class Image:
    def __init__(self, path, writable=False):
        flags = (os.O_RDWR if writable else os.O_RDONLY) | getattr(os, 'O_NOFOLLOW', 0)
        fd = os.open(path, flags)
        try:
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode):
                raise StoreError('only regular image files are supported; devices are refused')
            if st.st_size < TOTAL_SECTORS * SECTOR:
                raise StoreError('image is too small')
            fcntl.flock(fd, (fcntl.LOCK_EX if writable else fcntl.LOCK_SH) | fcntl.LOCK_NB)
            self.stream = os.fdopen(fd, 'r+b' if writable else 'rb')
            fd = None
            if self.read_sector(0) != superblock():
                raise StoreError('not a valid GTOS app-store image')
            self.dirs, self.decoded = [], []
            for sector in (1, 2):
                data = self.read_sector(sector)
                self.dirs.append(data)
                try:
                    self.decoded.append(decode_directory(data))
                except (StoreError, PackageError):
                    self.decoded.append(None)
            if not any(self.decoded):
                raise StoreError('both directory snapshots are corrupt')
            if self.decoded[0] and self.decoded[1]:
                g0, g1 = self.decoded[0][0], self.decoded[1][0]
                if ((g1-g0) & 0xffffffff) == 0x80000000 or (g0 == g1 and self.dirs[0] != self.dirs[1]):
                    raise StoreError('ambiguous directory generations')
                self.active = int(0 < ((g1-g0) & 0xffffffff) < 0x80000000)
            else:
                self.active = 0 if self.decoded[0] else 1
            self.generation, self.entries = self.decoded[self.active]
        except BaseException:
            if fd is not None:
                os.close(fd)
            elif hasattr(self, 'stream'):
                self.stream.close()
            raise

    def close(self):
        self.stream.close()

    def read_sector(self, index):
        if not 0 <= index < TOTAL_SECTORS:
            raise StoreError('sector outside dedicated store')
        self.stream.seek(index * SECTOR)
        data = self.stream.read(SECTOR)
        if len(data) != SECTOR:
            raise StoreError('short image read')
        return data

    def write_sector(self, index, data):
        if not 0 < index < TOTAL_SECTORS or len(data) != SECTOR:
            raise StoreError('invalid write extent')
        self.stream.seek(index * SECTOR)
        if self.stream.write(data) != SECTOR:
            raise StoreError('short image write')

    def sync(self):
        self.stream.flush()
        os.fsync(self.stream.fileno())

    def commit(self, entries):
        # Readback after a failed fsync is not proof of durability. Preserve the
        # recovered head before reusing its companion directory sector.
        self.sync()
        target = 1 - self.active
        data = directory(self.generation + 1, entries)
        self.write_sector(target + 1, data)
        self.sync()
        if self.read_sector(target + 1) != data:
            raise StoreError('directory readback failed')
        self.dirs[target] = data
        self.decoded[target] = decode_directory(data)
        self.active = target
        self.generation, self.entries = self.decoded[target]

    def read(self, app_id):
        entry = next((e for e in self.entries if e['id'] == app_id), None)
        if entry is None:
            raise StoreError('app is not installed')
        data = b''.join(self.read_sector(DATA_START + entry['slot'] * SLOT_SECTORS + n)
                        for n in range(SLOT_SECTORS))[:entry['bytes']]
        if crc(data) != entry['checksum']:
            raise StoreError('stored package checksum mismatch')
        metadata = inspect_package(data)
        if metadata['id'] != entry['id'] or metadata['title'] != entry['title']:
            raise StoreError('package manifest disagrees with directory')
        return data

    def install(self, data):
        metadata = inspect_package(data)
        index = next((i for i, e in enumerate(self.entries) if e['id'] == metadata['id']),
                     len(self.entries))
        if index == len(self.entries) == MAX_APPS:
            raise StoreError('store is full (8 apps)')
        used = {e['slot'] for snapshot in self.decoded if snapshot for e in snapshot[1]}
        slot = next((n for n in range(SLOTS) if n not in used), None)
        if slot is None:
            raise StoreError('no unreferenced payload slot')
        payload = data.ljust(SLOT_SECTORS * SECTOR, b'\0')
        for n in range(SLOT_SECTORS):
            self.write_sector(DATA_START + slot * SLOT_SECTORS + n, payload[n*SECTOR:(n+1)*SECTOR])
        self.sync()
        readback = b''.join(self.read_sector(DATA_START + slot * SLOT_SECTORS + n)
                            for n in range(SLOT_SECTORS))
        if readback != payload:
            raise StoreError('payload readback failed')
        entries = [dict(e) for e in self.entries]
        entry = dict(id=metadata['id'], title=metadata['title'], slot=slot,
                     bytes=len(data), checksum=crc(data))
        if index == len(entries):
            entries.append(entry)
        else:
            entries[index] = entry
        self.commit(entries)

    def uninstall(self, app_id):
        entries = [e for e in self.entries if e['id'] != app_id]
        if len(entries) == len(self.entries):
            raise StoreError('app is not installed')
        self.commit(entries)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    new = subs.add_parser('create', help='exclusively create a NEW formatted image')
    new.add_argument('image')
    new.add_argument('--size-mib', type=int, default=8)
    for name in ('list', 'check', 'install', 'uninstall'):
        sub = subs.add_parser(name)
        sub.add_argument('image')
        if name in ('install', 'uninstall'):
            sub.add_argument('package' if name == 'install' else 'app_id')
            sub.add_argument('--offline', required=True, action='store_true',
                             help='confirm the image is not attached to a running VM')
    args = parser.parse_args(argv)
    image = None
    try:
        if args.command == 'create':
            create(args.image, args.size_mib)
            print('Created %s: %d MiB; 8 app entries, 16 copy-on-write payload slots' %
                  (args.image, args.size_mib))
            return 0
        image = Image(args.image, args.command in ('install', 'uninstall'))
        if args.command == 'install':
            image.install(Path(args.package).read_bytes())
        elif args.command == 'uninstall':
            image.uninstall(args.app_id)
        if args.command == 'check':
            for entry in image.entries:
                image.read(entry['id'])
        print(json.dumps(dict(generation=image.generation,
                              directory_snapshots_valid=[bool(v) for v in image.decoded],
                              apps=image.entries), indent=2))
        return 0
    except (OSError, ValueError) as exc:
        print('disk: %s' % exc, file=sys.stderr)
        return 1
    finally:
        if image:
            image.close()


if __name__ == '__main__':
    sys.exit(main())
