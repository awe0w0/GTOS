#include <drivers/nvme.h>
#include <storage/blockheadercache.h>
#include <storage/filestore.h>

extern "C" int puts(const char*);
using namespace gtos::drivers;
using namespace gtos::hardwarecommunication;
using namespace gtos::memory;
using namespace gtos::storage;
static uint32_t checks=0,failures=0;
static void Check(bool value,const char* message) {
    ++checks;if (!value) { ++failures;puts(message); }
}
static void Copy(uint8_t* to,const uint8_t* from,uint32_t size) {
    for (uint32_t i=0;i<size;i++) to[i]=from[i];
}
static void Fill(uint8_t* to,uint8_t value,uint32_t size) {
    for (uint32_t i=0;i<size;i++) to[i]=value;
}
static bool Equal(const uint8_t* a,const uint8_t* b,uint32_t size) {
    for (uint32_t i=0;i<size;i++) if (a[i]!=b[i]) return false;
    return true;
}
static uint32_t clockTicks=0,releases=0,pciCommand=0;
static uint32_t Ticks() { clockTicks+=100;return clockTicks; }

// Only the hardware boundary is substituted. Production queue submission,
// PRP construction, completion matching, bounce copies and FileStore all run.
namespace gtos { namespace hardwarecommunication {
Port::Port(uint16_t value):portnumber(value) {}
Port::~Port() {}
Port32Bit::Port32Bit(uint16_t value):Port(value) {}
Port32Bit::~Port32Bit() {}
void Port32Bit::Write(uint32_t) {}
uint32_t Port32Bit::Read() { return 0; }
PeripheralComponentInterconnectDeviceDescriptor::PeripheralComponentInterconnectDeviceDescriptor() {}
PeripheralComponentInterconnectDeviceDescriptor::~PeripheralComponentInterconnectDeviceDescriptor() {}
PeripheralComponentInterconnectController::PeripheralComponentInterconnectController()
    :dataPort(0xCFC),commandPort(0xCF8) {}
PeripheralComponentInterconnectController::~PeripheralComponentInterconnectController() {}
uint32_t PeripheralComponentInterconnectController::Read(uint16_t,uint16_t,uint16_t,uint32_t) {
    return pciCommand;
}
void PeripheralComponentInterconnectController::Write(uint16_t,uint16_t,uint16_t,uint32_t,uint32_t value) {
    pciCommand=value;
}
} }
namespace gtos { namespace memory {
LowBootstrapPool::LowBootstrapPool() {}
PhysicalMemoryManager::PhysicalMemoryManager() {}
bool PhysicalMemoryManager::freeContiguous(uint32_t,uint32_t) { ++releases;return true; }
} }

namespace gtos { namespace drivers {
struct NvmeTestFixture {
    static uint8_t dma[520U*4096U];
    static uint8_t media[2048U*512U];
    static uint32_t registers[8192U/4U];
    static uint32_t submission,completion,phase,commands,reads,writes,flushes,batches;
    static uint32_t multiSectorReads,readSectors,blockErases,multiSectorPrograms;
    static uint32_t failAt;
    static bool reverse,badCid,resetFails,dropCompletion;
    static uint8_t* mutate;

