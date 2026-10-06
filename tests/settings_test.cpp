// Actual kernel C++ settings code, cached/durable media and injected power loss.
#include <storage/settings.h>
#include <storage/appstore.h>
#include "cached_disk_fixture.h"
extern "C" int puts(const char*);
using namespace gtos::storage;
using namespace gtos::apps;

namespace {
int failures=0;
void Check(bool good,const char* message) { if(!good) { puts(message); failures++; } }
void Zero(uint8_t* p,uint32_t n) { for(uint32_t i=0;i<n;i++) p[i]=0; }
void Copy(uint8_t* a,const uint8_t* b,uint32_t n) { for(uint32_t i=0;i<n;i++) a[i]=b[i]; }
bool Equal(const uint8_t* a,const uint8_t* b,uint32_t n) {
    for(uint32_t i=0;i<n;i++) if(a[i]!=b[i]) return false;
    return true;
}
bool Same(const Settings& a,const Settings& b) {
    return a.locale==b.locale&&a.theme==b.theme;
}
const Settings Defaults={English,Dark}, ChineseLight={SimplifiedChinese,Light},
    ChineseDark={SimplifiedChinese,Dark}, EnglishLight={English,Light};

typedef gtos_test::CachedDisk MemoryDisk;

void Record(uint8_t* out,const Settings& settings,uint32_t generation) {
    Zero(out,512);
    const uint8_t magic[8]={'G','T','S','E','T','0','1',0}; Copy(out,magic,8);
    Write32(out+8,1); Write32(out+12,512); Write32(out+16,generation);
    Write32(out+20,(uint32_t)settings.locale); Write32(out+24,(uint32_t)settings.theme);
    Write32(out+508,CRC32(out,508));
}
void FixCRC(uint8_t* data) { Write32(data+508,CRC32(data,508)); }
void Package(uint8_t* out) {
    Zero(out,136);
    const uint8_t magic[8]={'G','T','A','P','P','0','1',0}; Copy(out,magic,8);
    Write32(out+8,1); Write32(out+12,128); Write32(out+16,136); Write32(out+20,1);
    out[32]='a'; out[56]='A'; out[80]='T'; Write32(out+120,PackageCRC(out,136));
}

void Basics(MemoryDisk& disk) {
    SettingsStore missing(0);
    Check(Same(missing.Current(),Defaults)&&missing.Status()==SettingsStore::Unloaded,
        "constructor provides explicit unloaded defaults");
    Check(!missing.Save(ChineseLight)&&missing.LastError()==SettingsStore::NotLoaded,
        "save requires successful load");
    Check(!missing.Load()&&missing.Status()==SettingsStore::Unavailable
        &&missing.LastError()==SettingsStore::NoDisk,"null disk unavailable");
    disk.Format(); SettingsStore store(&disk);
    Check(store.Load()&&store.Writable()&&!store.HasPersistedSettings()
        &&Same(store.Current(),Defaults)&&disk.writes==0,"blank extension loads defaults without writing");
    Check(store.Save(Defaults)&&disk.writes==0&&disk.flushes==0,"unchanged defaults never write");
    uint32_t appCRC=CRC32((uint8_t*)disk.data,AppStore::TotalSectors*512);
    disk.data[261][31]=0xCC; disk.FixtureDurable();
    Check(store.Save(ChineseLight)&&store.Generation()==1&&disk.lastSector==259
        &&store.HasPersistedSettings(),"first save commits first settings sector");
    Check(disk.writes==1&&disk.flushes==1,"single settings commit writes and flushes once");
    int writes=disk.writes,flushes=disk.flushes;
    Check(store.Save(ChineseLight)&&disk.writes==writes&&disk.flushes==flushes,
        "repeated selection avoids write wear");
    Check(store.Save(EnglishLight)&&store.Generation()==2&&disk.lastSector==260,
        "second save alternates settings sector");
    Check(CRC32((uint8_t*)disk.data,AppStore::TotalSectors*512)==appCRC
        &&disk.data[261][31]==0xCC,"settings never change app-store or further trailing sectors");
    disk.PowerLoss(); SettingsStore reboot(&disk);
    Check(reboot.Load()&&Same(reboot.Current(),EnglishLight)&&reboot.Generation()==2,
        "locale and theme survive power loss and fresh instance");
    Settings invalid={(Locale)2,Dark};
    Check(!reboot.Save(invalid)&&reboot.LastError()==SettingsStore::InvalidSettings
        &&reboot.Writable()&&disk.writes==0,"unknown locale rejected without disabling valid store");
    invalid.locale=English; invalid.theme=(Theme)0xFFFFFFFFU;
    Check(!reboot.Save(invalid)&&disk.writes==0,"unknown theme rejected without writing");
    Check(reboot.Save(ChineseDark)&&Same(reboot.Current(),ChineseDark),
        "valid save still works after invalid request");
    uint8_t before[1024],package[136]; Copy(before,disk.data[259],1024); Package(package);
    AppStore apps(&disk);
    Check(apps.Mount()&&apps.Install(package,136)&&apps.Uninstall("a"),
        "app-store still installs and uninstalls beside settings");
    Check(Equal(before,disk.data[259],1024),"app-store mutations never touch settings reservation");
    Check(reboot.Save(ChineseLight),"changed app directory does not falsely invalidate settings");
}

void Refusal(MemoryDisk& disk) {
    disk.Format(); disk.available=false; SettingsStore absent(&disk);
    Check(!absent.Load()&&absent.LastError()==SettingsStore::NoDisk&&disk.writes==0,"absent disk refused");
    for(uint32_t size=0;size<SettingsStore::RequiredSectors;size++) {
        disk.Format(); disk.sectors=size; SettingsStore small(&disk);
        Check(!small.Load()&&small.LastError()==SettingsStore::InsufficientCapacity
            &&!small.Save(ChineseLight)&&disk.writes==0,"undersized image never touched");
    }
    disk.Format(); disk.sectors=261; SettingsStore exact(&disk);
    Check(exact.Load()&&exact.Save(ChineseLight),"exact minimum 261-sector image supported");
    disk.Format(); disk.data[0][0]='X'; SettingsStore foreign(&disk);
    Check(!foreign.Load()&&foreign.LastError()==SettingsStore::UnownedDisk
        &&!foreign.Save(ChineseLight)&&disk.writes==0,"foreign superblock never formatted");
    disk.Format(); disk.data[0][508]^=1; SettingsStore badCRC(&disk);
    Check(!badCRC.Load()&&badCRC.LastError()==SettingsStore::CorruptAppStore
        &&disk.writes==0,"invalid superblock checksum blocks settings");
    disk.Format(); Write32(disk.data[0]+16,261); FixCRC(disk.data[0]); SettingsStore geometry(&disk);
    Check(!geometry.Load()&&disk.writes==0,"extension does not change GTSTOR1 app geometry");
    disk.Format(); disk.data[1][508]^=1; disk.data[2][508]^=1; SettingsStore dirs(&disk);
    Check(!dirs.Load()&&dirs.LastError()==SettingsStore::CorruptAppStore,
        "both invalid app directories block settings ownership");
    for(uint32_t sector=259;sector<=260;sector++) {
        disk.Format(); disk.data[sector][413]=0xA5;
        Record(disk.data[sector==259?260:259],ChineseLight,7);
        uint32_t crc=CRC32((uint8_t*)disk.data,sizeof(disk.data)); SettingsStore unknown(&disk);
        Check(!unknown.Load()&&unknown.Status()==SettingsStore::ReadOnly
            &&unknown.LastError()==SettingsStore::UnknownData
            &&Same(unknown.Current(),ChineseLight)&&unknown.HasPersistedSettings(),
            "unknown companion retains readable valid settings but disables all writes");
        Check(!unknown.Save(ChineseDark)&&disk.writes==0
            &&CRC32((uint8_t*)disk.data,sizeof(disk.data))==crc,"unknown trailing bytes never overwritten");
    }
    const uint32_t offsets[]={8,12,20,24,28,507};
    for(uint32_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        disk.Format(); Record(disk.data[259],ChineseLight,1); Record(disk.data[260],Defaults,0);
        disk.data[259][offsets[i]]=3; FixCRC(disk.data[259]); SettingsStore unsupported(&disk);
        Check(!unsupported.Load()&&unsupported.LastError()==SettingsStore::UnsupportedRecord
            &&Same(unsupported.Current(),Defaults)&&!unsupported.Save(ChineseLight)
            &&disk.writes==0,"unsupported versions, lengths, values and extensions are read-only");
    }
    disk.Format(); SettingsStore changed(&disk); Check(changed.Load(),"media-change setup loads");
    disk.data[260][13]=42;
    Check(!changed.Save(ChineseLight)&&changed.Status()==SettingsStore::ReadOnly
        &&changed.LastError()==SettingsStore::MediaChanged&&disk.writes==0,
        "new unknown data after load is rechecked before any write");
    disk.Format(); SettingsStore ownerChanged(&disk); ownerChanged.Load(); disk.data[0][0]='X';
    Check(!ownerChanged.Save(ChineseLight)&&disk.writes==0,"ownership checked again on every changed save");
}

void CorruptionAndGeneration(MemoryDisk& disk) {
    disk.Format(); Record(disk.data[259],Defaults,30); Record(disk.data[260],ChineseLight,31);
    disk.data[260][508]^=1; SettingsStore fallback(&disk);
    Check(fallback.Load()&&Same(fallback.Current(),Defaults)&&fallback.Generation()==30,
        "CRC failure falls back to valid older copy");
    Check(fallback.Save(ChineseDark)&&fallback.Generation()==31&&disk.lastSector==260,
        "known damaged inactive copy may be safely replaced");
    disk.data[259][508]^=1; disk.data[260][508]^=1; SettingsStore both(&disk);
    int writes=disk.writes;
    Check(!both.Load()&&both.LastError()==SettingsStore::CorruptRecords
        &&Same(both.Current(),Defaults)&&!both.Save(ChineseLight)&&disk.writes==writes,
        "two corrupt copies default read-only without automatic reset");
    disk.Format(); Record(disk.data[259],ChineseLight,0xFFFFFFFFU); SettingsStore wrap(&disk);
    Check(wrap.Load()&&wrap.Save(ChineseDark)&&wrap.Generation()==0,
        "uint32 generation rollover commits zero after UINT32_MAX");
    disk.PowerLoss(); SettingsStore wrapped(&disk);
    Check(wrapped.Load()&&Same(wrapped.Current(),ChineseDark)&&wrapped.Generation()==0,
        "serial comparison selects generation zero across rollover");
    Check(wrapped.Save(EnglishLight)&&wrapped.Generation()==1,"writes continue after generation rollover");
    disk.Format(); Record(disk.data[259],ChineseDark,0); Record(disk.data[260],ChineseLight,0xFFFFFFFFU);
    SettingsStore reversedWrap(&disk);
    Check(reversedWrap.Load()&&Same(reversedWrap.Current(),ChineseDark)&&reversedWrap.Generation()==0,
        "generation rollover comparison works in either copy orientation");
    disk.Format(); Record(disk.data[259],ChineseLight,4); Record(disk.data[260],Defaults,4);
    SettingsStore conflict(&disk);
    Check(!conflict.Load()&&conflict.LastError()==SettingsStore::AmbiguousGeneration
        &&Same(conflict.Current(),Defaults)&&disk.writes==0,"equal conflicting generations never guessed");
    Copy(disk.data[260],disk.data[259],512); SettingsStore equal(&disk);
    Check(equal.Load()&&Same(equal.Current(),ChineseLight),"identical equal-generation snapshots accepted");
    disk.Format(); Record(disk.data[259],ChineseLight,3); Record(disk.data[260],Defaults,0x80000003U);
    SettingsStore half(&disk);
    Check(!half.Load()&&half.LastError()==SettingsStore::AmbiguousGeneration
        &&!half.Save(ChineseDark)&&disk.writes==0,"exact serial half-range rejected read-only");
}

void Failures(MemoryDisk& disk,MemoryDisk& snapshot) {
    disk.Format(); SettingsStore initial(&disk); initial.Load(); initial.Save(ChineseLight);
    snapshot.Restore(disk);
    for(uint32_t cut=0;cut<=512;cut++) {
        disk.Restore(snapshot); SettingsStore interrupted(&disk); interrupted.Load();
        disk.failWrite=0; disk.tearBytes=cut;
        Check(!interrupted.Save(ChineseDark)&&interrupted.Status()==SettingsStore::Faulted
            &&interrupted.LastError()==SettingsStore::IOFailure
            &&Same(interrupted.Current(),ChineseLight),"injected torn commit reports uncertain I/O");
        Check(Equal(disk.data[259],snapshot.data[259],512),"all 513 torn prefixes preserve last valid copy");
        Check(!interrupted.Save(Defaults)&&disk.writes==1,"failed commit cannot retry without reload");
        disk.PowerLoss(); SettingsStore recovered(&disk); recovered.Load();
        bool committed=Read32(disk.data[260]+508)==CRC32(disk.data[260],508)
            &&Read32(disk.data[260]+16)==2;
        Check(recovered.HasPersistedSettings()
            &&Same(recovered.Current(),committed?ChineseDark:ChineseLight),
            "power-loss recovery uses a complete valid copy, never torn settings");
    }
    // A failed flush may leave either the old or the full new record durable.
    for(int committed=0;committed<2;committed++) {
        disk.Restore(snapshot); SettingsStore interrupted(&disk); interrupted.Load();
        // Establish the post-load barrier before targeting the commit's Flush.
        Check(interrupted.Save(ChineseLight),"flush-failure fixture establishes loaded durability");
        disk.failFlush=true; disk.flushDespiteFailure=committed;
        Check(!interrupted.Save(ChineseDark)&&Same(interrupted.Current(),ChineseLight)
            &&interrupted.Status()==SettingsStore::Faulted,"flush failure leaves live acknowledged settings unchanged");
        Check(Equal(disk.durable[259],snapshot.data[259],512),"flush failure never loses previous durable copy");
        disk.PowerLoss(); SettingsStore recovered(&disk);
        Check(recovered.Load()&&Same(recovered.Current(),committed?ChineseDark:ChineseLight),
            "remount resolves both permitted failed-flush outcomes");
    }
    for(int fault=0;fault<3;fault++) {
        disk.Restore(snapshot); SettingsStore interrupted(&disk); interrupted.Load();
        // A save reads app-store sectors0/1/2, both settings, then the readback.
        if(fault==0) disk.failRead=disk.reads+5;
        if(fault==1) disk.corruptRead=disk.reads+5;
        if(fault==2) { disk.failWrite=0; disk.tearBytes=100; disk.lieAboutWrite=true; }
        Check(!interrupted.Save(ChineseDark)&&interrupted.Status()==SettingsStore::Faulted
            &&Same(interrupted.Current(),ChineseLight),"readback failures or dishonest short writes detected");
        Check(Equal(disk.data[259],snapshot.data[259],512),"readback failure preserves old record");
        disk.PowerLoss(); SettingsStore recovered(&disk);
        Check(recovered.Load()&&Same(recovered.Current(),fault==2?ChineseLight:ChineseDark),
            "readback error requires remount to discover durable outcome");
    }
    for(int read=0;read<5;read++) {
        disk.Restore(snapshot); disk.failRead=read; SettingsStore unreadable(&disk);
        Check(!unreadable.Load()&&unreadable.Status()==SettingsStore::Faulted
            &&unreadable.LastError()==SettingsStore::IOFailure&&disk.writes==0,
            "every initial metadata read failure produces explicit I/O error without writes");
    }
    disk.Restore(snapshot); SettingsStore preflight(&disk); preflight.Load();
    disk.failRead=disk.reads+3;
    Check(!preflight.Save(ChineseDark)&&disk.writes==0,"save preflight read failure prevents mutation");
}

void TornExistingCopies(MemoryDisk& disk,MemoryDisk& snapshot) {
    disk.Format(); SettingsStore initial(&disk); initial.Load();
    initial.Save(ChineseLight); initial.Save(ChineseDark); snapshot.Restore(disk);
    for(uint32_t cut=0;cut<=512;cut++) {
        disk.Restore(snapshot); SettingsStore interrupted(&disk); interrupted.Load();
        disk.failWrite=0; disk.tearBytes=cut;
        Check(!interrupted.Save(EnglishLight)&&Same(interrupted.Current(),ChineseDark),
            "torn overwrite of older copy leaves live settings unchanged");
        Check(Equal(disk.data[260],snapshot.data[260],512),
            "torn overwrite preserves active second sector at every byte boundary");
        disk.PowerLoss(); SettingsStore recovered(&disk); recovered.Load();
        bool committed=Read32(disk.data[259]+508)==CRC32(disk.data[259],508)
            &&Read32(disk.data[259]+16)==3;
        Check(recovered.HasPersistedSettings()
            &&Same(recovered.Current(),committed?EnglishLight:ChineseDark),
            "mixed old/new inactive bytes never replace a valid active copy");
    }
    const uint32_t cuts[]={0,1,8,13,100,508,511,512};
    for(uint32_t i=0;i<sizeof(cuts)/sizeof(cuts[0]);i++) {
        disk.Format(); SettingsStore first(&disk); first.Load();
        disk.failWrite=0; disk.tearBytes=cuts[i];
        Check(!first.Save(ChineseLight)&&Same(first.Current(),Defaults),
            "interrupted first save retains session defaults");
        disk.PowerLoss(); SettingsStore recovered(&disk); recovered.Load();
        Check(Same(recovered.Current(),recovered.HasPersistedSettings()?ChineseLight:Defaults)
            &&disk.writes==0,"first-write recovery supplies valid settings or defaults without formatting");
    }
}

void DirtyCache(MemoryDisk& disk,SettingsStore& original) {
    disk.Format();
    Check(original.Load()&&original.Save(ChineseLight),"dirty-cache setup saves durable generation1");
    disk.failFlush=true;
    Check(!original.Save(ChineseDark)&&Same(original.Current(),ChineseLight),
        "dirty-cache setup leaves generation2 cached after failed flush");
    disk.ResetFaults(); // Deliberately retain device cache: this is NOT a reboot.
}

void ReloadDurability(MemoryDisk& disk) {
    for(int fresh=0;fresh<2;fresh++) {
        for(uint32_t cut=0;cut<=512;cut++) {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original);
            SettingsStore& reloaded=fresh?replacement:original;
            Check(reloaded.Load()&&Same(reloaded.Current(),ChineseDark)&&disk.flushes==0,
                "same/fresh reload reads cached generation2 without claiming durability");
            disk.failWrite=0; disk.tearBytes=cut;
            Check(!reloaded.Save(EnglishLight)&&disk.flushes==1,
                "reloaded record is flushed before every possible torn companion write");
            Check(Read32(disk.durable[260]+16)==2
                &&Read32(disk.durable[260]+508)==CRC32(disk.durable[260],508),
                "recovery barrier makes selected cached generation durable before reclaim");
            disk.PowerLoss(); SettingsStore recovered(&disk); recovered.Load();
            bool committed=Read32(disk.data[259]+508)==CRC32(disk.data[259],508)
                &&Read32(disk.data[259]+16)==3;
            Check(recovered.HasPersistedSettings()
                &&Same(recovered.Current(),committed?EnglishLight:ChineseDark),
                "reload without power cycle plus another torn save cannot lose all settings");
        }
        {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original); SettingsStore& reloaded=fresh?replacement:original;
            reloaded.Load();
            Check(reloaded.Save(ChineseDark)&&disk.writes==0&&disk.flushes==1,
                "unchanged recovered value needs a durability barrier but no sector writes");
            Check(reloaded.Save(ChineseDark)&&disk.writes==0&&disk.flushes==1,
                "repeated unchanged known-durable saves need no additional barrier");
            disk.PowerLoss(); SettingsStore recovered(&disk);
            Check(recovered.Load()&&Same(recovered.Current(),ChineseDark),
                "acknowledged unchanged recovered settings survive power loss");
        }
        for(int unchanged=0;unchanged<2;unchanged++) for(int durable=0;durable<2;durable++) {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original); SettingsStore& reloaded=fresh?replacement:original;
            reloaded.Load(); disk.failFlush=true; disk.flushDespiteFailure=durable;
            Check(!reloaded.Save(unchanged?ChineseDark:EnglishLight)
                &&reloaded.Status()==SettingsStore::Faulted
                &&reloaded.LastError()==SettingsStore::IOFailure
                &&Same(reloaded.Current(),ChineseDark)&&disk.writes==0&&disk.flushes==1,
                "failed recovery barrier never writes companion or acknowledges success");
            Check(!reloaded.Save(EnglishLight)&&disk.writes==0&&disk.flushes==1,
                "failed recovery barrier requires another Load before retry");
            disk.PowerLoss(); SettingsStore recovered(&disk);
            Check(recovered.Load()&&Same(recovered.Current(),durable?ChineseDark:ChineseLight),
                "either failed-barrier durability outcome retains previously saved settings");
        }
        {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original); SettingsStore& reloaded=fresh?replacement:original;
            reloaded.Load(); disk.data[259][79]^=1;
            Check(!reloaded.Save(ChineseDark)&&reloaded.LastError()==SettingsStore::MediaChanged
                &&disk.writes==0&&disk.flushes==0,
                "unchanged recovered save rechecks both sectors before flushing changed media");
        }
        {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original); SettingsStore& reloaded=fresh?replacement:original;
            reloaded.Load(); disk.data[0][0]='X';
            Check(!reloaded.Save(ChineseDark)&&reloaded.LastError()==SettingsStore::UnownedDisk
                &&disk.writes==0&&disk.flushes==0,
                "unchanged recovered save rechecks ownership before recovery barrier");
        }
        {
            SettingsStore original(&disk),replacement(&disk);
            DirtyCache(disk,original); SettingsStore& reloaded=fresh?replacement:original;
            reloaded.Load(); disk.failRead=disk.reads+4;
            Check(!reloaded.Save(ChineseDark)&&reloaded.Status()==SettingsStore::Faulted
                &&disk.writes==0&&disk.flushes==0,
                "recovery preflight read failure cannot flush or write unverified media");
        }
    }
}
}

int main() {
    MemoryDisk disk,snapshot;
    Basics(disk); Refusal(disk); CorruptionAndGeneration(disk); Failures(disk,snapshot);
    TornExistingCopies(disk,snapshot);
    ReloadDurability(disk);
    if(failures) { puts("FAIL settings C++ tests"); return 1; }
    puts("PASS settings C++ tests: safety, CRC, wrap, 2052 torn updates, reload-cache durability, flush/readback failures");
    return 0;
}
