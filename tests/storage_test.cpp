// Tests the kernel's actual C++ store implementation against a fault-injecting disk.
#include <storage/appstore.h>
extern "C" int puts(const char*);
using namespace gtos::storage;
using namespace gtos::apps;
static int failures=0;
static void Check(bool good,const char* message) { if (!good) { puts(message); failures++; } }
static void Zero(uint8_t* p,uint32_t n) { for (uint32_t i=0;i<n;i++) p[i]=0; }
static void Copy(uint8_t* a,const uint8_t* b,uint32_t n) { for (uint32_t i=0;i<n;i++) a[i]=b[i]; }
static bool Equal(const uint8_t* a,const uint8_t* b,uint32_t n) { for(uint32_t i=0;i<n;i++) if(a[i]!=b[i])return false;return true; }
class MemoryDisk:public BlockDevice {
public:
    uint8_t data[AppStore::TotalSectors][512];
    int writes, reads, failAt; bool tear, available, failFlush;
    MemoryDisk():writes(0),reads(0),failAt(-1),tear(false),available(true),failFlush(false) { Zero((uint8_t*)data,sizeof(data)); }
    bool Identify() { return available; }
    uint32_t SectorCount()const { return AppStore::TotalSectors; }
    bool ReadSector(uint32_t sector,uint8_t* out) { reads++; if(sector>=AppStore::TotalSectors)return false;Copy(out,data[sector],512);return true; }
    bool WriteSector(uint32_t sector,const uint8_t* in) {
        if(sector>=AppStore::TotalSectors)return false;
        if(writes++==failAt) { if(tear)Copy(data[sector],in,100);return false; }
        Copy(data[sector],in,512);return true;
    }
    bool Flush() { return !failFlush; }
    void Format() {
        Zero((uint8_t*)data,sizeof(data)); writes=reads=0;failAt=-1;tear=false;failFlush=false;available=true;
        const uint8_t magic[8]={'G','T','S','T','O','R','1',0};Copy(data[0],magic,8);
        Write32(data[0]+8,1);Write32(data[0]+12,512);Write32(data[0]+16,259);
        Write32(data[0]+20,16);Write32(data[0]+24,16);Write32(data[0]+28,8);Write32(data[0]+32,3);
        Write32(data[0]+508,CRC32(data[0],508));
        const uint8_t dir[8]={'G','T','D','I','R','0','1',0};
        for(uint32_t i=1;i<=2;i++){Copy(data[i],dir,8);Write32(data[i]+508,CRC32(data[i],508));}
    }
};
static void Package(uint8_t* p,char id) {
    Zero(p,136);
    const uint8_t magic[8]={'G','T','A','P','P','0','1',0};Copy(p,magic,8);
    Write32(p+8,1);Write32(p+12,128);Write32(p+16,136);Write32(p+20,1);
    p[32]=id;p[56]='A';p[80]='T';Write32(p+120,PackageCRC(p,136));
}
int main() {
    MemoryDisk disk; uint8_t p[136],readback[8192];uint32_t length;
    AppStore blank(&disk);Check(!blank.Mount()&&blank.Status()==AppStore::NotFormatted,"unformatted disk refused");
    Package(p,'a');Check(!blank.Install(p,136)&&disk.writes==0,"unformatted disk never written");
    disk.Format();AppStore store(&disk);Check(store.Mount()&&store.Count()==0,"format mounts empty");
    Check(store.Install(p,136)&&store.Count()==1&&store.Generation()==1,"install commits");
    Check(store.Read("a",readback,sizeof(readback),&length)&&length==136&&Equal(p,readback,136),"read verifies exact payload");
    Check(!store.Read("a",readback,10,&length)&&store.Status()==AppStore::BufferSmall&&length==0,"small output buffer rejected");
    AppStore reboot(&disk);Check(reboot.Mount()&&reboot.Count()==1&&reboot.Read("a",readback,8192,&length),"installed app survives remount");
    int writes=disk.writes;
    p[135]^=1;Check(!store.Install(p,136)&&disk.writes==writes,"bad package CRC never written");p[135]^=1;
    Write32(p+8,2);Write32(p+120,PackageCRC(p,136));Check(!store.Install(p,136),"unknown package version rejected");Package(p,'a');
    p[128]=10;Write32(p+132,2);Write32(p+120,PackageCRC(p,136));Check(!store.Install(p,136),"out of bounds VM branch rejected");Package(p,'a');
    for(char id='b';id<='h';id++){Package(p,id);Check(store.Install(p,136),"fill distinct app slot");}
    Package(p,'i');Check(!store.Install(p,136)&&store.Status()==AppStore::Full&&store.Count()==8,"ninth app rejected");
    Package(p,'a');p[56]='B';Write32(p+120,PackageCRC(p,136));Check(store.Install(p,136)&&store.Count()==8,"replace at capacity uses COW");
    Check(store.Uninstall("a")&&store.Count()==7,"uninstall removes only app");
    Check(!store.Uninstall("a")&&store.Status()==AppStore::NotFound,"double uninstall reports missing");
    Check(store.Install(p,136)&&store.Count()==8,"reinstall restores app");
    for(int i=0;i<50;i++)Check(store.Install(p,136),"COW slot recycling stays bounded");
    uint32_t slot=store.Get(7)->slot;disk.data[3+16*slot][135]^=1;
    Check(!store.Read("a",readback,8192,&length)&&store.Status()==AppStore::Corrupt,"payload corruption prevents launch");
    disk.Format();AppStore baseline(&disk);baseline.Mount();Package(p,'a');baseline.Install(p,136);
    MemoryDisk snapshot;Copy((uint8_t*)snapshot.data,(uint8_t*)disk.data,sizeof(disk.data));
    for(int cut=0;cut<=16;cut++)for(int tear=0;tear<2;tear++) {
        Copy((uint8_t*)disk.data,(uint8_t*)snapshot.data,sizeof(disk.data));disk.writes=0;disk.failAt=cut;disk.tear=tear;
        AppStore interrupted(&disk);Check(interrupted.Mount(),"power loss source mounts");Package(p,'b');
        Check(!interrupted.Install(p,136),"injected write failure detected");
        disk.failAt=-1;AppStore recovered(&disk);Check(recovered.Mount(),"torn install retains valid directory");
        Check(recovered.Read("a",readback,8192,&length),"torn install retains old app");
        Check(recovered.Count()==1,"torn install does not publish partial package");
    }
    Copy((uint8_t*)disk.data,(uint8_t*)snapshot.data,sizeof(disk.data));disk.failAt=-1;
    // Snapshot1 is newest at sector2. Corrupt its CRC: snapshot0 remains empty and valid.
    disk.data[2][508]^=1;AppStore fallback(&disk);Check(fallback.Mount()&&fallback.Count()==0,"bad newest metadata rolls back to previous snapshot");
    disk.data[1][508]^=1;AppStore bad(&disk);Check(!bad.Mount()&&bad.Status()==AppStore::Corrupt,"both corrupt snapshots refused");
    disk.Format();Write32(disk.data[0]+32,0xFFFFFFFFU);Write32(disk.data[0]+508,CRC32(disk.data[0],508));
    AppStore malicious(&disk);Check(!malicious.Mount(),"unsupported superblock geometry rejected despite checksum");
    disk.Format();disk.available=false;AppStore absent(&disk);Check(!absent.Mount()&&absent.Status()==AppStore::NoDisk,"missing disk reported");
    if(failures){puts("FAIL storage C++ tests");return 1;}puts("PASS storage C++ tests: validation, capacity, COW, 34 torn installs, remount, corruption");return 0;
}