    static void Setup(NvmeDisk& disk) {
        Fill(dma,0,sizeof(dma));Fill((uint8_t*)registers,0,sizeof(registers));
        disk.controller.pci.bus=disk.controller.pci.device=disk.controller.pci.function=0;
        disk.registers=registers;
        disk.dma=(uint32_t)(__UINTPTR_TYPE__)dma;disk.dmaPages=520;
        disk.readDepth=4;disk.stride=4;disk.readyTicks=50;disk.maximumBytes=8U*512U;
        disk.nsid=1;disk.sectorBytes=512;disk.sectors=2048;disk.entries=16;
        disk.nextCommand=0;disk.ready=true;disk.error=NvmeOk;disk.completionStatus=0;
        disk.io={disk.dma,disk.dma+4096,1,0,0,1};
        for (uint32_t slot=0;slot<4;slot++) {
            const uint32_t data=disk.DataAddress(slot);
            uint64_t* prps=(uint64_t*)(__UINTPTR_TYPE__)(data-4096);
            for (uint32_t page=1;page<128;page++) prps[page-1]=data+page*4096U;
        }
        submission=completion=commands=reads=writes=flushes=batches=multiSectorReads=0;phase=1;
        readSectors=blockErases=multiSectorPrograms=0;
        failAt=0xFFFFFFFFU;reverse=badCid=resetFails=dropCompletion=false;mutate=nullptr;
        releases=0;pciCommand=4;clockTicks=0;registers[0x1C/4]=1;
    }
    static void Geometry(NvmeDisk& disk,uint32_t bytes,uint64_t sectors) {
        disk.sectorBytes=bytes;disk.sectors=sectors;
    }
    static void TransferLimit(NvmeDisk& disk,uint32_t bytes) { disk.maximumBytes=bytes; }
    static void WrapCid(NvmeDisk& disk) { disk.nextCommand=65534; }
    static void Doorbell(NvmeDisk& disk,uint32_t offset,uint32_t tail) {
        if (offset==0x14&&tail==0) {
            registers[0x1C/4]=resetFails?1:0;return;
        }
        if (offset!=0x1008) return;
        uint32_t pending[16][16],posted=0;
        while (submission!=tail&&posted<16) {
            for (uint32_t i=0;i<16;i++) pending[posted][i]=((uint32_t*)dma)[submission*16+i];
            ++posted;submission=(submission+1)%16;
        }
        Check(posted>0&&posted<=4,"bounded queue batch is posted");++batches;
        if (mutate) { *mutate^=0xFF;mutate=nullptr; }
        for (uint32_t n=0;n<posted;n++) {
            const uint32_t index=reverse?posted-1-n:n;
            const uint32_t* command=pending[index];
            const uint32_t opcode=command[0]&255;
            const uint64_t first=((uint64_t)command[11]<<32)|command[10];
            const uint32_t count=(command[12]&0xFFFFU)+1;
            const uint32_t bytes=count*512U;
            const uint32_t data=disk.DataAddress(index);
            Check(command[1]==1,"command targets the identified namespace");
            const bool failed=commands++==failAt;
            if (opcode==0) {
                ++flushes;
                Check(command[6]==0&&command[8]==0&&command[12]==0,"flush has no data transfer");
            } else {
                Check(opcode==1||opcode==2,"NVM read/write opcode");
                Check(first<2048&&count<=2048-first&&bytes<=disk.maximumBytes,"command range and MDTS bound");
                const uint32_t pages=(bytes+4095U)/4096U;
                Check(command[6]==data&&command[7]==0&&command[9]==0,"PRP1 is the owned bounce slot");
                Check(command[8]==(pages==1?0:pages==2?data+4096U:data-4096U),"PRP2 selects the page or list by transfer size");
                if (pages>2) {
                    const uint64_t* prps=(const uint64_t*)(__UINTPTR_TYPE__)command[8];
                    for (uint32_t page=1;page<pages;page++)
                        Check(prps[page-1]==data+page*4096U,"PRP list stays in the owned DMA slot");
                }
                if (!failed) {
                    uint8_t* bounce=(uint8_t*)(__UINTPTR_TYPE__)command[6];
                    if (opcode==1) {
                        ++writes;
                        bool erased=true;
                        for (uint32_t i=0;i<bytes;i++) erased&=bounce[i]==0xFF;
                        if (erased&&bytes==FileStore::BlockSize) ++blockErases;
                        if (!erased&&count>1) ++multiSectorPrograms;
                        Copy(media+(uint32_t)first*512U,bounce,bytes);
                    }
                    else {
                        ++reads;
                        readSectors+=count;
                        if (count>1) ++multiSectorReads;
                        Copy(bounce,media+(uint32_t)first*512U,bytes);
                    }
                }
            }
            if (!dropCompletion) {
                uint32_t* entry=(uint32_t*)(dma+4096)+completion*4;
                entry[2]=(1U<<16)|tail;
                entry[3]=((phase|(failed?2U:0U))<<16)|
                    (badCid?0xAAAAU:command[0]>>16);
                if (++completion==16) { completion=0;phase^=1; }
            }
        }
    }
};
alignas(4096) uint8_t NvmeTestFixture::dma[520U*4096U];
uint8_t NvmeTestFixture::media[2048U*512U];
uint32_t NvmeTestFixture::registers[8192U/4U];
uint32_t NvmeTestFixture::submission,NvmeTestFixture::completion,NvmeTestFixture::phase;
uint32_t NvmeTestFixture::commands,NvmeTestFixture::reads,NvmeTestFixture::writes;
uint32_t NvmeTestFixture::flushes,NvmeTestFixture::batches,NvmeTestFixture::failAt;
uint32_t NvmeTestFixture::multiSectorReads,NvmeTestFixture::readSectors;
uint32_t NvmeTestFixture::blockErases,NvmeTestFixture::multiSectorPrograms;
bool NvmeTestFixture::reverse,NvmeTestFixture::badCid,NvmeTestFixture::resetFails;
bool NvmeTestFixture::dropCompletion;
uint8_t* NvmeTestFixture::mutate;
void NvmeTestWriteRegister(NvmeDisk& disk,uint32_t offset,uint32_t value) {
    NvmeTestFixture::Doorbell(disk,offset,value);
}
} }
using F=gtos::drivers::NvmeTestFixture;
static uint8_t input[513U*512U+2],output[513U*512U+2];

