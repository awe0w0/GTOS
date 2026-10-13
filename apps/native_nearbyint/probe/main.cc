#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <process/abi.h>
#include <process/clock_abi.h>
#include "heap.h"
#include "record.h"
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    using U=unsigned;using Q=uint64_t;
    static_assert(sizeof(double)==8&&__DBL_MANT_DIG__==53,"actual IEEE64 ABI");
    using Rounder=double (*)(double) noexcept;Rounder volatile rounder=static_cast<Rounder>(&::nearbyint);
    void* pressure[80];U actual_lo,actual_hi,expected_lo,expected_hi,diagnostic_mode,diagnostic_sticky;
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;
        native_file_record.file=actual_lo;native_file_record.invalid_io=expected_lo;
        native_file_record.directory=actual_hi;native_file_record.foreign=expected_hi;
        native_file_record.command=diagnostic_mode;native_file_record.png_bytes=diagnostic_sticky;
        unsigned short cw,sw;U mx;asm volatile("fnstcw %0; fnstsw %1; stmxcsr %2":"=m"(cw),"=m"(sw),"=m"(mx));
        native_file_record.base=cw;native_file_record.vm_handle=mx;native_file_record.progress=sw;
        Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    Q Bits(double value) { Q bits;memcpy(&bits,&value,8);return bits; }
    double Value(Q bits) { double value;memcpy(&value,&bits,8);return value; }
    U ControlWord() { unsigned short word;asm volatile("fnstcw %0":"=m"(word));return word; }
    U StatusWord() { unsigned short word;asm volatile("fnstsw %0":"=m"(word));return word; }
    U Mxcsr() { U word;asm volatile("stmxcsr %0":"=m"(word));return word; }
    void SetControls(U cw,U mx) { unsigned short word=(unsigned short)cw;asm volatile("fldcw %0; ldmxcsr %1"::"m"(word),"m"(mx):"memory"); }
    void SetFlags(bool sticky) {
        U environment[7];asm volatile("fnstenv %0":"=m"(environment)::"memory");
        environment[1]=(environment[1]&~0x3fU)|(sticky?0x20U:0U);
        asm volatile("fldenv %0"::"m"(environment):"memory");
        U mx=Mxcsr()&~0x3fU;if (sticky) mx|=0x20;
        asm volatile("ldmxcsr %0"::"m"(mx):"memory");
    }
#include "vectors.h"
    void NumericChecks() {
        const U before=native_file_record.checks,cw=ControlWord(),mx=Mxcsr();U calls=0;
        Need(!(cw&0xc00)&&!(mx&0x6000)&&!(mx&0x8040)&&(cw&0x3f)==0x3f&&(mx&0x1f80)==0x1f80);
        for (U mode=0;mode<4;mode++) for (U sticky=0;sticky<2;sticky++) for (const RoundCase& row:roundCases) {
            diagnostic_mode=mode;diagnostic_sticky=sticky;
            SetControls((cw&~0xc00U)|(mode<<10),(mx&~0x603fU)|(mode<<13));
            const double input=Value(row.input);SetFlags(sticky);errno=1234;
            Need(((StatusWord()>>5)&1)==sticky&&((Mxcsr()>>5)&1)==sticky);
            const Q actual=Bits(rounder(input));calls++;actual_lo=(U)actual;actual_hi=(U)(actual>>32);
            if (native_file_record.mode==5&&!sticky) {
                U corrupted=Mxcsr()|0x20;asm volatile("ldmxcsr %0"::"m"(corrupted):"memory");
            }
            expected_lo=(U)row.expected[mode];expected_hi=(U)(row.expected[mode]>>32);
            Need(actual==row.expected[mode]);Need(errno==1234);
            Need((ControlWord()&0xc00)==mode<<10&&(Mxcsr()&0x6000)==mode<<13);
            Need(((StatusWord()>>5)&1)==sticky);Need(((Mxcsr()>>5)&1)==sticky);
        }
        SetControls(cw,mx);Need(!(ControlWord()&0xc00)&&!(Mxcsr()&0x6000));
        native_file_record.mask=native_file_record.checks-before;native_file_record.raw_calls=calls;
        native_file_record.file=32;native_file_record.bad_calls=2;
        GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==0);
    }
    Q Ticks() {
        GtosClockReadResult result={};GtosClockReadRequest request={1,GTOS_CLOCK_ID_MONOTONIC,0,(U)&result,sizeof(result)};
        if (Raw(GTOS_SYS_CLOCK_READ,(U)&request,sizeof(request))!=0) Fail(__LINE__);return result.delivered_ticks;
    }
    void FpContexts() {
        const U before=native_file_record.checks,cw=ControlWord(),mx=Mxcsr();Need(!(cw&0xc00)&&!(mx&0x6000));
        for (U mode=0;mode<4;mode++) for (U sticky=0;sticky<2;sticky++) {
            diagnostic_mode=mode;diagnostic_sticky=sticky;
            SetControls((cw&~0xc00U)|(mode<<10),(mx&~0x603fU)|(mode<<13));SetFlags(sticky);
            const Q begin=Ticks();U spins=0;while (Ticks()-begin<2) if (++spins>1000000) Fail(__LINE__);Need(Ticks()-begin>=2);
            errno=1234;Need(Bits(rounder(1.5))==(mode==0||mode==2?0x4000000000000000ULL:0x3ff0000000000000ULL));
            Need(errno==1234);Need((ControlWord()&0xc00)==mode<<10&&(Mxcsr()&0x6000)==mode<<13);
            Need(((StatusWord()>>5)&1)==sticky&&((Mxcsr()>>5)&1)==sticky);
        }
        SetControls(cw,mx);Need(!(ControlWord()&0xc00)&&!(Mxcsr()&0x6000));native_file_record.png_bytes=native_file_record.checks-before;
    }
    void HeapRecord() {
        GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16);
        native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.command=1;
    }
}
extern "C" int NativeV8IsolateMain() {
    Need(native_file_record.mode<6);NumericChecks();
    const char marker[]="NEARBYINT CHECKS PASS\n";Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    const U mode=native_file_record.mode;
    if (mode==0||mode==1||mode==4) {
        if (mode==1) {
            U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
            Need(count>0&&count<80);for (U i=0;i<count;i++) free(pressure[i]);NumericChecks();
        }
        if (mode==4) FpContexts();native_file_record.stage=2;return 0;
    }
    double* input=(double*)malloc(4096);Need(input);*input=1.5;HeapRecord();native_file_record.stage=3;
    if (mode==3) { *(volatile U*)0x100000=1;Fail(__LINE__); }
    for (;;) { if (Bits(rounder(*(volatile double*)input))!=0x4000000000000000ULL) Fail(__LINE__);native_file_record.progress=native_file_record.progress+1; }
}
