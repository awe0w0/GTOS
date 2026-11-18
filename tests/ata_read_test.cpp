#include <drivers/ata.h>
#include <storage/blockheadercache.h>

extern "C" int puts(const char*);
using namespace gtos::drivers;
using namespace gtos::storage;
static uint32_t checks=0,failures=0;
static void Check(bool value,const char* message) {
    ++checks;if (!value) { ++failures;puts(message); }
}
static uint8_t Pattern(uint32_t sector,uint32_t byte) {
    return (uint8_t)(sector*37U+byte*13U+(sector>>16));
}
namespace gtos { namespace hardwarecommunication {
static uint32_t accesses=0,capacity=0x10000000U,word=0,current=0,remaining=0,completed=0;
static uint32_t commands=0,starts[513],counts[513],failAfter=0xFFFFFFFFU;
static uint32_t writeSize=512;
static uint8_t registers[8];
static bool identify=false,failFinish=false,writing=false,writeDataMatches=true;
static void Reset() {
    accesses=commands=completed=remaining=word=0;
    failAfter=0xFFFFFFFFU;failFinish=false;writing=false;
    writeSize=512;writeDataMatches=true;
}
Port::Port(uint16_t number):portnumber(number) {}
Port::~Port() {}
Port8Bit::Port8Bit(uint16_t number):Port(number) {}
Port8Bit::~Port8Bit() {}
void Port8Bit::Write(uint8_t value) {
    ++accesses;
    if (portnumber>=0x1F0&&portnumber<=0x1F7) registers[portnumber-0x1F0]=value;
    if (portnumber!=0x1F7) return;
    if (value==0xEC) { identify=true;word=0; }
    if (value==0x20||value==0x30) {
        writing=value==0x30;
        Check(commands<513,"command capture bound");
        current=((uint32_t)(registers[6]&15)<<24)|((uint32_t)registers[5]<<16)
            |((uint32_t)registers[4]<<8)|registers[3];
        remaining=registers[2]?registers[2]:256;word=0;
        if (commands<513) { starts[commands]=current;counts[commands]=remaining; }
        ++commands;
    }
}
uint8_t Port8Bit::Read() {
    ++accesses;
    if (portnumber==0x1F7||portnumber==0x3F6) {
        if (commands&&(completed>=failAfter||(failFinish&&!remaining))) return 0x41;
        return identify||remaining?0x48:0x40;
    }
    return 0;
}
Port16Bit::Port16Bit(uint16_t number):Port(number) {}
Port16Bit::~Port16Bit() {}
void Port16Bit::Write(uint16_t value) {
    ++accesses;
    Check(remaining&&writing,"data write has a pending sector");
    const uint32_t byte=word*2U;
    const uint16_t low=byte<writeSize?Pattern(current,byte):0;
    const uint16_t high=byte+1<writeSize?Pattern(current,byte+1):0;
    writeDataMatches&=value==(low|(high<<8));
    if (++word==256) { word=0;++current;--remaining;++completed; }
}
uint16_t Port16Bit::Read() {
    ++accesses;
    if (identify) {
        const uint32_t index=word++;
        if (word==256) { identify=false;word=0; }
        if (index==49) return 1U<<9;
        if (index==60) return (uint16_t)capacity;
        if (index==61) return (uint16_t)(capacity>>16);
        return index>=27&&index<47?0x2020:0;
    }
    Check(remaining!=0,"data read has a pending sector");
    const uint16_t value=Pattern(current,word*2U)|((uint16_t)Pattern(current,word*2U+1)<<8);
    if (++word==256) { word=0;++current;--remaining;++completed; }
    return value;
}
void Port16Bit::ReadWords(uint8_t* data,uint32_t count) {
    Check(count==256,"bulk data port transfers exactly one sector");
    for (uint32_t i=0;i<count;i++) {
        const uint16_t value=Read();*data++=(uint8_t)value;*data++=(uint8_t)(value>>8);
    }
}
void Port16Bit::WriteWords(const uint8_t* data,uint32_t count) {
    Check(count==256,"bulk write transfers exactly one sector per DRQ");
    for (uint32_t i=0;i<count;i++,data+=2) Write(data[0]|((uint16_t)data[1]<<8));
}
} }
using namespace gtos::hardwarecommunication;
static uint8_t bytes[513U*512U+2];
static void Fill(uint8_t value) { for (uint32_t i=0;i<sizeof(bytes);i++) bytes[i]=value; }
static void FillSectors(uint32_t first,uint32_t count) {
    for (uint32_t sector=0;sector<count;sector++)
        for (uint32_t i=0;i<512;i++) bytes[1+sector*512U+i]=Pattern(first+sector,i);
}
static bool Matches(uint32_t first,uint32_t count,const uint8_t* data) {
    for (uint32_t sector=0;sector<count;sector++)
        for (uint32_t i=0;i<512;i++) if (data[sector*512U+i]!=Pattern(first+sector,i)) return false;
    return true;
}
static void Number(uint32_t value) {
    char digits[11];uint32_t used=0;
    do { digits[used++]=(char)('0'+value%10);value/=10; } while (value);
    for (uint32_t i=0;i<used/2;i++) {
        const char saved=digits[i];digits[i]=digits[used-1-i];digits[used-1-i]=saved;
    }
    digits[used]=0;puts(digits);
}
static void AtaReads() {
    AdvancedTechnologyAttachment disk(0x1F0,true);
    Check(disk.Identify()&&disk.SectorCount()==capacity,"real ATA identify reaches the port spy");
    const uint32_t lengths[]={1,2,8,16,255,256,257,513};
    for (uint32_t n=0;n<sizeof(lengths)/sizeof(lengths[0]);n++) {
        Reset();Fill(0xA5);const uint32_t count=lengths[n];
        Check(disk.ReadSectors(31,count,bytes+1),"multi-sector read succeeds");
        Check(commands==(count+255)/256&&completed==count,"at most 256 sectors per ATA command");
        bool ranges=true;
        for (uint32_t i=0;i<commands;i++)
            ranges&=starts[i]==31+i*256U&&counts[i]==(count-i*256U>256?256:count-i*256U);
        Check(ranges&&Matches(31,count,bytes+1),"command ranges and every returned byte agree");
        Check(bytes[0]==0xA5&&bytes[count*512U+1]==0xA5,"read preserves unaligned buffer guards");
        Check(disk.LastError()==AdvancedTechnologyAttachment::None,"success clears the previous error");
    }
    const uint32_t batchCommands=commands;
    Reset();Fill(0xA5);bool control=true;
    for (uint32_t i=0;i<513;i++) control&=disk.Read28(31+i,bytes+1+i*512U);
    Check(control&&commands==513&&Matches(31,513,bytes+1),"single-sector control returns identical bytes");
    Check(batchCommands==3,"513-sector transfer uses three commands rather than 513");
    puts("ATA READ CONTROL COMMANDS");Number(commands);
    puts("ATA READ BATCH COMMANDS");Number(batchCommands);
    Reset();Fill(0xA5);
    Check(disk.ReadSectors(capacity-256,256,bytes+1)&&commands==1
        &&starts[0]==capacity-256&&counts[0]==256&&Matches(capacity-256,256,bytes+1),
        "zero count register encodes 256 sectors through the final LBA28 sector");
    Reset();
    Check(!disk.ReadSectors(0,0,bytes)&&!disk.ReadSectors(0,1,0)
        &&!disk.ReadSectors(capacity,1,bytes)&&!disk.ReadSectors(capacity-1,2,bytes)
        &&!disk.ReadSectors(0xFFFFFFFFU,2,bytes)
        &&!disk.ReadSectors(0,1,(uint8_t*)(~(__UINTPTR_TYPE__)0-255)),"invalid ranges rejected");
    Check(!accesses&&disk.LastError()==AdvancedTechnologyAttachment::BadArgument,
          "all invalid ranges fail before the first hardware access");
    Reset();Fill(0xA5);failAfter=2;
    Check(!disk.ReadSectors(13,257,bytes+1)&&commands==1&&completed==2
        &&disk.LastError()==AdvancedTechnologyAttachment::DeviceError,"mid-command error stops before another command");
    Check(Matches(13,2,bytes+1)&&bytes[2*512U+1]==0xA5,"partial failure writes only completed sectors");
    Reset();failFinish=true;
    Check(!disk.ReadSectors(0,1,bytes)&&completed==1
        &&disk.LastError()==AdvancedTechnologyAttachment::DeviceError,"final command status is checked");
    const int partial[]={1,3,511};
    for (uint32_t i=0;i<3;i++) {
        Reset();Fill(0xA5);
        Check(disk.Read28(9,bytes+1,partial[i])&&commands==1&&completed==1,"partial-sector compatibility drains a whole sector");
        bool correct=true;
        for (int j=0;j<partial[i];j++) correct&=bytes[j+1]==Pattern(9,(uint32_t)j);
        Check(correct&&bytes[0]==0xA5&&bytes[partial[i]+1]==0xA5,"partial-sector compatibility preserves the tail");
    }
    Reset();AdvancedTechnologyAttachment absent(0x1F0,true);
    Check(!absent.ReadSectors(0,1,bytes)&&!accesses,"unidentified disk performs no hardware access");
}
static void AtaWrites() {
    AdvancedTechnologyAttachment disk(0x1F0,true);
    Check(disk.Identify(),"identify a writable ATA disk");
    Reset();FillSectors(31,257);
    Check(disk.WriteSectors(31,257,bytes+1)&&commands==2&&completed==257
        &&starts[0]==31&&starts[1]==287&&counts[0]==256&&counts[1]==1
        &&writeDataMatches&&Matches(31,257,bytes+1),"write batches preserve unaligned bytes and split at 256 sectors");
    Reset();failAfter=2;
    Check(!disk.WriteSectors(31,257,bytes+1)&&commands==1&&completed==2&&writeDataMatches
        &&disk.LastError()==AdvancedTechnologyAttachment::DeviceError,"failed write stops before issuing another command");
    Reset();failFinish=true;
    Check(!disk.WriteSectors(31,1,bytes+1)&&commands==1&&completed==1
        &&disk.LastError()==AdvancedTechnologyAttachment::DeviceError,"final write status is checked");
    Reset();writeSize=3;
    Check(disk.Write28(31,bytes+1,3)&&commands==1&&completed==1&&writeDataMatches,
          "legacy partial write still pads a sector with zeroes");
    Reset();
    Check(!disk.WriteSectors(0,0,bytes)&&!disk.WriteSectors(0,1,0)
        &&!disk.WriteSectors(capacity-1,2,bytes)
        &&!disk.WriteSectors(0,1,(const uint8_t*)(~(__UINTPTR_TYPE__)0-255))
        &&!accesses&&disk.LastError()==AdvancedTechnologyAttachment::BadArgument,
        "invalid writes fail before the first port access");
}
class SectorDisk : public BlockDevice {
public:
    uint32_t reads,writes,batches,failed;
    SectorDisk():reads(0),writes(0),batches(0),failed(0xFFFFFFFFU) {}
    bool Identify() override { return true; }
    uint32_t SectorCount() const override { return 2048; }
    bool ReadSector(uint32_t sector,uint8_t* data) override {
        ++reads;if (sector==failed||sector>=SectorCount()||!data) return false;
        for (uint32_t i=0;i<512;i++) data[i]=Pattern(sector,i);
        return true;
    }
    bool WriteSector(uint32_t sector,const uint8_t* data) override {
        ++writes;
        return sector!=failed&&sector<SectorCount()&&data&&Matches(sector,1,data);
    }
    bool Flush() override { return true; }
};
class BatchDisk : public SectorDisk {
public:
    uint32_t writeBatches=0;
    bool ReadSectors(uint32_t first,uint32_t count,uint8_t* data) override {
        ++batches;return BlockDevice::ReadSectors(first,count,data);
    }
    bool WriteSectors(uint32_t first,uint32_t count,const uint8_t* data) override {
        ++writeBatches;return BlockDevice::WriteSectors(first,count,data);
    }
};
static void DeviceAndCache() {
    SectorDisk disk;Fill(0xA5);
    Check(disk.ReadSectors(2045,3,bytes+1)&&disk.reads==3&&Matches(2045,3,bytes+1),
          "sector-only devices retain the bounded fallback");
    const uint32_t before=disk.reads;
    Check(!disk.ReadSectors(2047,2,bytes)&&!disk.ReadSectors(0,0,bytes)
        &&!disk.ReadSectors(0,1,0)&&disk.reads==before,"fallback rejects the whole range before I/O");
    disk.failed=3;disk.reads=0;Fill(0xA5);
    Check(!disk.ReadSectors(1,4,bytes+1)&&disk.reads==3&&Matches(1,2,bytes+1)
        &&bytes[1025]==0xA5,"fallback stops at the first failed sector");
    FillSectors(1,4);
    Check(!disk.WriteSectors(1,4,bytes+1)&&disk.writes==3,"write fallback stops at the first failed sector");
    Check(!disk.WriteSectors(2047,2,bytes+1)&&disk.writes==3,"invalid write range never touches the disk");
    disk.failed=0xFFFFFFFFU;
    Check(disk.WriteSectors(1,4,bytes+1)&&disk.writes==7,"sector-only write devices keep the bounded fallback");
    BatchDisk source;BlockHeaderCache cache(source);
    Check(cache.ReadSectors(8,17,bytes+1)&&source.batches==1&&source.reads==17
        &&Matches(8,17,bytes+1),"metadata cache forwards one contiguous device request");
    const uint32_t readBaseline=source.reads;
    Check(cache.ReadSector(8,bytes)&&cache.ReadSectors(16,1,bytes)
        &&cache.ReadSector(24,bytes)&&source.reads==readBaseline,"batch populates every aligned block header");
    source.failed=8;
    Check(!cache.WriteSector(8,bytes)&&!cache.ReadSector(8,bytes)
        &&source.reads==readBaseline+1,"failed write still invalidates the cached header");
    source.failed=0xFFFFFFFFU;
    Check(cache.ReadSector(8,bytes),"reload the header before testing a partial write");
    source.failed=9;
    FillSectors(8,17);
    Check(!cache.WriteSectors(8,17,bytes+1)&&source.writeBatches==2,
          "cache forwards a batched write once and propagates partial failure");
    source.failed=0xFFFFFFFFU;
    const uint32_t afterWrite=source.reads;
    Check(cache.ReadSector(8,bytes)&&cache.ReadSector(16,bytes)&&cache.ReadSector(24,bytes)
        &&source.reads==afterWrite+3,"partial write invalidates every possibly written header");
    Check(!cache.WriteSectors(2047,2,bytes)&&!cache.WriteSectors(0,1,0)
        &&source.writeBatches==2,"cache rejects invalid writes before forwarding");
    const uint32_t batchBaseline=source.batches;
    Check(!cache.ReadSectors(2047,2,bytes)&&!cache.ReadSectors(0,1,0)
        &&source.batches==batchBaseline,"cache rejects invalid requests before forwarding");
    source.failed=64;
    Check(!cache.ReadSectors(64,2,bytes),"failed device batch is not cached");
    source.failed=0xFFFFFFFFU;const uint32_t failedReads=source.reads;
    Check(cache.ReadSector(64,bytes)&&source.reads==failedReads+1,"failed batch cannot supply a later cache hit");
}
int main() {
    AtaReads();AtaWrites();DeviceAndCache();
    Check(checks>1000,"all full-sector port transfers were exercised");
    puts(failures?"ATA BATCH READ FAIL":"ATA BATCH READ PASS");
    return failures?1:0;
}
