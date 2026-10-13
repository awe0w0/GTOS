#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <process/abi.h>
#include "heap.h"
#include "record.h"
extern "C" void __cxa_pure_virtual();
extern "C" void __cxa_deleted_virtual();
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    using U=unsigned;using VirtualCall=void (*)();
    VirtualCall volatile pure=&__cxa_pure_virtual,deleted=&__cxa_deleted_virtual;
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.error=line;Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
}
extern "C" int NativeV8IsolateMain() {
    const U mode=native_file_record.mode;Need(mode<8);
    volatile U* input=(volatile U*)malloc(4096);Need(input);
    input[0]=0xc0a01234;
    input[1023]=0x51a7b00b;Need(input[0]==0xc0a01234&&input[1023]==0x51a7b00b);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16&&heap.handle);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;
    native_file_record.file=(U)input;native_file_record.foreign=(U)&errno;
    native_file_record.command=mode/2;native_file_record.mask=(mode&1)?32:29;
    errno=1234;native_file_record.stage=1;
    if (mode&1) deleted();else pure();
    // A returning ABI implementation must fail the test, even under optimization.
    native_file_record.stage=2;Fail(__LINE__);
}
