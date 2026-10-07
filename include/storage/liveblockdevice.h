#ifndef GTOS_STORAGE_LIVEBLOCKDEVICE_H
#define GTOS_STORAGE_LIVEBLOCKDEVICE_H
#include <storage/blockdevice.h>
namespace gtos { namespace storage {
// A bounded, BSP-owned RAM disk using the real GTSTOR1 app-store format.
// Writes and flushes are synchronous in RAM; no contents survive a reboot.
// This object never accesses a controller, a partition, or a host filesystem.
class LiveBlockDevice : public BlockDevice {
public:
    enum { Sectors=261, SectorBytes=512 };
private:
    uint8_t bytes[Sectors*SectorBytes];
public:
    LiveBlockDevice();
    virtual bool Identify() { return true; }
    virtual uint32_t SectorCount() const { return Sectors; }
    virtual bool ReadSector(uint32_t sector,uint8_t* destination);
    virtual bool WriteSector(uint32_t sector,const uint8_t* source);
    virtual bool Flush() { return true; }
};
} }
#endif
