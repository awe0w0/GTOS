#include <storage/liveblockdevice.h>
#include <storage/appstore.h>
#include <storage/settings.h>
#include <apps/package.h>
using namespace gtos::storage;
using gtos::apps::Write32;
using gtos::apps::CRC32;
static_assert((uint32_t)LiveBlockDevice::Sectors==(uint32_t)SettingsStore::RequiredSectors,
    "Live RAM disk must include exactly the app-store and settings sectors");
static_assert((uint32_t)AppStore::TotalSectors==(uint32_t)SettingsStore::FirstSector,
    "App-store extent must end before the live settings copies");
LiveBlockDevice::LiveBlockDevice() {
    for (uint32_t i=0;i<sizeof(bytes);++i) bytes[i]=0;
    const uint8_t super[8]={'G','T','S','T','O','R','1',0};
    const uint8_t directory[8]={'G','T','D','I','R','0','1',0};
    for (uint32_t i=0;i<8;++i) bytes[i]=super[i];
    Write32(bytes+8,1);
    Write32(bytes+12,SectorBytes);
    Write32(bytes+16,AppStore::TotalSectors);
    Write32(bytes+20,AppStore::SlotCount);
    Write32(bytes+24,AppStore::SlotSectors);
    Write32(bytes+28,AppStore::MaxApps);
    Write32(bytes+32,AppStore::DataStart);
    Write32(bytes+508,CRC32(bytes,508));
    for (uint32_t sector=1;sector<=2;++sector) {
        uint8_t* data=bytes+sector*SectorBytes;
        for (uint32_t i=0;i<8;++i) data[i]=directory[i];
        Write32(data+508,CRC32(data,508));
    }
}
bool LiveBlockDevice::ReadSector(uint32_t sector,uint8_t* destination) {
    if (sector>=Sectors||!destination) return false;
    const uint8_t* source=bytes+sector*SectorBytes;
    for (uint32_t i=0;i<SectorBytes;++i) destination[i]=source[i];
    return true;
}
bool LiveBlockDevice::WriteSector(uint32_t sector,const uint8_t* source) {
    if (sector>=Sectors||!source) return false;
    uint8_t* destination=bytes+sector*SectorBytes;
    for (uint32_t i=0;i<SectorBytes;++i) destination[i]=source[i];
    return true;
}
