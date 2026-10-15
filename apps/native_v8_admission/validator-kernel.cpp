// Private diagnostic: pass the real full58 ELF to the unchanged GTOS validator.
#include <process/elf32.h>
#include <memory/multiboot.h>
using namespace gtos::memory;
using namespace gtos::process;
extern "C" uint8_t kernel_end;
namespace {
    void Text(const char* text) {
        while (*text) { asm volatile("outb %0,$0xe9"::"a"(*text));++text; }
    }
    void Hex(uint32_t value) {
        for (int shift=28;shift>=0;shift-=4) {
            const char digit="0123456789ABCDEF"[(value>>shift)&15];
            asm volatile("outb %0,$0xe9"::"a"(digit));
        }
    }
    [[noreturn]] void Finish(bool pass) {
        Text(pass?"GTOS REAL V8 ADMISSION PAGE LIMIT PASS\n":"GTOS REAL V8 ADMISSION FAIL\n");
        const uint8_t code=pass?0x10:0x11;asm volatile("outb %0,$0xf4"::"a"(code));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass) { if (!pass) Finish(false); }
    uint32_t Get32(const uint8_t* image,uint32_t size,uint32_t offset) {
        Need(offset<=size && size-offset>=4);
        return image[offset]|((uint32_t)image[offset+1]<<8)|((uint32_t)image[offset+2]<<16)|((uint32_t)image[offset+3]<<24);
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    Text("GTOS REAL V8 ADMISSION BOOT\n");
    Need(magic==MultibootBootMagic && (uint32_t)multiboot>=0x1000 && (uint32_t)multiboot<=0x04000000U-sizeof(MultibootInfo));
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;
    Need((boot->flags&8) && boot->moduleCount==1 && boot->modules>=0x1000 && boot->modules<=0x04000000U-sizeof(MultibootModule));
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    Need(module.start>=(uint32_t)&kernel_end && module.end>module.start && module.end<=0x04000000U);
    const uint8_t* image=(const uint8_t*)module.start;const uint32_t size=module.end-module.start;
    Need(size==12927508U && Get32(image,size,0)==0x464c457fU && Get32(image,size,24)==0x40000000U);
    const uint32_t phoff=Get32(image,size,28),shape=Get32(image,size,40);
    Need((shape>>16)==32 && size>=46 && image[44]==2 && image[45]==0 && phoff<=size && size-phoff>=64);
    uint32_t pages=0,bytes=0;
    for (uint32_t index=0;index<2;index++) {
        const uint32_t offset=phoff+index*32;
        const uint32_t address=Get32(image,size,offset+8),memory=Get32(image,size,offset+20);
        Need(Get32(image,size,offset)==1 && !(address&4095) && memory<0x01000000U);
        pages+=(memory+4095)/4096;bytes+=memory;
    }
    Text("V8 ELF file=");Hex(size);Text(" load_bytes=");Hex(bytes);Text(" pages=");Hex(pages);Text(" cap=");Hex(Elf32MaximumPages);Text("\n");
    Need(pages==2383 && bytes==9757692 && Elf32MaximumPages==256);
    Elf32LoadPlan plan;uint8_t* output=(uint8_t*)&plan;
    for (uint32_t i=0;i<sizeof(plan);i++) output[i]=0xa5;
    Elf32Error error=Elf32Ok;const bool accepted=ValidateElf32(image,size,plan,error);
    bool zero=true;for (uint32_t i=0;i<sizeof(plan);i++) if (output[i]) zero=false;
    Text("V8 ELF VALIDATE accepted=");Hex(accepted?1:0);Text(" error=");Hex(error);Text(" name=");Text(Elf32ErrorName(error));Text(" plan_zero=");Hex(zero?1:0);Text("\n");
    Need(!accepted && error==Elf32PageLimit && zero);Finish(true);
}
