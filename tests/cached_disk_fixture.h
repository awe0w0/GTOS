#ifndef GTOS_TEST_CACHED_DISK_FIXTURE_H
#define GTOS_TEST_CACHED_DISK_FIXTURE_H
#include <storage/appstore.h>
namespace gtos_test {
using namespace gtos::storage;
using namespace gtos::apps;
inline void Zero(uint8_t* p,uint32_t n) { for(uint32_t i=0;i<n;i++)p[i]=0; }
inline void Copy(uint8_t* out,const uint8_t* in,uint32_t n) { for(uint32_t i=0;i<n;i++)out[i]=in[i]; }
// Explicit volatile-cache and durable-media views with deterministic faults.
class CachedDisk:public BlockDevice {
public:
    enum { Count=264 };
    uint8_t data[Count][512],durable[Count][512];
    uint32_t sectors;
    int writes,reads,flushes,failWrite,failRead,corruptRead;
    uint32_t tearBytes,lastSector;
    bool available,failFlush,flushDespiteFailure,lieAboutWrite;
    CachedDisk() { Format(); }
    void ResetFaults() {
        writes=reads=flushes=0; failWrite=failRead=corruptRead=-1;
        tearBytes=0; lastSector=0; available=true;
        failFlush=flushDespiteFailure=lieAboutWrite=false;
    }
    bool Identify() { return available; }
    uint32_t SectorCount() const { return sectors; }
    bool ReadSector(uint32_t sector,uint8_t* out) {
        int call=reads++;
        if(call==failRead||sector>=sectors||sector>=Count) return false;
        Copy(out,data[sector],512);
        if(call==corruptRead) out[64]^=1;
        return true;
    }
    bool WriteSector(uint32_t sector,const uint8_t* in) {
        int call=writes++;
        if(sector>=sectors||sector>=Count) return false;
        lastSector=sector;
        if(call==failWrite) {
            Copy(data[sector],in,tearBytes);
            // A torn write may reach media before a successful flush.
            Copy(durable[sector],in,tearBytes);
            return lieAboutWrite;
        }
        Copy(data[sector],in,512);
        return true;
    }
    bool Flush() {
        flushes++;
        if(!failFlush||flushDespiteFailure) FixtureDurable();
        return !failFlush;
    }
    void FixtureDurable() { Copy((uint8_t*)durable,(uint8_t*)data,sizeof(data)); }
    void PowerLoss() { Copy((uint8_t*)data,(uint8_t*)durable,sizeof(data)); ResetFaults(); }
    void Format() {
        sectors=Count; ResetFaults(); Zero((uint8_t*)data,sizeof(data));
        const uint8_t magic[8]={'G','T','S','T','O','R','1',0}; Copy(data[0],magic,8);
        Write32(data[0]+8,1); Write32(data[0]+12,512); Write32(data[0]+16,259);
        Write32(data[0]+20,16); Write32(data[0]+24,16); Write32(data[0]+28,8);
        Write32(data[0]+32,3); Write32(data[0]+508,CRC32(data[0],508));
        const uint8_t dir[8]={'G','T','D','I','R','0','1',0};
        for(uint32_t i=1;i<=2;i++) {
            Copy(data[i],dir,8); Write32(data[i]+508,CRC32(data[i],508));
        }
        FixtureDurable();
    }
    void Restore(const CachedDisk& other) {
        sectors=other.sectors; ResetFaults();
        Copy((uint8_t*)data,(const uint8_t*)other.data,sizeof(data)); FixtureDurable();
    }
};

}
#endif
