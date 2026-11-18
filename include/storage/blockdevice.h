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
    virtual bool ReadSectors(uint32_t first,uint32_t count,uint8_t* data) {
        if (!ValidTransfer(first,count,data)) return false;
        for (uint32_t i=0;i<count;i++)
            if (!ReadSector(first+i,data+(__UINTPTR_TYPE__)i*512U)) return false;
        return true;
    }
    virtual bool WriteSector(uint32_t sector,const uint8_t* data)=0;
    virtual bool WriteSectors(uint32_t first,uint32_t count,const uint8_t* data) {
        if (!ValidTransfer(first,count,data)) return false;
        for (uint32_t i=0;i<count;i++)
            if (!WriteSector(first+i,data+(__UINTPTR_TYPE__)i*512U)) return false;
        return true;
    }
    virtual bool Flush()=0;
protected:
    bool ValidTransfer(uint32_t first,uint32_t count,const uint8_t* data) const {
        const uint32_t sectors=SectorCount();
        const __UINTPTR_TYPE__ address=(__UINTPTR_TYPE__)data;
        const __UINTPTR_TYPE__ maximum=~(__UINTPTR_TYPE__)0;
        return data&&count&&first<sectors&&count<=sectors-first
            &&(uint64_t)count*512U<=maximum-address;
    }
};
} }
#endif
