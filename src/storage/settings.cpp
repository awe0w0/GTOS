#include <storage/settings.h>
#include <storage/appstore.h>

using namespace gtos::storage;
using gtos::apps::CRC32;
using gtos::apps::Read32;
using gtos::apps::Write32;

static_assert((uint32_t)SettingsStore::FirstSector==(uint32_t)AppStore::TotalSectors,
    "Settings sectors must follow, never overlap, the app-store extent");

namespace {
const uint8_t Magic[8]={'G','T','S','E','T','0','1',0};
enum RecordKind { Blank, Valid, Damaged, Foreign, Unsupported };

void Zero(uint8_t* data,uint32_t count) {
    for(uint32_t i=0;i<count;i++) data[i]=0;
}
void Copy(uint8_t* to,const uint8_t* from,uint32_t count) {
    for(uint32_t i=0;i<count;i++) to[i]=from[i];
}
bool Equal(const uint8_t* a,const uint8_t* b,uint32_t count) {
    for(uint32_t i=0;i<count;i++) if(a[i]!=b[i]) return false;
    return true;
}
bool IsZero(const uint8_t* data,uint32_t count) {
    for(uint32_t i=0;i<count;i++) if(data[i]) return false;
    return true;
}
bool Supported(const Settings& settings) {
    return (settings.locale==English||settings.locale==SimplifiedChinese)
        &&(settings.theme==Dark||settings.theme==Light);
}
RecordKind Classify(const uint8_t* data) {
    if(IsZero(data,512)) return Blank;
    if(!Equal(data,Magic,8)) return Foreign;
    if(Read32(data+8)!=SettingsStore::FormatVersion||Read32(data+12)!=512)
        return Unsupported;
    // Nonzero extensions or unknown values could belong to a future writer.
    // Never "repair" them, even if another copy is supported and valid.
    if(Read32(data+20)>1||Read32(data+24)>1||!IsZero(data+28,480))
        return Unsupported;
    return Read32(data+508)==CRC32(data,508)?Valid:Damaged;
}
void Encode(uint8_t* data,const Settings& settings,uint32_t generation) {
    Zero(data,512);
    Copy(data,Magic,8);
    Write32(data+8,SettingsStore::FormatVersion);
    Write32(data+12,512);
    Write32(data+16,generation);
    Write32(data+20,(uint32_t)settings.locale);
    Write32(data+24,(uint32_t)settings.theme);
    Write32(data+508,CRC32(data,508));
}
}

SettingsStore::SettingsStore(BlockDevice* device):disk(device),state(Unloaded),
    error(NotLoaded),active(0),generation(0),hasRecord(false),durabilityKnown(false) {
    current.locale=English; current.theme=Dark;
    Zero((uint8_t*)copies,sizeof(copies));
}

bool SettingsStore::Fail(State next,Error reason) {
    if(next==Faulted) durabilityKnown=false;
    state=next; error=reason; return false;
}

bool SettingsStore::CheckOwner() {
    if(!disk||!disk->Identify()) return Fail(Unavailable,NoDisk);
    if(disk->SectorCount()<RequiredSectors) return Fail(Unavailable,InsufficientCapacity);
    // Reuse the actual app-store validator, including exact geometry, reserved
    // bytes, checksums and at least one valid app directory. Never write sector0.
    AppStore owner(disk);
    if(!owner.Mount()) {
        switch(owner.Status()) {
            case AppStore::NoDisk:return Fail(Unavailable,NoDisk);
            case AppStore::IOFailure:return Fail(Faulted,IOFailure);
            case AppStore::Corrupt:return Fail(ReadOnly,CorruptAppStore);
            default:return Fail(Unavailable,UnownedDisk);
        }
    }
    return true;
}

