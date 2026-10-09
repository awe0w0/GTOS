#include <process/abi.h>
#include <process/file_abi.h>
#include "record.h"
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,6,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0};
static unsigned Raw(unsigned op,unsigned address,unsigned bytes) {
    unsigned result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return result;
}
extern "C" void NativeEntry() {
    const unsigned sizes[16]={16,16,16,20,8,8,12,20,12,12,20,12,16,8,8,16};
    for (unsigned pass=0;pass<2;pass++) for (unsigned i=0;i<16;i++) {
        const unsigned value=Raw(0x4720+i,pass?0xfffffff8:0,pass?0:sizes[i]);
        native_file_record.raw_calls++;native_file_record.bad_calls++;native_file_record.mask|=1U<<i;native_file_record.checks++;
        if ((int)value!=-19) { native_file_record.error=native_file_record.checks;Raw(GTOS_SYS_EXIT,91,0); }
    }
    native_file_record.invalid_io=0;native_file_record.stage=2;Raw(GTOS_SYS_EXIT,0,0);for (;;) asm volatile("ud2");
}
