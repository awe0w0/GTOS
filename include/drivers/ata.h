#ifndef __GTOS__DRIVERS__ATA_H
#define __GTOS__DRIVERS__ATA_H
#include <common/types.h>
#include <hardwarecommunication/port.h>
#include <storage/blockdevice.h>
namespace gtos { namespace drivers {
// Polling ATA PIO, LBA28, 512-byte sectors. Every hardware wait is bounded.
class AdvancedTechnologyAttachment : public storage::BlockDevice {
protected:
    hardwarecommunication::Port16Bit dataPort;
    hardwarecommunication::Port8Bit errorPort, sectorCountPort, lbaLowPort,
        lbaMidPort, lbaHiPort, devicePort, commandPort, controlPort;
    bool master, present;
    uint32_t sectors;
    char model[41];
    bool Wait(bool dataRequest);
    void Delay();
    bool Select(uint32_t sector);
public:
    enum Error { None, NoDevice, Timeout, DeviceError, Unsupported, BadArgument };
private:
    Error lastError;
public:
    AdvancedTechnologyAttachment(uint16_t portBase, bool master);
    ~AdvancedTechnologyAttachment();
    virtual bool Identify();
    bool Read28(uint32_t sector, uint8_t* data, int count = 512);
    bool Write28(uint32_t sector, const uint8_t* data, int count = 512);
    virtual bool Flush();
    virtual bool ReadSector(uint32_t sector,uint8_t* data) { return Read28(sector,data,512); }
    virtual bool WriteSector(uint32_t sector,const uint8_t* data) { return Write28(sector,data,512); }
    bool Present() const { return present; }
    virtual uint32_t SectorCount() const { return sectors; }
    const char* Model() const { return model; }
    Error LastError() const { return lastError; }
};
} }
#endif
