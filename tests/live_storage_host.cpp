// Tests the actual RAM block device, AppStore, SettingsStore and ATA service.
// ATA tests replace only port implementations with a counted hardware spy.
#include <storage/liveblockdevice.h>
#include <storage/appstore.h>
#include <storage/settings.h>
#include <apps/package.h>
#ifndef GTOS_LIVE_RAM_ONLY
#include <drivers/ata.h>
#endif
extern "C" int puts(const char*);
using namespace gtos::storage;
using namespace gtos::apps;
static uint32_t checks=0, failures=0;
static void Check(bool condition,const char* message) {
    ++checks;
    if (!condition) { ++failures; puts(message); }
}
static bool Equal(const uint8_t* a,const uint8_t* b,uint32_t bytes) {
    for (uint32_t i=0;i<bytes;++i) if (a[i]!=b[i]) return false;
    return true;
}
static void Copy(uint8_t* a,const uint8_t* b,uint32_t bytes) {
    for (uint32_t i=0;i<bytes;++i) a[i]=b[i];
}
static void Package(uint8_t* bytes) {
    for (uint32_t i=0;i<136;++i) bytes[i]=0;
    const uint8_t magic[8]={'G','T','A','P','P','0','1',0};
    for (uint32_t i=0;i<8;++i) bytes[i]=magic[i];
    Write32(bytes+8,1); Write32(bytes+12,128); Write32(bytes+16,136);
    Write32(bytes+20,1);
    const char id[]="live_test", title[]="Live test", summary[]="RAM-only package";
    Copy(bytes+32,(const uint8_t*)id,sizeof(id));
    Copy(bytes+56,(const uint8_t*)title,sizeof(title));
    Copy(bytes+80,(const uint8_t*)summary,sizeof(summary));
    // Opcode zero is the real VM HALT instruction.
    Write32(bytes+120,PackageCRC(bytes,136));
}
static void RamContract() {
    LiveBlockDevice disk;
    Check(disk.Identify() && disk.SectorCount()==261,"live geometry");
    // Independent Python/zlib oracle for the exact tools/disk.py wire format.
    uint8_t format[512];
    Check(disk.ReadSector(0,format) && Read32(format+508)==0xE5E1AAA3U,
          "independent superblock CRC oracle");
    for(uint32_t i=36;i<508;++i) Check(format[i]==0,"reserved superblock bytes are zero");
    for(uint32_t sector=1;sector<=2;++sector) {
        Check(disk.ReadSector(sector,format) && Read32(format+508)==0xAC59DC4CU,
              "independent empty directory CRC oracle");
        for(uint32_t i=8;i<508;++i) Check(format[i]==0,"empty directory bytes are zero");
    }
    AppStore store(&disk);
    Check(store.Mount() && store.Count()==0 && store.Generation()==0,"real validator accepts RAM format");
    SettingsStore preferences(&disk);
    Check(preferences.Load() && preferences.Writable() && !preferences.HasPersistedSettings()
          && preferences.Current().locale==English && preferences.Current().theme==Dark,
          "real settings loads blank RAM extension");
    uint8_t package[136], retrieved[136], before[512], after[512];
    Package(package);
    Check(ValidatePackage(package,sizeof(package))==PackageOK,"independent test package is valid");
    Check(store.Install(package,sizeof(package)) && store.Count()==1 && store.Generation()==1,
          "actual app installation commits RAM payload and directory");
    uint32_t length=0;
    Check(store.Read("live_test",retrieved,sizeof(retrieved),&length) && length==sizeof(package)
          && Equal(package,retrieved,sizeof(package)),"actual package read verifies bytes");
    uint8_t altered[136]; Copy(altered,package,sizeof(package)); altered[135]^=1;
    Check(!store.Install(altered,sizeof(altered)) && store.Status()==AppStore::InvalidPackage,
          "RAM store rejects damaged package");
    Check(disk.ReadSector(0,before),"read immutable format before settings");
    const Settings chineseLight={SimplifiedChinese,Light};
    Check(preferences.Save(chineseLight) && preferences.Generation()==1
          && preferences.HasPersistedSettings(),"actual settings save commits RAM record");
    Check(disk.ReadSector(0,after) && Equal(before,after,512),"settings preserves RAM superblock");
    SettingsStore reloaded(&disk);
    Check(reloaded.Load() && reloaded.Current().locale==SimplifiedChinese
          && reloaded.Current().theme==Light,"settings reload reads real RAM commit");
    Check(store.Mount() && store.Count()==1,"RAM remount retains session application");
    Check(store.Uninstall("live_test") && store.Count()==0 && store.Generation()==2,
          "actual RAM uninstall commits directory");
    Check(store.Install(package,sizeof(package)) && store.Mount() && store.Count()==1,
          "RAM reinstall after removal and reload");
    LiveBlockDevice reboot;
    AppStore fresh(&reboot); SettingsStore defaults(&reboot);
    Check(fresh.Mount() && fresh.Count()==0 && fresh.Generation()==0 && defaults.Load()
          && defaults.Current().locale==English && defaults.Current().theme==Dark
          && !defaults.HasPersistedSettings(),"fresh boot forgets apps and settings");
    // Exhaust every legal sector; exactly 512 bytes change/read, no prefix/suffix.
    for (uint32_t sector=0;sector<261;++sector) {
        uint8_t source[512], guarded[514];
        for(uint32_t i=0;i<512;++i) source[i]=(uint8_t)(sector*37U+i*13U);
        for(uint32_t i=0;i<514;++i) guarded[i]=0xA5;
        Check(disk.WriteSector(sector,source) && disk.ReadSector(sector,guarded+1)
              && guarded[0]==0xA5 && guarded[513]==0xA5 && Equal(source,guarded+1,512),
              "bounded sector round trip");
    }
    Check(disk.Flush(),"RAM flush succeeds without hardware");
    const uint32_t rejected[]={261,262,0x7FFFFFFFU,0xFFFFFFFFU};
    for(uint32_t n=0;n<sizeof(rejected)/sizeof(rejected[0]);++n) {
        for(uint32_t i=0;i<512;++i) after[i]=0x5C;
        Check(!disk.ReadSector(rejected[n],after),"out-of-range RAM read denied");
        for(uint32_t i=0;i<512;++i) Check(after[i]==0x5C,"bad RAM read leaves output unchanged");
        Check(!disk.WriteSector(rejected[n],(const uint8_t*)1),"bad RAM write rejects before source touch");
    }
    Check(!disk.ReadSector(0,0) && !disk.WriteSector(0,0),"null RAM pointers denied");
    Check(disk.ReadSector(260,before),"capture final legal RAM sector");
    Check(!disk.WriteSector(261,(const uint8_t*)1) && disk.ReadSector(260,after)
          && Equal(before,after,512),"overflow write leaves last valid sector intact");
}
#ifndef GTOS_LIVE_RAM_ONLY
namespace gtos { namespace hardwarecommunication {
static uint32_t reads=0,writes=0,dataWrites=0,writeCommands=0,flushCommands=0,identifyWord=0;
Port::Port(uint16_t number):portnumber(number) {}
Port::~Port() {}
Port8Bit::Port8Bit(uint16_t number):Port(number) {}
Port8Bit::~Port8Bit() {}
void Port8Bit::Write(uint8_t value) {
    ++writes;
    if ((portnumber&7U)==7U) {
        if(value==0xEC) identifyWord=0;
        if(value==0x30) ++writeCommands;
        if(value==0xE7) ++flushCommands;
    }
}
uint8_t Port8Bit::Read() { ++reads; return (portnumber&7U)==7U?8:0; }
Port16Bit::Port16Bit(uint16_t number):Port(number) {}
Port16Bit::~Port16Bit() {}
void Port16Bit::Write(uint16_t) { ++writes; ++dataWrites; }
uint16_t Port16Bit::Read() {
    ++reads;
    const uint32_t word=identifyWord++;
    if(word==49) return 1U<<9;
    if(word==60) return 261;
    if(word==61) return 0;
    return word>=27 && word<47?0x2020:0;
}
} }
static void AtaContract() {
    using namespace gtos::drivers;
    using namespace gtos::hardwarecommunication;
    AdvancedTechnologyAttachment existing(0x1F0,true);
    uint8_t sector[512]; for(uint32_t i=0;i<512;++i) sector[i]=(uint8_t)i;
    Check(!AdvancedTechnologyAttachment::WritesDisabledForLiveBoot(),"ordinary ATA initially writable");
    Check(existing.Identify() && existing.SectorCount()==261,"actual ordinary ATA identify via spy");
    Check(existing.Write28(0,sector) && writeCommands==1 && dataWrites==256,
          "ordinary ATA Write28 still emits one real full-sector command");
    Check(existing.Flush() && flushCommands==1,"ordinary ATA flush preserved");
    AdvancedTechnologyAttachment::DisableWritesForLiveBoot();
    const uint32_t readBaseline=reads,writeBaseline=writes;
    Check(!existing.Write28(0,sector) && existing.LastError()==AdvancedTechnologyAttachment::WriteProtected,
          "live blocks existing instance Write28");
    BlockDevice* throughInterface=&existing;
    Check(!throughInterface->WriteSector(0,sector)
          && existing.LastError()==AdvancedTechnologyAttachment::WriteProtected,
          "live blocks virtual WriteSector");
    Check(!existing.Flush() && existing.LastError()==AdvancedTechnologyAttachment::WriteProtected,
          "live blocks existing instance Flush");
    Check(!existing.Write28(0xFFFFFFFFU,(const uint8_t*)1,-1)
          && existing.LastError()==AdvancedTechnologyAttachment::WriteProtected,
          "live guard precedes arguments and source memory");
    Check(reads==readBaseline && writes==writeBaseline,"blocked calls perform zero port accesses");
    AdvancedTechnologyAttachment fresh(0x170,false);
    Check(!fresh.Write28(0,sector) && !fresh.Flush()
          && fresh.LastError()==AdvancedTechnologyAttachment::WriteProtected,
          "global gate blocks fresh controller without device probe");
    Check(reads==readBaseline && writes==writeBaseline,"new object and blocked operations perform no I/O");
    Check(existing.Identify(),"ordinary read-side identify remains valid after locking");
    const uint32_t afterIdentifyReads=reads,afterIdentifyWrites=writes;
    AdvancedTechnologyAttachment::DisableWritesForLiveBoot();
    Check(!existing.Write28(0,sector) && !existing.Flush() && !fresh.Write28(0,sector)
          && AdvancedTechnologyAttachment::WritesDisabledForLiveBoot(),"identify and repeated lock cannot unlock");
    Check(reads==afterIdentifyReads && writes==afterIdentifyWrites
          && writeCommands==1 && flushCommands==1,"no write/flush commands after live lock");
}
#endif
int main() {
    RamContract();
#ifndef GTOS_LIVE_RAM_ONLY
    AtaContract();
#endif
    Check(checks>=2300,"expected contract cases exercised");
    char digits[11], count[11]; uint32_t number=checks, used=0;
    do { digits[used++]=(char)('0'+number%10); number/=10; } while(number);
    for(uint32_t i=0;i<used;++i) count[i]=digits[used-1-i];
    count[used]=0; puts("LIVE STORAGE CHECKS"); puts(count);
    puts(failures?"LIVE STORAGE CONTRACT FAIL":"LIVE STORAGE CONTRACT PASS");
    return failures?1:0;
}