static void Transfers(NvmeDisk& disk) {
    const uint32_t sizes[]={1,2,8,9,31,32,33,257,513};
    for (uint32_t i=0;i<sizeof(input);i++) input[i]=(uint8_t)(i*37U+(i>>8));
    for (uint32_t n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++) {
        F::Setup(disk);F::reverse=true;Fill(output,0xA5,sizeof(output));
        const uint32_t count=sizes[n];
        Check(disk.WriteSectors(3,count,input+1),"real write path completes an unaligned source");
        Check(F::writes==(count+7)/8&&F::reads==0,"write splits at MDTS and never issues a read");
        Check(disk.Flush()&&F::flushes==1,"flush is a real completed NVM command");
        Check(disk.ReadSectors(3,count,output+1),"real read path completes out-of-order CIDs");
        Check(Equal(input+1,output+1,count*512U),"every read byte equals the written payload");
        Check(output[0]==0xA5&&output[count*512U+1]==0xA5,"read preserves both unaligned buffer guards");
    }
    F::Setup(disk);const uint8_t saved=input[0];F::mutate=input;
    Check(disk.WriteSectors(0,1,input),"write completes when caller source changes after posting");
    Check(F::media[0]==saved&&input[0]!=(uint8_t)saved,"DMA reads the stable owned copy");
    input[0]=saved;
    const uint32_t large[]={16,24,512};
    for (uint32_t count:large) {
        F::Setup(disk);F::TransferLimit(disk,count*512U);
        Check(disk.WriteSectors(0,count,input)&&disk.ReadSectors(0,count,output),"multi-page PRP write and read complete");
        Check(Equal(input,output,count*512U),"multi-page payload matches every byte");
    }
    F::Setup(disk);F::WrapCid(disk);F::reverse=true;
    Check(disk.WriteSectors(0,33,input)&&disk.ReadSectors(0,33,output)&&Equal(input,output,33U*512U),
          "command IDs wrap through zero without confusing completion ownership");
}
static void Refusals(NvmeDisk& disk) {
    F::Setup(disk);
    Check(!disk.WriteSectors(0,0,input)&&!disk.WriteSectors(0,1,nullptr),"empty/null writes refused");
    Check(!disk.WriteSectors(2048,1,input)&&!disk.WriteSectors(2047,2,input),"write range refused");
    Check(!disk.ReadSectors(~0ULL,2,output),"64-bit LBA overflow refused");
    Check(!disk.WriteSectors(0,2,(const uint8_t*)(__UINTPTR_TYPE__)0xFFFFFF00U),"source address wrap refused");
    Check(F::commands==0,"invalid requests submit zero commands");
    F::Setup(disk);F::failAt=0;
    Check(!disk.WriteSectors(1,33,input)&&disk.Error()==NvmeCommandFailed,"controller error propagates");
    Check(F::commands==4&&releases==1&&!(pciCommand&4),"no next batch after failure and DMA resets before reclaim");
    const uint32_t before=F::commands;
    Check(!disk.ReadSectors(0,1,output)&&!disk.Flush()&&F::commands==before,"failed controller stays unavailable");
    F::Setup(disk);F::badCid=true;
    Check(!disk.ReadSectors(0,1,output)&&disk.Error()==NvmeBadCompletion,"unmatched completion refused");
    F::Setup(disk);F::failAt=0;F::resetFails=true;
    Check(!disk.WriteSectors(0,1,input)&&releases==0,"unacknowledged reset retains DMA ownership");
    F::Setup(disk);F::dropCompletion=true;
    Check(!disk.Flush()&&disk.Error()==NvmeCommandTimeout,"flush timeout propagates");
}
static void Volume(NvmeDisk& disk) {
    F::Setup(disk);Fill(F::media,0xA5,sizeof(F::media));
    NvmeBlockDevice device(disk);
    Check(device.Identify()&&device.SectorCount()==2048,"512-byte geometry adapts without truncation");
    F::Geometry(disk,4096,2048);
    Check(!device.Identify()&&device.SectorCount()==0,"non-512 namespace is not silently reinterpreted");
    F::Geometry(disk,512,0x100000000ULL);
    Check(!device.Identify()&&device.SectorCount()==0,"FileStore geometry overflow refused");
    F::Geometry(disk,512,2048);
    Check(!device.ReadSectors(2047,2,output)&&!device.WriteSector(2048,input)
        &&!device.WriteSectors(2047,2,input)&&!device.WriteSectors(0,1,nullptr),"adapter range checks before hardware");
    Check(F::commands==0,"adapter refusals submit zero commands");
    static BlockHeaderCache cache(device);
    // Kernel I/O buffers are in the low identity map. Keep this real
    // filesystem's caches below 4 GiB in the no-PIE host image as well.
    static FileStore store;
    Check(store.Format(&cache,8,2040)==0&&store.Mount(&cache,8,2040)==0,"LittleFS formats and mounts a non-erased NVMe volume");
    Check(F::writes>0&&F::blockErases==0,"format programs metadata without erase writes");
    const int32_t fd=store.Open(1,"/profile",FileStore::ReadWrite|FileStore::Create);
    Check(fd>0&&store.WriteFile(1,fd,input,32769)==32769,"real file writes across many queue wraps");
    Check(store.Close(1,fd)==0&&store.Unmount()==0,"close and unmount flush actual NVM writes");
    Check(store.Mount(&cache,8,2040)==0,"remount sees committed metadata");
    FileStore::Info info;
    Check(store.Stat("/profile",info)==0&&info.size==32769,"persisted file size agrees");
    int32_t reader=store.Open(1,"/profile",FileStore::Read);
    const uint32_t smallCacheStart=F::reads;
    Check(reader>0&&store.ReadFile(1,reader,output,32769)==32769&&Equal(input,output,32769),"real file readback matches all bytes");
    const uint32_t smallCacheReads=F::reads-smallCacheStart;
    Check(store.Close(1,reader)==0&&store.OpenCount()==0&&store.Unmount()==0,"all handles close before final unmount");
    Check(F::writes>0&&F::reads>0&&F::flushes>0,"filesystem used real read, write and flush paths");

    static uint8_t cacheBuffer[FileStore::BlockCacheBytes(FileStore::BlockSize)];
    const FileStore::BlockCaches caches={FileStore::BlockSize,cacheBuffer,sizeof(cacheBuffer)};
    Check(store.Mount(&cache,8,2040,false,nullptr,&caches)==0,"block caches mount the same on-disk volume");
    reader=store.Open(1,"/profile",FileStore::Read);
    const uint32_t blockCacheStart=F::reads;
    const uint32_t batchesBefore=F::multiSectorReads;
    Check(reader>0&&store.ReadFile(1,reader,output,32769)==32769&&Equal(input,output,32769),
          "batched filesystem read preserves every file byte");
    Check(F::multiSectorReads>batchesBefore&&F::reads-blockCacheStart<smallCacheReads,
          "block caches issue real multi-sector reads and reduce command count");
    const int32_t writer=store.Open(2,"/profile",FileStore::Write|FileStore::Truncate);
    const uint32_t flushBefore=F::flushes;
    const uint32_t programsBefore=F::multiSectorPrograms;
    Check(writer>0&&store.WriteFile(2,writer,input+1,32769)==32769&&store.Sync(2,writer)==0&&
          F::flushes>flushBefore,"block-sized program cache persists writes and flushes");
    Check(F::multiSectorPrograms>programsBefore,"program cache writes useful payload in multi-sector NVMe commands");
    Check(store.Close(2,writer)==0&&store.ReadFileAt(1,reader,4095,output,4098)==4098&&
          Equal(input+1+4095,output,4098),"open reader sees persisted writes across block boundaries");
    Check(store.Close(1,reader)==0&&store.Unmount()==0&&store.Mount(&cache,8,2040,true)==0,
          "a remount can return to sector-sized buffers");
    reader=store.Open(1,"/profile",FileStore::Read);
    Check(reader>0&&store.ReadFile(1,reader,output,32769)==32769&&Equal(input+1,output,32769),
          "block-cache writes persist through a read-only sector-cache mount");
    Check(store.Close(1,reader)==0&&store.Unmount()==0,"releases remounted file and caches");
    Check(F::blockErases==0,"write, truncate, and remount need no fill writes on NVMe");

    // Leave a replacement's data on the simulated disk and lose the live
    // handles before sync. A fresh reader must see the committed version.
    Check(store.Mount(&cache,8,2040,false,nullptr,&caches)==0,"mount for an interrupted rewrite");
    const int32_t abandoned=store.Open(3,"/profile",FileStore::Write);
    const uint32_t writesBefore=F::writes;
    Check(abandoned>0&&store.TruncateFile(3,abandoned,0)==0
          &&store.WriteFile(3,abandoned,input,32769)==32769&&F::writes>writesBefore,
          "uncommitted replacement reaches the block device");
    F::Setup(disk);
    static BlockHeaderCache recoveredCache(device);
    static FileStore recovered;
    Check(recovered.Mount(&recoveredCache,8,2040)==0,"recover a fresh mount after interrupted write");
    reader=recovered.Open(1,"/profile",FileStore::Read);
    Check(reader>0&&recovered.ReadFile(1,reader,output,32769)==32769&&Equal(input+1,output,32769),
          "recovery exposes the earlier committed file, not the uncommitted replacement");
    Check(recovered.Close(1,reader)==0&&recovered.Unmount()==0,"release recovered file");
    Check(recovered.Format(&recoveredCache,8,2040)==0&&recovered.Mount(&recoveredCache,8,2040)==0
          &&recovered.Open(1,"/profile",FileStore::Read)==LFS_ERR_NOENT,"reformat hides old metadata");
    const int32_t sparse=recovered.Open(1,"/sparse",FileStore::ReadWrite|FileStore::Create);
    const uint32_t hole=8193;
    Check(sparse>0&&recovered.Seek(1,sparse,hole,0)==(int32_t)hole&&recovered.WriteFile(1,sparse,input,1)==1
          &&recovered.Sync(1,sparse)==0,"write a file with a hole over the reused volume");
    Check(recovered.Seek(1,sparse,0,0)==0&&recovered.ReadFile(1,sparse,output,hole+1)==(int32_t)(hole+1),
          "read the entire sparse file");
    bool zero=true;
    for (uint32_t i=0;i<hole;i++) zero&=output[i]==0;
    Check(zero&&output[hole]==input[0],"unwritten bytes stay zero on reused blocks");
    Check(recovered.Close(1,sparse)==0&&recovered.Unmount()==0&&F::blockErases==0,
          "recovery, reformat and sparse writes require no erase writes");
}
static void LargeVolume(NvmeDisk& disk,uint32_t blockBytes) {
    F::Setup(disk);
    Fill(F::media,0xA5,sizeof(F::media));
    const uint32_t diskSectors=1792;
    F::TransferLimit(disk,blockBytes);
    NvmeBlockDevice device(disk);
    static FileStore store;
    static uint8_t cacheBuffer[FileStore::BlockCacheBytes(GTOS_CONTENT_VOLUME_128K_BLOCK_BYTES)];
    FileStore::BlockCaches caches={blockBytes,cacheBuffer,FileStore::BlockCacheBytes(blockBytes)};
    BlockHeaderCache cache(device,blockBytes/FileStore::SectorSize,8);
    --caches.length;
    Check(store.Format(&cache,8,diskSectors,&caches)==LFS_ERR_INVAL&&F::commands==0,
          "undersized block cache is refused before touching the volume");
    ++caches.length;
    Check(store.Format(&cache,8,diskSectors-8,&caches)==LFS_ERR_INVAL&&F::commands==0,
          "large geometry requires a whole number of blocks");
    Check(store.Format(&cache,8,diskSectors,&caches)==0
          &&store.Mount(&cache,8,diskSectors,false,nullptr,&caches)==0,"large LittleFS blocks format and mount");
    int32_t handle=store.Open(1,"/profile",FileStore::Create|FileStore::ReadWrite);
    const uint32_t size=2*blockBytes+17;
    Check(handle>0&&store.WriteFile(1,handle,input,size)==(int32_t)size
          &&store.Sync(1,handle)==0&&store.Close(1,handle)==0&&store.Unmount()==0,
          "a file crossing large block boundaries is committed");
    Check(store.Mount(&cache,8,diskSectors,true)!=0,"the legacy mount refuses the different block geometry");
    const uint32_t otherBlockBytes=blockBytes==GTOS_CONTENT_VOLUME_LARGE_BLOCK_BYTES
        ? GTOS_CONTENT_VOLUME_128K_BLOCK_BYTES : GTOS_CONTENT_VOLUME_LARGE_BLOCK_BYTES;
    FileStore::BlockCaches otherCaches={otherBlockBytes,cacheBuffer,FileStore::BlockCacheBytes(otherBlockBytes)};
    Check(store.Mount(&cache,8,diskSectors,true,nullptr,&otherCaches)!=0,
          "an explicitly different large block geometry is refused");
    Check(store.Mount(&cache,8,diskSectors,true,nullptr,&caches)==0,"explicit geometry remounts the volume");
    handle=store.Open(1,"/profile",FileStore::Read);
    const uint32_t sectorEdge=3*FileStore::SectorSize-7;
    const uint32_t offsets[]={blockBytes/2+13,blockBytes+123,sectorEdge};
    for (uint32_t offset:offsets) {
        const uint32_t before=F::readSectors,requests=F::reads;
        Check(handle>0&&store.ReadFileAt(1,handle,offset,output,19)==19&&Equal(input+offset,output,19),
              "a short positioned read returns the requested bytes");
        const uint32_t sectors=F::readSectors-before;
        char line[]="POSITIONED READ SECTORS 000";
        line[sizeof(line)-4]='0'+sectors/100;
        line[sizeof(line)-3]='0'+sectors/10%10;
        line[sizeof(line)-2]='0'+sectors%10;
        puts(line);
        Check(sectors<=4,"short positioned reads avoid large block prefetch");
        if (offset==sectorEdge)
            Check(sectors==2&&F::reads-requests==1,"a sector-spanning short read uses one NVMe transfer");
    }
    Check(handle>0&&store.ReadFileAt(1,handle,blockBytes-11,output,4096)==4096
          &&Equal(input+blockBytes-11,output,4096),"positioned read crosses large block boundaries");
    Check(handle>0&&store.ReadFileAt(1,handle,size-17,output,32)==17&&Equal(input+size-17,output,17)
          &&store.ReadFileAt(1,handle,size,output,32)==0,"positioned reads clamp to EOF");
    Check(handle>0&&store.ReadFile(1,handle,output,size)==(int32_t)size&&Equal(input,output,size),
          "positioned reads preserve the cursor for a full sequential read");
    Check(store.Close(1,handle)==0&&store.Unmount()==0,"close all large-block resources");
}
int main() {
    PeripheralComponentInterconnectController pci;
    PhysicalMemoryManager frames;
    NvmeDisk disk(pci,frames,Ticks);
    Transfers(disk);Refusals(disk);Volume(disk);
    LargeVolume(disk,GTOS_CONTENT_VOLUME_LARGE_BLOCK_BYTES);
    LargeVolume(disk,GTOS_CONTENT_VOLUME_128K_BLOCK_BYTES);
    puts(failures?"NVME IO FAIL":"NVME IO PASS: queues, DMA, read/write/flush, refusals and FileStore");
    return failures?1:0;
}
