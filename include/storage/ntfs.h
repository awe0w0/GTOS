#ifndef GTOS_STORAGE_NTFS_H
#define GTOS_STORAGE_NTFS_H

#include <storage/gpt.h>

namespace gtos { namespace storage {
    enum NtfsStatus { NtfsOk, NtfsNotMounted, NtfsIo, NtfsInvalid, NtfsUnsupported, NtfsNotFound };

    struct NtfsFileId {
        uint64_t record;
        uint16_t sequence;
    };

    struct NtfsEntry {
        NtfsFileId id;
        uint64_t bytes;
        bool directory, reparse;
        char name[3 * 255 + 1];
    };

    // Read-only NTFS 3.x streams within one GPT partition. File references are
    // valid only while the source disk remains mounted and unchanged.
    class NtfsVolume {
        static const uint32_t MaximumSectorBytes = 4096;
        static const uint32_t MaximumRecordBytes = 4096;
        static const uint32_t MaximumIndexBytes = 16384;

        struct Stream {
            const uint8_t* data;
            uint32_t bytes;
            uint64_t size, initialized;
            bool nonresident;
        };

        ReadableDisk* disk;
        uint64_t firstSector, volumeBytes;
        uint32_t sectorShift, clusterShift, recordShift, indexShift;
        uint8_t mftRecord[MaximumRecordBytes];
        Stream mft;

        NtfsStatus ReadBytes(uint64_t offset, uint32_t bytes, uint8_t* output) const;
        NtfsStatus ReadStream(const Stream& stream, uint64_t offset, uint32_t bytes,
                              uint8_t* output) const;
        NtfsStatus ReadRecord(NtfsFileId file, uint8_t* output) const;
        NtfsStatus MountVolume(ReadableDisk& source, const GptPartition& partition);
        static NtfsStatus Attribute(const uint8_t* record, uint32_t type, const char* name,
                                    Stream& stream);
        NtfsVolume(const NtfsVolume&) = delete;
        NtfsVolume& operator=(const NtfsVolume&) = delete;

    public:
        // Returning false from a visitor ends a successful enumeration.
        typedef bool (*Visitor)(const NtfsEntry&, void*);

        NtfsVolume();
        NtfsStatus Mount(ReadableDisk& source, const GptPartition& partition);
        static NtfsFileId Root() { return {5, 0}; }
        NtfsStatus List(NtfsFileId directory, Visitor visitor, void* context) const;
        // Exact UTF-8 name lookup; no short-name aliases or reparse traversal.
        NtfsStatus Find(NtfsFileId directory, const char* name, NtfsEntry& entry) const;
        NtfsStatus ReadFile(NtfsFileId file, uint64_t offset, uint32_t bytes, uint8_t* output,
                            uint32_t& read) const;
    };
} }

#endif
