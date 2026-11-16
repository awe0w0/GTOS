#include <storage/ntfs.h>

using namespace gtos::storage;

namespace {
    const uint32_t FileRecord = 0x454C4946; // FILE
    const uint32_t IndexRecord = 0x58444E49; // INDX
    const uint32_t DataAttribute = 0x80;
    const uint32_t IndexRoot = 0x90;
    const uint32_t IndexAllocation = 0xA0;
    const uint32_t Bitmap = 0xB0;

    uint64_t Unsigned(const uint8_t* p, uint32_t bytes) {
        uint64_t value = 0;
        for (uint32_t i = bytes; i; --i)
            value = (value << 8) | p[i - 1];
        return value;
    }

    uint16_t Le16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
    uint32_t Le32(const uint8_t* p) { return (uint32_t)Unsigned(p, 4); }
    uint64_t Le64(const uint8_t* p) { return Unsigned(p, 8); }

    uint32_t Shift(uint32_t powerOfTwo) {
        uint32_t bits = 0;
        while (powerOfTwo > 1) {
            powerOfTwo >>= 1;
            ++bits;
        }
        return bits;
    }

    bool PowerOfTwo(uint32_t n) { return n && !(n & (n - 1)); }

    uint32_t RecordBytes(uint8_t code, uint32_t clusterBytes, uint32_t maximum) {
        if (code & 128) {
            const uint32_t exponent = 256 - code;
            return exponent <= Shift(maximum) ? 1u << exponent : 0;
        }
        if (!code || code > maximum / clusterBytes)
            return 0;
        return code * clusterBytes;
    }

    bool Fixup(uint8_t* record, uint32_t bytes, uint32_t signature, uint32_t minimumOffset) {
        if (bytes < 512 || Le32(record) != signature)
            return false;
        const uint32_t offset = Le16(record + 4);
        const uint32_t count = Le16(record + 6);
        if ((offset & 1) || offset < minimumOffset || count != bytes / 512 + 1 ||
            offset + count * 2 > 510)
            return false;
        const uint16_t sequence = Le16(record + offset);
        for (uint32_t i = 1; i < count; ++i) {
            uint8_t* trailer = record + i * 512 - 2;
            if (Le16(trailer) != sequence)
                return false;
            trailer[0] = record[offset + i * 2];
            trailer[1] = record[offset + i * 2 + 1];
        }
        return true;
    }

    bool ValidRecord(const uint8_t* record, uint32_t bytes) {
        const uint32_t used = Le32(record + 24);
        const uint32_t allocated = Le32(record + 28);
        const uint32_t attributes = Le16(record + 20);
        const uint32_t fixupEnd = Le16(record + 4) + 2u * Le16(record + 6);
        return used <= allocated && allocated == bytes && used >= 52 &&
               attributes >= fixupEnd && attributes >= 48 &&
               !(attributes & 7) && attributes <= used - 4;
    }

    bool NameMatches(const uint8_t* utf16, uint32_t units, const char* ascii) {
        for (uint32_t i = 0; i < units; ++i)
            if (!ascii[i] || Le16(utf16 + 2 * i) != (uint8_t)ascii[i])
                return false;
        return ascii[units] == 0;
    }

    bool Utf8Name(const uint8_t* utf16, uint32_t units, char* out) {
        for (uint32_t i = 0; i < units; ++i) {
            uint32_t value = Le16(utf16 + 2 * i);
            if (!value || value == '/' || value == '\\')
                return false;
            if (value >= 0xD800 && value <= 0xDBFF && i + 1 < units) {
                const uint32_t second = Le16(utf16 + 2 * (i + 1));
                if (second >= 0xDC00 && second <= 0xDFFF) {
                    value = 0x10000 + ((value - 0xD800) << 10) + second - 0xDC00;
                    ++i;
                }
            }
            if (value >= 0xD800 && value <= 0xDFFF)
                return false;
            if (value < 0x80) {
                *out++ = (char)value;
            } else {
                if (value < 0x800) {
                    *out++ = (char)(0xC0 | (value >> 6));
                } else {
                    if (value < 0x10000) {
                        *out++ = (char)(0xE0 | (value >> 12));
                    } else {
                        *out++ = (char)(0xF0 | (value >> 18));
                        *out++ = (char)(0x80 | ((value >> 12) & 63));
                    }
                    *out++ = (char)(0x80 | ((value >> 6) & 63));
                }
                *out++ = (char)(0x80 | (value & 63));
            }
        }
        *out = 0;
        return true;
    }

