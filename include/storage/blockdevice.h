#ifndef GTOS_STORAGE_BLOCKDEVICE_H
#define GTOS_STORAGE_BLOCKDEVICE_H
#include <common/types.h>
namespace gtos { namespace storage {
// Transfer contract: exactly 512 bytes per sector; Flush establishes ordering.
class BlockDevice {
public:
    virtual bool Identify()=0;
    virtual uint32_t SectorCount() const=0;
    virtual bool ReadSector(uint32_t sector,uint8_t* data)=0;
    virtual bool WriteSector(uint32_t sector,const uint8_t* data)=0;
    virtual bool Flush()=0;
};
} }
#endif
