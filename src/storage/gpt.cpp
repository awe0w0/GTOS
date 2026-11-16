#include <storage/gpt.h>
#include <common/crc32.h>

using namespace gtos::storage;
using gtos::common::Crc32Update;

namespace {
    const uint32_t MaximumSectorBytes = 4096;
    const uint32_t StandardEntryBytes = 128;
    const uint32_t MaximumTableBytes = 16 * 1024 * 1024;

    uint32_t Read32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }

    uint64_t Read64(const uint8_t* p) {
        return Read32(p) | ((uint64_t)Read32(p + 4) << 32);
    }

    bool EmptyGuid(const uint8_t* bytes) {
        for (uint32_t i = 0; i < 16; ++i)
            if (bytes[i])
                return false;
        return true;
    }

    bool ProtectiveMbr(ReadableDisk& disk, uint8_t* block) {
        if (!disk.ReadSectors(0, 1, block) || block[510] != 0x55 || block[511] != 0xAA)
            return false;
        const uint64_t remaining = disk.LogicalSectorCount() - 1;
        const uint32_t covered = remaining > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)remaining;
        bool found = false;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint8_t* entry = block + 446 + 16 * i;
            if (!entry[4])
                continue;
            if (found || entry[4] != 0xEE || Read32(entry + 8) != 1 ||
                Read32(entry + 12) != covered)
                return false;
            found = true;
        }
        return found;
    }
}

GptTable::GptTable()
    : disk(0), entriesLba(0), firstUsable(0), lastUsable(0),
      entryBytes(0), entryCount(0), usingBackup(false) {}

bool GptTable::Open(ReadableDisk& device) {
    disk = 0;
    entryCount = 0;
    usingBackup = false;
    const uint32_t bytes = device.LogicalSectorBytes();
    const uint64_t count = device.LogicalSectorCount();
    if (bytes < 512 || bytes > MaximumSectorBytes || (bytes & (bytes - 1)) || count < 6)
        return false;

    uint8_t block[MaximumSectorBytes];
    if (!ProtectiveMbr(device, block))
        return false;
    return ReadHeader(device, 1, false) || ReadHeader(device, count - 1, true);
}

bool GptTable::ReadHeader(ReadableDisk& device, uint64_t address, bool backup) {
    uint8_t block[MaximumSectorBytes];
    const uint32_t sectorBytes = device.LogicalSectorBytes();
    const uint64_t end = device.LogicalSectorCount() - 1;
    if (!device.ReadSectors(address, 1, block) ||
        Read32(block) != 0x20494645u || Read32(block + 4) != 0x54524150u ||
        Read32(block + 8) != 0x00010000u || Read32(block + 20))
        return false;
    const uint32_t headerBytes = Read32(block + 12);
    const uint32_t headerCrc = Read32(block + 16);
    if (headerBytes < 92 || headerBytes > sectorBytes)
        return false;
    for (uint32_t i = 16; i < 20; ++i)
        block[i] = 0;
    if (~Crc32Update(0xFFFFFFFFu, block, headerBytes) != headerCrc)
        return false;

    const uint64_t first = Read64(block + 40);
    const uint64_t last = Read64(block + 48);
    const uint64_t table = Read64(block + 72);
    const uint32_t count = Read32(block + 80);
    const uint32_t bytes = Read32(block + 84);
    const uint32_t expectedCrc = Read32(block + 88);
    if (Read64(block + 24) != address || Read64(block + 32) != (backup ? 1 : end) ||
        EmptyGuid(block + 56) || first < 2 || first > last || last >= end ||
        bytes < StandardEntryBytes || (bytes & (bytes - 1)) ||
        !count || count > MaximumTableBytes / bytes || table < 2 || table >= end)
        return false;

    uint32_t remaining = count * bytes;
    const uint32_t tableSectors = (remaining + sectorBytes - 1) / sectorBytes;
    if (backup ? (table <= last || tableSectors > end - table)
               : (table >= first || tableSectors > first - table))
        return false;

    uint32_t crc = 0xFFFFFFFFu;
    for (uint64_t sector = table; remaining; ++sector) {
        if (!device.ReadSectors(sector, 1, block))
            return false;
        const uint32_t take = remaining < sectorBytes ? remaining : sectorBytes;
        crc = Crc32Update(crc, block, take);
        remaining -= take;
    }
    if (~crc != expectedCrc)
        return false;
    disk = &device;
    entriesLba = table;
    firstUsable = first;
    lastUsable = last;
    entryCount = count;
    entryBytes = bytes;
    usingBackup = backup;
    return true;
}

GptEntryResult GptTable::Entry(uint32_t index, GptPartition& partition) {
    partition = {};
    if (!disk || index >= entryCount)
        return GptEntryError;
    const uint32_t sectorBytes = disk->LogicalSectorBytes();
    const uint32_t byteOffset = index * entryBytes;
    uint8_t sector[MaximumSectorBytes];
    if (!disk->ReadSectors(entriesLba + byteOffset / sectorBytes, 1, sector))
        return GptEntryError;

    const uint8_t* entry = sector + byteOffset % sectorBytes;
    if (EmptyGuid(entry))
        return GptUnused;

    const uint64_t first = Read64(entry + 32);
    const uint64_t last = Read64(entry + 40);
    if (first < firstUsable || first > last || last > lastUsable || EmptyGuid(entry + 16))
        return GptEntryError;
    partition.first = first;
    partition.last = last;
    partition.attributes = Read64(entry + 48);
    for (uint32_t i = 0; i < 16; ++i) {
        partition.type[i] = entry[i];
        partition.id[i] = entry[i + 16];
    }
    for (uint32_t i = 0; i < 36; ++i)
        partition.name[i] = entry[56 + 2 * i] | ((uint16_t)entry[57 + 2 * i] << 8);
    return GptPartitionFound;
}