    NtfsStatus VisitIndex(const uint8_t* node, uint32_t bytes, NtfsVolume::Visitor visitor,
                          void* context, bool& stopped) {
        if (bytes < 16)
            return NtfsInvalid;
        uint32_t position = Le32(node);
        const uint32_t used = Le32(node + 4);
        const uint32_t allocated = Le32(node + 8);
        if (position < 16 || (position & 7) || position > used || used > allocated ||
            allocated > bytes || node[12] > 1)
            return NtfsInvalid;
        while (used - position >= 16) {
            const uint8_t* entry = node + position;
            const uint32_t size = Le16(entry + 8);
            const uint32_t keyBytes = Le16(entry + 10);
            const uint32_t flags = Le16(entry + 12);
            const uint32_t overhead = (flags & 1) ? 24 : 16;
            if ((flags & ~3u) || size < overhead || size > used - position ||
                (size & 7) || keyBytes > size - overhead)
                return NtfsInvalid;
            if (flags & 2)
                return keyBytes == 0 && size == used - position ? NtfsOk : NtfsInvalid;
            if (keyBytes < 66)
                return NtfsInvalid;
            const uint8_t* name = entry + 16;
            const uint32_t units = name[64];
            if (!units || units * 2 > keyBytes - 66 || name[65] > 3)
                return NtfsInvalid;
            if (name[65] != 2) {
                const uint64_t reference = Le64(entry);
                const uint32_t attributes = Le32(name + 56);
                NtfsEntry item;
                item.id = {reference & 0xFFFFFFFFFFFFULL, (uint16_t)(reference >> 48)};
                item.bytes = Le64(name + 48);
                item.directory = attributes & 0x10000000u;
                item.reparse = attributes & 0x400u;
                if (!item.id.sequence || !Utf8Name(name + 66, units, item.name))
                    return NtfsInvalid;
                if (!visitor(item, context)) {
                    stopped = true;
                    return NtfsOk;
                }
            }
            position += size;
        }
        return NtfsInvalid;
    }
}

NtfsVolume::NtfsVolume()
    : disk(0), firstSector(0), volumeBytes(0), sectorShift(0), clusterShift(0),
      recordShift(0), indexShift(0), mft{} {}

NtfsStatus NtfsVolume::Mount(ReadableDisk& source, const GptPartition& partition) {
    disk = 0;
    mft = {};
    const NtfsStatus status = MountVolume(source, partition);
    if (status != NtfsOk) {
        disk = 0;
        mft = {};
    }
    return status;
}

NtfsStatus NtfsVolume::MountVolume(ReadableDisk& source, const GptPartition& part) {
    const uint32_t sector = source.LogicalSectorBytes();
    if (sector < 512 || sector > MaximumSectorBytes || !PowerOfTwo(sector) ||
        part.first > part.last || part.last >= source.LogicalSectorCount())
        return NtfsInvalid;
    uint8_t boot[MaximumSectorBytes];
    if (!source.ReadSectors(part.first, 1, boot))
        return NtfsIo;
    if (Le64(boot + 3) != 0x202020205346544EULL || Le16(boot + 510) != 0xAA55)
        return NtfsUnsupported;

    uint32_t sectorsPerCluster = boot[13];
    if (sectorsPerCluster >= 0xF4)
        sectorsPerCluster = 1u << (256 - sectorsPerCluster);
    else if (sectorsPerCluster > 0x80)
        return NtfsInvalid;
    if (Le16(boot + 11) != sector || !PowerOfTwo(sectorsPerCluster))
        return NtfsInvalid;
    const uint32_t clusterBytes = sector * sectorsPerCluster;
    const uint32_t recordBytes = RecordBytes(boot[64], clusterBytes, MaximumRecordBytes);
    const uint32_t indexBytes = RecordBytes(boot[68], clusterBytes, MaximumIndexBytes);
    if (recordBytes < 512 || indexBytes < 512 || !PowerOfTwo(recordBytes) ||
        !PowerOfTwo(indexBytes))
        return NtfsUnsupported;

    const uint64_t sectors = Le64(boot + 40);
    const uint64_t mftCluster = Le64(boot + 48);
    sectorShift = Shift(sector);
    clusterShift = Shift(clusterBytes);
    indexShift = Shift(indexBytes);
    recordShift = Shift(recordBytes);
    if (!sectors || sectors - 1 > part.last - part.first ||
        sectors > (~0ULL >> sectorShift) || mftCluster >= (sectors >> Shift(sectorsPerCluster)))
        return NtfsInvalid;
    firstSector = part.first;
    volumeBytes = sectors << sectorShift;
    disk = &source;
    const NtfsStatus read = ReadBytes(mftCluster << clusterShift, recordBytes, mftRecord);
    if (read != NtfsOk)
        return read;
    if (!Fixup(mftRecord, recordBytes, FileRecord, 42) || !ValidRecord(mftRecord, recordBytes))
        return NtfsInvalid;
    const NtfsStatus attr = Attribute(mftRecord, DataAttribute, "", mft);
    if (attr != NtfsOk)
        return attr;
    if (!mft.nonresident || mft.initialized < (6ULL << recordShift))
        return NtfsInvalid;
    uint8_t root[MaximumRecordBytes];
    const NtfsStatus rootStatus = ReadRecord(Root(), root);
    if (rootStatus != NtfsOk)
        return rootStatus;
    return (Le16(root + 22) & 2) ? NtfsOk : NtfsInvalid;
}