bool SettingsStore::Load() {
    current.locale=English; current.theme=Dark;
    active=generation=0; hasRecord=false; durabilityKnown=false;
    state=Unloaded; error=NotLoaded;
    Zero((uint8_t*)copies,sizeof(copies));
    if(!CheckOwner()) return false;
    RecordKind kinds[CopyCount];
    for(uint32_t i=0;i<CopyCount;i++) {
        if(!disk->ReadSector(FirstSector+i,copies[i])) return Fail(Faulted,IOFailure);
        kinds[i]=Classify(copies[i]);
    }
    if(kinds[0]==Valid&&kinds[1]==Valid) {
        uint32_t g0=Read32(copies[0]+16),g1=Read32(copies[1]+16),delta=g1-g0;
        // RFC1982-style unsigned serial comparison. The exact half-range and
        // conflicting equal generations have no defensible newest snapshot.
        if(delta==0x80000000U||(!delta&&!Equal(copies[0],copies[1],512)))
            return Fail(ReadOnly,AmbiguousGeneration);
        active=delta&&delta<0x80000000U?1:0;
        hasRecord=true;
    } else if(kinds[0]==Valid||kinds[1]==Valid) {
        active=kinds[0]==Valid?0:1;
        hasRecord=true;
    }
    if(hasRecord) {
        generation=Read32(copies[active]+16);
        current.locale=(Locale)Read32(copies[active]+20);
        current.theme=(Theme)Read32(copies[active]+24);
    }
    // Either unknown sector disables all writes, including the other sector.
    for(uint32_t i=0;i<CopyCount;i++) {
        if(kinds[i]==Foreign) return Fail(ReadOnly,UnknownData);
        if(kinds[i]==Unsupported) return Fail(ReadOnly,UnsupportedRecord);
    }
    if(!hasRecord&&(kinds[0]!=Blank||kinds[1]!=Blank))
        return Fail(ReadOnly,CorruptRecords);
    state=Ready; error=NoError; return true;
}

bool SettingsStore::Save(const Settings& settings) {
    if(!Supported(settings)) { error=InvalidSettings; return false; }
    if(state!=Ready) return false;
    bool unchanged=settings.locale==current.locale&&settings.theme==current.theme;
    if(unchanged&&(!hasRecord||durabilityKnown)) {
        error=NoError; return true;
    }
    if(!CheckOwner()) return false;
    uint8_t record[512],verify[512];
    // Reject changed media/another writer instead of trusting cached ownership.
    // The app directory may change independently; settings copies may not.
    for(uint32_t i=0;i<CopyCount;i++) {
        if(!disk->ReadSector(FirstSector+i,verify)) return Fail(Faulted,IOFailure);
        if(!Equal(verify,copies[i],512)) return Fail(ReadOnly,MediaChanged);
    }
    // A reload, including a fresh instance, can select a valid newer record
    // still present only in a device's dirty cache after a failed Flush. Before
    // reusing its companion (possibly the only durable copy), make the selected
    // record durable. Unchanged Save must not falsely acknowledge cached data.
    if(hasRecord&&!durabilityKnown) {
        if(!disk->Flush()) return Fail(Faulted,IOFailure);
        durabilityKnown=true;
    }
    if(unchanged) { error=NoError; return true; }
    uint32_t target=hasRecord?1-active:0;
    Encode(record,settings,generation+1);
    // Only the inactive copy is changed. The last acknowledged settings remain
    // intact even if this write tears, Flush fails, or the readback cannot run.
    if(!disk->WriteSector(FirstSector+target,record)||!disk->Flush()
        ||!disk->ReadSector(FirstSector+target,verify)||!Equal(record,verify,512))
        return Fail(Faulted,IOFailure);
    Copy(copies[target],record,512);
    current=settings; active=target; generation++; hasRecord=true; durabilityKnown=true;
    state=Ready; error=NoError; return true;
}

const char* SettingsStore::StatusText() const {
    switch(error) {
        case NoError:return hasRecord?(durabilityKnown?"Settings saved":"Settings loaded")
            :"Using default settings";
        case NotLoaded:return "Settings not loaded";
        case NoDisk:return "No settings disk";
        case InsufficientCapacity:return "Settings disk too small";
        case UnownedDisk:return "Disk not GTOS formatted";
        case CorruptAppStore:return "App-store metadata corrupt";
        case IOFailure:return "Settings I/O failed; reload required";
        case UnknownData:return "Settings area contains unknown data";
        case UnsupportedRecord:return "Unsupported settings record";
        case CorruptRecords:return "No valid settings copy";
        case AmbiguousGeneration:return "Settings generations ambiguous";
        case InvalidSettings:return "Unsupported settings value";
        case MediaChanged:return "Settings media changed; reload required";
    }
    return "Unknown settings error";
}
