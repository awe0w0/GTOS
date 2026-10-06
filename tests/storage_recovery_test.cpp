// The kernel store is exercised against cached and durable media separately.
#include "cached_disk_fixture.h"
extern "C" int puts(const char*);
using namespace gtos::storage;
using namespace gtos::apps;
using gtos_test::Zero;
using gtos_test::Copy;
typedef gtos_test::CachedDisk MemoryDisk;
static int failures;
static void Check(bool value,const char* message){if(!value){puts(message);++failures;}}
static void FixCRC(uint8_t* data){Write32(data+508,CRC32(data,508));}
static void Package(uint8_t* out){
    Zero(out,136);const uint8_t magic[8]={'G','T','A','P','P','0','1',0};Copy(out,magic,8);
    Write32(out+8,1);Write32(out+12,128);Write32(out+16,136);Write32(out+20,1);
    out[32]='a';out[56]='A';out[80]='T';Write32(out+120,PackageCRC(out,136));
}
class DirectoryDisk : public MemoryDisk {
public:
    uint32_t syncCalls, failSync;
    DirectoryDisk():syncCalls(0),failSync(0) {}
    bool Flush() {
        ++syncCalls;
        if (failSync && syncCalls==failSync) return false;
        return MemoryDisk::Flush();
    }
};
static void Pair(DirectoryDisk& disk,uint8_t* a,uint8_t* b) {
    disk.Format();disk.syncCalls=disk.failSync=0;
    Package(a);Package(b);b[32]='b';b[56]='B';Write32(b+120,PackageCRC(b,136));
    AppStore apps(&disk);
    Check(apps.Mount()&&apps.Install(a,136)&&apps.Install(b,136),"seed two acknowledged apps");
}
static bool OnlyB(AppStore& store) {return store.Count()==1&&store.Get(0)&&store.Get(0)->id[0]=='b';}
int main() {
    static DirectoryDisk disk;
    uint8_t a[136],b[136];
    for(uint32_t fresh=0;fresh<2;++fresh)for(uint32_t cut=0;cut<=512;++cut) {
        Pair(disk,a,b);
        AppStore first(&disk);Check(first.Mount(),"load acknowledged pair");
        disk.failSync=disk.syncCalls+2; // recovery barrier succeeds; publication flush fails.
        Check(!first.Uninstall("a")&&!first.Mounted(),"failed publication requires remount");
        disk.failSync=0;disk.ResetFaults();
        AppStore second(&disk);AppStore* active=fresh?&second:&first;
        Check(active->Mount()&&OnlyB(*active),"recover newer cache-only directory without power cycle");
        disk.failWrite=0;disk.tearBytes=cut;
        Check(!active->Uninstall("b"),"next metadata write is interrupted");
        disk.PowerLoss();AppStore reboot(&disk);
        Check(reboot.Mount(),"at least one durable directory survives");
        Check(OnlyB(reboot)||reboot.Count()==0,"recover complete newer head, never old A-only snapshot");
        if(OnlyB(reboot)){uint8_t bytes[136];uint32_t length=0;Check(reboot.Read("b",bytes,136,&length)&&length==136,"retained payload remains readable");}
    }
    Pair(disk,a,b);AppStore first(&disk);Check(first.Mount(),"barrier case mount");
    disk.failSync=disk.syncCalls+2;Check(!first.Uninstall("a"),"seed nondurable head");
    disk.failSync=0;disk.ResetFaults();AppStore recovered(&disk);Check(recovered.Mount(),"barrier case reload");
    disk.failSync=disk.syncCalls+1;int writes=disk.writes;
    Check(!recovered.Uninstall("b")&&disk.writes==writes,"failed recovery flush never overwrites companion");
    disk.failSync=0;disk.PowerLoss();AppStore safe(&disk);Check(safe.Mount()&&safe.Count()==2,"barrier failure preserves acknowledged pair");
    Pair(disk,a,b);Write32(disk.data[1]+8,0);FixCRC(disk.data[1]);
    Write32(disk.data[2]+8,0x80000000U);FixCRC(disk.data[2]);AppStore ambiguous(&disk);
    Check(!ambiguous.Mount()&&ambiguous.Status()==AppStore::Corrupt,"half-range generations are ambiguous");
    if(!failures)puts("PASS app-store recovery: 1026 cached-remount tears, barrier failures and half-range rejection");
    return failures?1:0;
}