NtfsStatus NtfsVolume::ReadBytes(uint64_t offset, uint32_t bytes, uint8_t* output) const {
    if (offset > volumeBytes || bytes > volumeBytes - offset)
        return NtfsInvalid;
    const uint32_t sector = 1u << sectorShift;
    uint8_t scratch[MaximumSectorBytes];
    while (bytes) {
        const uint64_t lba = firstSector + (offset >> sectorShift);
        const uint32_t start = (uint32_t)offset & (sector - 1);
        uint32_t amount;
        if (!start && bytes >= sector) {
            const uint32_t count = bytes >> sectorShift;
            if (!disk->ReadSectors(lba, count, output))
                return NtfsIo;
            amount = count << sectorShift;
        } else {
            if (!disk->ReadSectors(lba, 1, scratch))
                return NtfsIo;
            amount = sector - start;
            if (amount > bytes) amount = bytes;
            for (uint32_t i = 0; i < amount; ++i)
                output[i] = scratch[start + i];
        }
        output += amount;
        offset += amount;
        bytes -= amount;
    }
    return NtfsOk;
}

NtfsStatus NtfsVolume::Attribute(const uint8_t* record, uint32_t type, const char* name,
                                 Stream& stream) {
    uint32_t offset = Le16(record + 20);
    const uint32_t used = Le32(record + 24);
    bool list = false;
    while (used - offset >= 4) {
        const uint8_t* p = record + offset;
        const uint32_t candidate = Le32(p);
        if (candidate == 0xFFFFFFFFu)
            return list ? NtfsUnsupported : NtfsNotFound;
        if (used - offset < 16)
            return NtfsInvalid;
        const uint32_t bytes = Le32(p + 4);
        const uint32_t header = p[8] ? 64 : 24;
        const uint32_t nameOffset = Le16(p + 10);
        if (p[8] > 1 || bytes < header || (bytes & 7) || bytes > used - offset ||
            (p[9] && (nameOffset < header || nameOffset > bytes ||
                      p[9] * 2u > bytes - nameOffset)))
            return NtfsInvalid;
        list |= candidate == 0x20;
        if (candidate == type && (p[9] ? NameMatches(p + nameOffset, p[9], name) : !name[0])) {
            if (Le16(p + 12) & 0x4001u) // encryption or compression
                return NtfsUnsupported;
            const uint32_t content = p[8] ? Le16(p + 32) : Le16(p + 20);
            const uint32_t afterName = p[9] ? nameOffset + p[9] * 2u : header;
            if (content < afterName || content > bytes)
                return NtfsInvalid;
            if (p[8]) {
                if (Le64(p + 16))
                    return NtfsUnsupported;
                const uint64_t size = Le64(p + 48);
                const uint64_t initialized = Le64(p + 56);
                if (initialized > size || content == bytes)
                    return NtfsInvalid;
                stream = {p + content, bytes - content, size, initialized, true};
            } else {
                const uint32_t size = Le32(p + 16);
                if (size > bytes - content)
                    return NtfsInvalid;
                stream = {p + content, size, size, size, false};
            }
            return NtfsOk;
        }
        offset += bytes;
    }
    return NtfsInvalid;
}

