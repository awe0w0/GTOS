#include <memory/physical.h>
#include <memory/bootstrap.h>
using namespace gtos::memory;
static uint32_t checks=0, failures=0;
static void print(const char* text){uint32_t length=0;while(text[length])++length;{ uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4),"b"(1),"c"(text),"d"(length) : "memory", "cc"); }}
static void number(uint32_t value){char digits[11];uint32_t at=10;digits[at]=0;do{digits[--at]='0'+value%10;value/=10;}while(value);print(digits+at);}
static void check(bool ok,uint32_t line){++checks;if(!ok){++failures;print("FAIL low bootstrap line ");number(line);print("\n");}}
#define CHECK(x) check((x),__LINE__)
static MultibootInfo info;
static MultibootMemoryMapEntry map[4];
static MultibootModule module;
static void prepare(){
 for(uint32_t i=0;i<sizeof(info);++i)((uint8_t*)&info)[i]=0;
 info.flags=0x41;info.memLower=640;info.memUpper=15*1024;
 info.memoryMap=(uint32_t)map;info.memoryMapLength=2*sizeof(map[0]);
 map[0].size=20;map[0].address=0;map[0].length=0x100000;map[0].type=1;
 map[1].size=20;map[1].address=0x100000;map[1].length=15*1024*1024;map[1].type=1;
}
static void policyTests(){
 LowBootstrapPool pool;uint32_t address=1;
 CHECK(!pool.claim(address)&&!address);
 pool.addAvailable(0,0x100000);CHECK(!pool.claim(address));
 CHECK(pool.restrictFirmware(640,0x9fc00,639));CHECK(pool.freePages()==143);
 pool.reserve(0x10001,4096);CHECK(pool.freePages()==141);
 CHECK(pool.claim(address)&&address==0x12000);CHECK(pool.isClaimed(address));
 CHECK(!pool.isClaimed(address+1));CHECK(pool.intersectsClaimed(address+4095,1));
 CHECK(!pool.intersectsClaimed(address+4096,1));CHECK(pool.claimedPages()==1);
 CHECK(!pool.restrictFirmware(640,0));CHECK(pool.isClaimed(address));
 for(uint32_t i=0;i<140;++i)CHECK(pool.claim(address));
 CHECK(!pool.claim(address)&&!address);
 pool.clear();pool.addAvailable(0,0x100000);CHECK(!pool.restrictFirmware(641,0));CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(0,0x100000);CHECK(!pool.restrictFirmware(0,0));CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(0,0x100000);CHECK(!pool.restrictFirmware(640,0x800));CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(0,0x100000);CHECK(!pool.restrictFirmware(640,0xa0000));CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(0,0x100000);CHECK(pool.restrictFirmware(640,0));CHECK(pool.freePages()==144);
 pool.clear();pool.addAvailable(0,0x100000);CHECK(pool.restrictFirmware(640,0x88000));CHECK(pool.freePages()==120);
 pool.clear();pool.addAvailable(0x10001,0x3fff);CHECK(pool.restrictFirmware(640,0));CHECK(pool.freePages()==3);
 pool.reserve(0x12001,1);CHECK(pool.freePages()==2);
 CHECK(pool.claim(address)&&address==0x11000);CHECK(pool.claim(address)&&address==0x13000);CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(~0ULL,2);CHECK(pool.restrictFirmware(640,0));CHECK(!pool.claim(address));
 pool.clear();pool.addAvailable(0,0x100000);CHECK(pool.restrictFirmware(640,0,320));CHECK(pool.freePages()==64);
}
static void integrationTests(){
 PhysicalMemoryManager frames;uint32_t address=1;
 CHECK(!frames.claimLowBootstrapPage(address)&&!address);
 prepare();info.flags|=1u<<3;info.modules=(uint32_t)&module;info.moduleCount=1;
 module.start=0x10000;module.end=0x11001;module.string=0;
 const PhysicalRange extra={0x12001,4096};
 CHECK(frames.initialize(&info,MultibootBootMagic,0x100000,0x200000,&extra,1));
 const PhysicalMemoryStatistics before=frames.getStatistics();
 CHECK(before.bootstrapFreeFrames==139&&before.bootstrapFrames==0);
 CHECK(frames.claimLowBootstrapPage(address)&&address==0x14000);
 CHECK(frames.isBootstrapPage(address));CHECK(!frames.isBootstrapPage(address+1));
 CHECK(!frames.isAllocated(address)&&!frames.isFree(address));CHECK(!frames.free(address));
 CHECK(!frames.reserveRegion(address+1,1));
 CHECK(!frames.initialize(&info,MultibootBootMagic,0x100000,0x200000));
 CHECK(frames.isBootstrapPage(address));
 CHECK(frames.getStatistics().freeFrames==before.freeFrames);
 CHECK(frames.getStatistics().allocatedFrames==before.allocatedFrames);
 CHECK(frames.getStatistics().bootstrapFrames==1);
 CHECK(frames.getStatistics().bootstrapFreeFrames==138);
 for(uint32_t i=0;i<138;++i){CHECK(frames.claimLowBootstrapPage(address));CHECK(address<0x9f000&&address>=0x15000);}
 CHECK(!frames.claimLowBootstrapPage(address)&&!address);
 CHECK(frames.getStatistics().bootstrapFrames==139);
 CHECK(frames.getStatistics().freeFrames==before.freeFrames);
 // A separate instance verifies successful high-memory fallback cannot invent low RAM.
 PhysicalMemoryManager fallback;prepare();info.flags=1;
 CHECK(fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));
 CHECK(!fallback.claimLowBootstrapPage(address)&&!address);
 CHECK(fallback.allocate(address));CHECK(fallback.free(address));
 // mem_lower supplies an additional cap, never an expansion beyond BIOS/EBDA.
 prepare();info.memLower=320;
 CHECK(fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));
 CHECK(fallback.getStatistics().bootstrapFreeFrames==64);
 CHECK(fallback.reserveRegion(0x10000,4096));CHECK(fallback.getStatistics().bootstrapFreeFrames==63);
 // An overlapping reserved map entry cannot be made eligible by a later RAM entry.
 prepare();map[2].size=20;map[2].address=0;map[2].length=0x100000;map[2].type=2;
 info.memoryMapLength=3*sizeof(map[0]);
 CHECK(fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));CHECK(!fallback.claimLowBootstrapPage(address));
 // Bad Multiboot low-memory metadata disables the SIPI pool, not ordinary RAM.
 prepare();info.memLower=0xFFFFFFFFu;
 CHECK(fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));CHECK(!fallback.claimLowBootstrapPage(address));
 prepare();info.memLower=0;
 CHECK(fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));CHECK(!fallback.claimLowBootstrapPage(address));
 // Failed initialization clears the boot-only pool as well.
 prepare();map[0].size=19;
 CHECK(!fallback.initialize(&info,MultibootBootMagic,0x100000,0x200000));CHECK(!fallback.claimLowBootstrapPage(address));
}
extern "C" int lowMemoryTests(){policyTests();integrationTests();print("Low bootstrap tests: ");number(checks);print(" checks, ");number(failures);print(" failures\n");return failures?1:0;}
asm(".global _start\n_start:\n andl $-16,%esp\n call lowMemoryTests\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
