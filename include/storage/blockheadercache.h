#ifndef GTOS_STORAGE_BLOCKHEADERCACHE_H
#define GTOS_STORAGE_BLOCKHEADERCACHE_H
#include <storage/filestore.h>
namespace gtos { namespace storage {
// Cache the first sector of aligned LittleFS blocks, where the CTZ links live.
// FileStore serializes access. The device must be exclusively owned by this
// volume, including writes. Writes and flushes still reach the device directly.
class BlockHeaderCache : public BlockDevice {
    enum { Entries = 128 };
    static const uint32_t Empty = 0xFFFFFFFFU;
    struct Entry {
        uint32_t sector;
        uint8_t bytes[FileStore::SectorSize];
    };
    BlockDevice& device;
    Entry entries[Entries];

    void Clear() {
        for (uint32_t i=0;i<Entries;i++) entries[i].sector=Empty;
    }
    Entry* Find(uint32_t sector) {
        if (sector%FileStore::SectorsPerBlock) return 0;
        const uint32_t block=sector/FileStore::SectorsPerBlock;
        return &entries[(block^(block/Entries))%Entries];
    }
public:
    explicit BlockHeaderCache(BlockDevice& source):device(source) { Clear(); }
    bool Identify() override {
        Clear();
        return device.Identify();
    }
    uint32_t SectorCount() const override { return device.SectorCount(); }
    bool ReadSector(uint32_t sector,uint8_t* data) override {
        if (!data) return false;
        Entry* entry=Find(sector);
        if (entry&&entry->sector==sector) {
            gtos_lfs_memcpy(data,entry->bytes,FileStore::SectorSize);
            return true;
        }
        if (!device.ReadSector(sector,data)) return false;
        if (entry) {
            gtos_lfs_memcpy(entry->bytes,data,FileStore::SectorSize);
            entry->sector=sector;
        }
        return true;
    }
    bool WriteSector(uint32_t sector,const uint8_t* data) override {
        // An unsuccessful write may still have changed the medium.
        Entry* entry=Find(sector);
        if (entry&&entry->sector==sector) entry->sector=Empty;
        return device.WriteSector(sector,data);
    }
    bool ReadSectors(uint32_t first,uint32_t count,uint8_t* data) override {
        if (!ValidRead(first,count,data)) return false;
        if (count==1) return ReadSector(first,data);
        if (!device.ReadSectors(first,count,data)) return false;
        for (uint32_t i=0;i<count;i++) {
            Entry* entry=Find(first+i);
            if (entry) {
                gtos_lfs_memcpy(entry->bytes,data+(__UINTPTR_TYPE__)i*FileStore::SectorSize,FileStore::SectorSize);
                entry->sector=first+i;
            }
        }
        return true;
    }
    bool Flush() override { return device.Flush(); }
};
} }
#endif