NtfsStatus NtfsVolume::ReadStream(const Stream& stream, uint64_t offset, uint32_t bytes,
                                   uint8_t* output) const {
    if (offset > stream.size || bytes > stream.size - offset)
        return NtfsInvalid;
    if (!stream.nonresident) {
        for (uint32_t i = 0; i < bytes; ++i)
            output[i] = stream.data[(uint32_t)offset + i];
        return NtfsOk;
    }

    const uint64_t available = offset < stream.initialized ? stream.initialized - offset : 0;
    uint32_t remaining = available < bytes ? (uint32_t)available : bytes;
    const uint32_t zeroTail = bytes - remaining;
    const uint64_t clusters = volumeBytes >> clusterShift;
    uint64_t lcn = 0, virtualByte = 0;
    uint32_t cursor = 0;
    while (remaining) {
        if (cursor >= stream.bytes || !stream.data[cursor])
            return NtfsUnsupported; // A later attribute extent may hold the missing mapping.
        const uint8_t fields = stream.data[cursor++];
        const uint32_t lengthBytes = fields & 15;
        const uint32_t deltaBytes = fields >> 4;
        if (!lengthBytes || lengthBytes > 8 || deltaBytes > 8 ||
            lengthBytes + deltaBytes > stream.bytes - cursor)
            return NtfsInvalid;
        const uint64_t length = Unsigned(stream.data + cursor, lengthBytes);
        cursor += lengthBytes;
        if (!length || length > ((~0ULL - virtualByte) >> clusterShift))
            return NtfsInvalid;
        if (deltaBytes) {
            const uint64_t delta = Unsigned(stream.data + cursor, deltaBytes);
            if (stream.data[cursor + deltaBytes - 1] & 128) {
                const uint64_t mask = ~0ULL >> (64 - 8 * deltaBytes);
                const uint64_t magnitude = (~delta & mask) + 1;
                if (magnitude > lcn)
                    return NtfsInvalid;
                lcn -= magnitude;
            } else {
                if (delta > clusters - lcn)
                    return NtfsInvalid;
                lcn += delta;
            }
            if (length > clusters - lcn)
                return NtfsInvalid;
        }
        cursor += deltaBytes;
        const uint64_t end = virtualByte + (length << clusterShift);
        if (offset < end) {
            const uint64_t inRun = end - offset;
            const uint32_t amount = inRun < remaining ? (uint32_t)inRun : remaining;
            if (deltaBytes) {
                const NtfsStatus status = ReadBytes((lcn << clusterShift) + (offset - virtualByte),
                                                    amount, output);
                if (status != NtfsOk)
                    return status;
            } else {
                for (uint32_t i = 0; i < amount; ++i) output[i] = 0;
            }
            output += amount;
            offset += amount;
            remaining -= amount;
        }
        virtualByte = end;
    }
    for (uint32_t i = 0; i < zeroTail; ++i) output[i] = 0;
    return NtfsOk;
}

NtfsStatus NtfsVolume::ReadRecord(NtfsFileId file, uint8_t* output) const {
    const uint32_t bytes = 1u << recordShift;
    if (file.record >= (mft.initialized >> recordShift))
        return NtfsNotFound;
    const NtfsStatus status = ReadStream(mft, file.record << recordShift, bytes, output);
    if (status != NtfsOk)
        return status;
    if (!Fixup(output, bytes, FileRecord, 42) || !ValidRecord(output, bytes))
        return NtfsInvalid;
    if (!(Le16(output + 22) & 1) || (file.sequence && file.sequence != Le16(output + 16)))
        return NtfsNotFound;
    return Le64(output + 32) ? NtfsUnsupported : NtfsOk;
}

