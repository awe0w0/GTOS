#include <storage/appstore.h>
using namespace gtos::storage;
using namespace gtos::apps;
static void Zero(uint8_t* p,uint32_t n) { for (uint32_t i=0;i<n;i++) p[i]=0; }
static void Copy(uint8_t* to,const uint8_t* from,uint32_t n) { for (uint32_t i=0;i<n;i++) to[i]=from[i]; }
static bool Equal(const uint8_t* a,const uint8_t* b,uint32_t n) {
    for (uint32_t i=0;i<n;i++) if (a[i]!=b[i]) return false;
    return true;
}
static bool SameID(const char* a,const char* b) {
    if (!a||!b) return false;
    for (uint32_t i=0;i<24;i++) { if(a[i]!=b[i]) return false; if(!a[i]) return true; }
    return false;
}
static bool ValidField(const uint8_t* p,uint32_t n,bool id) {
    bool end=false;
    if (!p[0]) return false;
    for (uint32_t i=0;i<n;i++) {
        uint8_t c=p[i];
        if (!c) { end=true; continue; }
        if (end || c<32 || c>126) return false;
        if (id && !(c>='a'&&c<='z') && !(c>='0'&&c<='9') && c!='_' && c!='-') return false;
    }
    return end;
}
AppStore::AppStore(BlockDevice* d):disk(d),mounted(false),active(0),generation(0),count(0),status(NotMounted) {
    valid[0]=valid[1]=false;
    Zero((uint8_t*)entries,sizeof(entries)); Zero((uint8_t*)dirs,sizeof(dirs));
}
bool AppStore::Fail(Error e) { status=e; return false; }
bool AppStore::ValidDirectory(const uint8_t* s) const {
    const uint8_t magic[8]={'G','T','D','I','R','0','1',0};
    if (!Equal(s,magic,8) || Read32(s+12)>MaxApps || Read32(s+508)!=CRC32(s,508)) return false;
    for (uint32_t i=16;i<24;i++) if (s[i]) return false;
    for (uint32_t i=504;i<508;i++) if (s[i]) return false;
    uint32_t n=Read32(s+12),slots=0;
    for (uint32_t i=0;i<MaxApps;i++) {
        const uint8_t* e=s+24+i*60;
        if (i>=n) { for (uint32_t j=0;j<60;j++) if (e[j]) return false; continue; }
        uint32_t slot=Read32(e+48),len=Read32(e+52);
        if (!ValidField(e,24,true)||!ValidField(e+24,24,false)||slot>=SlotCount
            ||len<136||len>PackageLimit||(len-128)%8||(slots&(1U<<slot))) return false;
        slots|=1U<<slot;
        for (uint32_t j=0;j<i;j++) if (SameID((const char*)e,(const char*)(s+24+j*60))) return false;
    }
    return true;
}
void AppStore::DecodeDirectory() {
    const uint8_t* s=dirs[active]; generation=Read32(s+8); count=Read32(s+12);
    Zero((uint8_t*)entries,sizeof(entries));
    for (uint32_t i=0;i<count;i++) {
        const uint8_t* p=s+24+60*i;
        Copy((uint8_t*)entries[i].id,p,24); Copy((uint8_t*)entries[i].title,p+24,24);
        entries[i].slot=Read32(p+48); entries[i].length=Read32(p+52); entries[i].checksum=Read32(p+56);
    }
}
bool AppStore::Mount() {
    mounted=false; count=0; valid[0]=valid[1]=false;
    if (!disk||!disk->Identify()) return Fail(NoDisk);
    if (disk->SectorCount()<TotalSectors) return Fail(NotFormatted);
    uint8_t s[512];
    if (!disk->ReadSector(0,s)) return Fail(IOFailure);
    const uint8_t magic[8]={'G','T','S','T','O','R','1',0};
    if (!Equal(s,magic,8)) return Fail(NotFormatted);
    if (Read32(s+8)!=1||Read32(s+12)!=512||Read32(s+16)!=TotalSectors
        ||Read32(s+20)!=SlotCount||Read32(s+24)!=SlotSectors||Read32(s+28)!=MaxApps
        ||Read32(s+32)!=DataStart||Read32(s+508)!=CRC32(s,508)) return Fail(Corrupt);
    for (uint32_t i=36;i<508;i++) if (s[i]) return Fail(Corrupt);
    for (uint32_t i=0;i<2;i++) {
        if (!disk->ReadSector(i+1,dirs[i])) return Fail(IOFailure);
        valid[i]=ValidDirectory(dirs[i]);
    }
    if (!valid[0]&&!valid[1]) return Fail(Corrupt);
    if (valid[0]&&valid[1]) {
        uint32_t g0=Read32(dirs[0]+8),g1=Read32(dirs[1]+8);
        // Serial-number arithmetic also handles generation wrap-around.
        uint32_t delta=g1-g0;
        if (delta==0x80000000U || (g0==g1&&!Equal(dirs[0],dirs[1],512))) return Fail(Corrupt);
        active=delta&&delta<0x80000000U?1:0;
    } else active=valid[0]?0:1;
    DecodeDirectory(); mounted=true; status=OK; return true;
}
bool AppStore::Commit(const AppInfo* next,uint32_t nextCount) {
    uint8_t sector[512],verify[512]; Zero(sector,512);
    const uint8_t magic[8]={'G','T','D','I','R','0','1',0}; Copy(sector,magic,8);
    Write32(sector+8,generation+1); Write32(sector+12,nextCount);
    for (uint32_t i=0;i<nextCount;i++) {
        uint8_t* p=sector+24+i*60;
        Copy(p,(const uint8_t*)next[i].id,24); Copy(p+24,(const uint8_t*)next[i].title,24);
        Write32(p+48,next[i].slot); Write32(p+52,next[i].length); Write32(p+56,next[i].checksum);
    }
    Write32(sector+508,CRC32(sector,508));
    uint32_t target=1-active;
    // A remount may read a valid newer directory still only in the device's
    // volatile cache after an earlier failed flush. Make that recovered head
    // durable before reclaiming its companion, including metadata-only removal.
    if (!disk->Flush()) { mounted=false; return Fail(IOFailure); }
    // Only publish after all package sectors have already been flushed.
    if (!disk->WriteSector(target+1,sector)||!disk->Flush()||!disk->ReadSector(target+1,verify)
        ||!Equal(sector,verify,512)) {
        // Durability is uncertain: force a remount before any further mutation.
        mounted=false; return Fail(IOFailure);
    }
    Copy(dirs[target],sector,512); valid[target]=true; active=target;
    DecodeDirectory(); status=OK; return true;
}
bool AppStore::Install(const uint8_t* package,uint32_t length) {
    if (!mounted) return Fail(NotMounted);
    PackageInfo info;
    if (ValidatePackage(package,length,&info)!=PackageOK) return Fail(InvalidPackage);
    uint32_t index=count;
    for (uint32_t i=0;i<count;i++) if (SameID(entries[i].id,info.id)) index=i;
    if (index==count&&count==MaxApps) return Fail(Full);
    // Keep payloads referenced by EITHER directory, so a torn commit can recover.
    uint32_t used=0;
    for (uint32_t d=0;d<2;d++) if (valid[d]) {
        uint32_t n=Read32(dirs[d]+12);
        for (uint32_t i=0;i<n;i++) used|=1U<<Read32(dirs[d]+24+60*i+48);
    }
    uint32_t slot=0;
    while (slot<SlotCount&&(used&(1U<<slot))) slot++;
    if (slot==SlotCount) return Fail(Full);
    uint8_t sector[512],verify[512];
    for (uint32_t i=0;i<SlotSectors;i++) {
        Zero(sector,512);
        uint32_t off=i*512,n=off<length?length-off:0;
        if (n>512) n=512;
        if (n) Copy(sector,package+off,n);
        if (!disk->WriteSector(DataStart+slot*SlotSectors+i,sector)) return Fail(IOFailure);
    }
    if (!disk->Flush()) return Fail(IOFailure);
    for (uint32_t i=0;i<SlotSectors;i++) {
        if (!disk->ReadSector(DataStart+slot*SlotSectors+i,verify)) return Fail(IOFailure);
        uint32_t off=i*512;
        for (uint32_t j=0;j<512;j++) if (verify[j]!=(off+j<length?package[off+j]:0)) return Fail(IOFailure);
    }
    AppInfo next[MaxApps]; Copy((uint8_t*)next,(const uint8_t*)entries,sizeof(next));
    Copy((uint8_t*)next[index].id,(const uint8_t*)info.id,24);
    Copy((uint8_t*)next[index].title,(const uint8_t*)info.title,24);
    next[index].slot=slot; next[index].length=length; next[index].checksum=CRC32(package,length);
    return Commit(next,index==count?count+1:count);
}
bool AppStore::Uninstall(const char* id) {
    if (!mounted) return Fail(NotMounted);
    uint32_t index=count;
    for (uint32_t i=0;i<count;i++) if (SameID(entries[i].id,id)) index=i;
    if (index==count) return Fail(NotFound);
    AppInfo next[MaxApps]; Zero((uint8_t*)next,sizeof(next));
    uint32_t n=0;
    for (uint32_t i=0;i<count;i++) if (i!=index) Copy((uint8_t*)&next[n++],(const uint8_t*)&entries[i],sizeof(AppInfo));
    // Payload reclamation is deferred, protecting the previous directory snapshot.
    return Commit(next,n);
}
bool AppStore::Read(const char* id,uint8_t* out,uint32_t capacity,uint32_t* length) {
    if (length) *length=0;
    if (!mounted) return Fail(NotMounted);
    const AppInfo* e=0;
    for (uint32_t i=0;i<count;i++) if (SameID(entries[i].id,id)) e=&entries[i];
    if (!e) return Fail(NotFound);
    if (!out||capacity<e->length) return Fail(BufferSmall);
    uint8_t sector[512];
    for (uint32_t off=0;off<e->length;off+=512) {
        if (!disk->ReadSector(DataStart+e->slot*SlotSectors+off/512,sector)) return Fail(IOFailure);
        uint32_t n=e->length-off; if (n>512) n=512; Copy(out+off,sector,n);
    }
    PackageInfo info;
    if (CRC32(out,e->length)!=e->checksum||ValidatePackage(out,e->length,&info)!=PackageOK
        ||!SameID(e->id,info.id)||!Equal((uint8_t*)e->title,(uint8_t*)info.title,24)) return Fail(Corrupt);
    if (length) *length=e->length;
    status=OK; return true;
}
const char* AppStore::StatusText() const {
    switch(status) {
        case OK:return "Ready"; case NotMounted:return "Store not mounted";
        case NoDisk:return "No ATA data disk"; case IOFailure:return "Disk I/O failed; remount";
        case NotFormatted:return "Disk not GTOS formatted"; case Corrupt:return "Store data corrupt";
        case Full:return "App store full (8 apps)"; case NotFound:return "App not installed";
        case InvalidPackage:return "Invalid app package"; case BufferSmall:return "App buffer too small";
    }
    return "Unknown store error";
}
