#include <process/elf32.h>
extern "C" int qualifier_read_file(const char*,unsigned char*,unsigned int,unsigned int*);
extern "C" void qualifier_report(const char*,unsigned int,unsigned int);
static unsigned char image[2*1024*1024];
int main(int argc,char** argv) {
  if(argc!=2) return 1;
  uint32_t size=0;
  if(qualifier_read_file(argv[1],image,sizeof(image),&size)) return 2;
  gtos::process::Elf32LoadPlan plan;
  gtos::process::Elf32Error error;
  if(!gtos::process::ValidateElf32(image,(uint32_t)size,plan,error)) {
    qualifier_report(gtos::process::Elf32ErrorName(error),0,0);return 4;
  }
  if(plan.pageCount>254) return 5;
  for(uint32_t i=0;i<plan.segmentCount;i++) {
    const auto& segment=plan.segments[i];
    uint32_t first=segment.virtualAddress&~4095u;
    uint32_t end=(segment.virtualAddress+segment.memorySize+4095u)&~4095u;
    if(first<0xc0000000u && 0xbfffc000u<end) return 6;
  }
  qualifier_report("REAL GTOS ELF32 VALIDATOR PASS",plan.pageCount,plan.segmentCount);
  return 0;
}