NtfsStatus NtfsVolume::List(NtfsFileId directory, Visitor visitor, void* context) const {
    if (!disk) return NtfsNotMounted;
    if (!visitor) return NtfsInvalid;
    uint8_t record[MaximumRecordBytes];
    NtfsStatus status = ReadRecord(directory, record);
    if (status != NtfsOk) return status;
    if (!(Le16(record + 22) & 2)) return NtfsInvalid;
    Stream root;
    status = Attribute(record, IndexRoot, "$I30", root);
    if (status != NtfsOk) return status;
    const uint32_t bytes = 1u << indexShift;
    if (root.nonresident || root.bytes < 32 || Le32(root.data) != 0x30 ||
        Le32(root.data + 4) != 1 || Le32(root.data + 8) != bytes)
        return NtfsInvalid;
    bool stopped = false;
    status = VisitIndex(root.data + 16, root.bytes - 16, visitor, context, stopped);
    if (status != NtfsOk || stopped || !root.data[28]) return status;

    Stream allocation, bitmap;
    status = Attribute(record, IndexAllocation, "$I30", allocation);
    if (status != NtfsOk) return status;
    status = Attribute(record, Bitmap, "$I30", bitmap);
    if (status != NtfsOk) return status;
    const uint32_t vcnShift = clusterShift > indexShift ? 9 : clusterShift;
    const uint64_t blocks = allocation.size >> indexShift;
    if (!allocation.nonresident || allocation.size > volumeBytes ||
        (allocation.size & (bytes - 1)) || !blocks ||
        (blocks + 7) / 8 > bitmap.initialized ||
        root.data[12] != (bytes >> vcnShift))
        return NtfsInvalid;

    uint8_t bits[128];
    uint8_t block[MaximumIndexBytes];
    for (uint64_t index = 0; index < blocks; ++index) {
        if (!(index & 1023)) {
            const uint64_t bytesLeft = (blocks - index + 7) / 8;
            const uint32_t size = bytesLeft < sizeof(bits) ? (uint32_t)bytesLeft : sizeof(bits);
            status = ReadStream(bitmap, index / 8, size, bits);
            if (status != NtfsOk) return status;
        }
        if (!(bits[(index & 1023) / 8] & (1u << (index & 7))))
            continue;
        const uint64_t offset = index << indexShift;
        status = ReadStream(allocation, offset, bytes, block);
        if (status != NtfsOk) return status;
        if (!Fixup(block, bytes, IndexRecord, 40) || Le64(block + 16) != (offset >> vcnShift))
            return NtfsInvalid;
        status = VisitIndex(block + 24, bytes - 24, visitor, context, stopped);
        if (status != NtfsOk || stopped) return status;
    }
    return NtfsOk;
}

NtfsStatus NtfsVolume::Find(NtfsFileId directory, const char* name, NtfsEntry& entry) const {
    if (!name || !name[0]) return NtfsInvalid;
    struct Lookup { const char* name; NtfsEntry& entry; bool found; } lookup{name, entry, false};
    const NtfsStatus status = List(directory, [](const NtfsEntry& candidate, void* opaque) {
        Lookup& state = *(Lookup*)opaque;
        uint32_t i = 0;
        while (candidate.name[i] && candidate.name[i] == state.name[i]) ++i;
        if (candidate.name[i] != state.name[i]) return true;
        state.entry = candidate;
        state.found = true;
        return false;
    }, &lookup);
    return status != NtfsOk ? status : lookup.found ? NtfsOk : NtfsNotFound;
}

NtfsStatus NtfsVolume::ReadFile(NtfsFileId file, uint64_t offset, uint32_t bytes, uint8_t* output,
                                uint32_t& read) const {
    read = 0;
    if (!disk) return NtfsNotMounted;
    if (bytes && !output) return NtfsInvalid;
    uint8_t record[MaximumRecordBytes];
    NtfsStatus status = ReadRecord(file, record);
    if (status != NtfsOk) return status;
    if (Le16(record + 22) & 2) return NtfsInvalid;
    Stream stream;
    status = Attribute(record, DataAttribute, "", stream);
    if (status != NtfsOk) return status;
    if (!bytes || offset >= stream.size) return NtfsOk;
    const uint64_t left = stream.size - offset;
    const uint32_t wanted = left < bytes ? (uint32_t)left : bytes;
    status = ReadStream(stream, offset, wanted, output);
    if (status == NtfsOk) read = wanted;
    return status;
}
