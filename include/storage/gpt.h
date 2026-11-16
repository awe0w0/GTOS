#ifndef GTOS_STORAGE_GPT_H
#define GTOS_STORAGE_GPT_H

#include <storage/readable_disk.h>

namespace gtos { namespace storage {
    struct GptPartition {
        uint64_t first, last, attributes;
        uint8_t type[16], id[16];
        uint16_t name[37];
    };

    enum GptEntryResult { GptUnused, GptPartitionFound, GptEntryError };

    class GptTable {
        ReadableDisk* disk;
        uint64_t entriesLba, firstUsable, lastUsable;
        uint32_t entryBytes, entryCount;
        bool usingBackup;

        bool ReadHeader(ReadableDisk& device, uint64_t lba, bool backup);

    public:
        GptTable();
        // The device and its partition data must stay unchanged while this table is open.
        bool Open(ReadableDisk& device);
        uint32_t EntryCount() const { return entryCount; }
        bool UsingBackup() const { return usingBackup; }
        GptEntryResult Entry(uint32_t index, GptPartition& partition);
    };
} }

#endif
