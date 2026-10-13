#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <process/abi.h>
#include <process/clock_abi.h>
#include "heap.h"
#include "record.h"
#include "src/__support/FPUtil/rounding_mode.h"
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    using U=unsigned;using Q=uint64_t;using B=unsigned char;
    static_assert(sizeof(float)==4&&sizeof(double)==8&&sizeof(long double)==12,"actual IA32 floating ABI");
    static_assert(__FLT_MANT_DIG__==24&&__DBL_MANT_DIG__==53&&__LDBL_MANT_DIG__==64,"actual IEEE significands");
    static_assert(__LDBL_MAX_EXP__==16384,"actual 80-bit exponent");
    decltype(&strtof) volatile parse_float=&strtof;
    decltype(&strtod) volatile parse_double=&strtod;
    decltype(&strtold) volatile parse_long=&strtold;
    void* pressure[80];const U readonly_end=0;
    U diagnostic_actual,diagnostic_expected,diagnostic_cw,diagnostic_mx,diagnostic_backend,diagnostic_expected_mode,diagnostic_mode;
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;
        native_file_record.file=diagnostic_actual;native_file_record.invalid_io=diagnostic_expected;
        native_file_record.directory=diagnostic_cw;native_file_record.foreign=diagnostic_mx;
        native_file_record.command=diagnostic_backend;native_file_record.png_bytes=diagnostic_expected_mode;native_file_record.progress=diagnostic_mode;
        Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    void Check(bool value,U id) { NeedAt(value,id); }
#include "sscanf-cases-v1.h"
    U Length(const char* text) { U size=0;while (text[size]) size++;return size; }
    U FBits(float value) { U bits;memcpy(&bits,&value,4);return bits; }
    Q DBits(double value) { Q bits;memcpy(&bits,&value,8);return bits; }
    struct LongBits { Q mant;U exp; };
    LongBits LBits(long double value) {
        B bytes[12];memcpy(bytes,&value,12);Q mant=0;
        for (U i=0;i<8;i++) mant|=(Q)bytes[i]<<(i*8);
        return {mant,(U)bytes[8]|(U)bytes[9]<<8};
    }
    bool LongEquals(long double value,Q mant,U exp) { LongBits bits=LBits(value);return bits.mant==mant&&bits.exp==exp; }
#include "vectors.h"
    void CheckRow(const char* text,const ParseCase& row,bool nullable) {
        char* end=(char*)0xffffffff;errno=1234;
        Need(FBits(parse_float(text,nullable?nullptr:&end))==row.fbits);
        if (!nullable) Need(end==text+row.end);
        Need(errno==(row.ferror?ERANGE:1234));
        end=(char*)0xffffffff;errno=1234;
        Need(DBits(parse_double(text,nullable?nullptr:&end))==row.dbits);
        if (!nullable) Need(end==text+row.end);
        Need(errno==(row.derror?ERANGE:1234));
        end=(char*)0xffffffff;errno=1234;
        Need(LongEquals(parse_long(text,nullable?nullptr:&end),row.lmant,row.lexp));
        if (!nullable) Need(end==text+row.end);
        Need(errno==(row.lerror?ERANGE:1234));
    }
    void FiniteChecks() {
        const U before=native_file_record.checks;char buffer[80];
        for (U offset=0;offset<8;offset++) for (const ParseCase& row:parseCases) {
            U size=Length(row.text);for (U i=0;i<=size;i++) buffer[offset+i]=row.text[i];
            CheckRow(buffer+offset,row,false);CheckRow(buffer+offset,row,true);
        }
        native_file_record.mask=native_file_record.checks-before;
    }
    struct Special { const char* text;U end;bool negative,nan; };
    void CheckSpecial(const char* text,const Special& row) {
        char* end=nullptr;errno=1234;U f=FBits(parse_float(text,&end));
        Need((f>>31)==row.negative&&(f&0x7f800000)==0x7f800000&&
            (row.nan?((f&0x00400000)!=0):((f&0x007fffff)==0)));Need(end==text+row.end);Need(errno==1234);
        end=nullptr;errno=1234;Q d=DBits(parse_double(text,&end));
        Need((d>>63)==row.negative&&(d&0x7ff0000000000000ULL)==0x7ff0000000000000ULL&&
            (row.nan?((d&0x0008000000000000ULL)!=0):((d&0x000fffffffffffffULL)==0)));Need(end==text+row.end);Need(errno==1234);
        end=nullptr;errno=1234;LongBits l=LBits(parse_long(text,&end));
        Need((l.exp>>15)==row.negative&&(l.exp&0x7fff)==0x7fff&&(l.mant&0x8000000000000000ULL)&&
            (row.nan?((l.mant&0x4000000000000000ULL)!=0):(l.mant==0x8000000000000000ULL)));Need(end==text+row.end);Need(errno==1234);
    }
    void SpecialChecks() {
        const U before=native_file_record.checks;
        const Special rows[]={{"infTAIL",3,false,false},{"-Infinity!",9,true,false},{"+INFINITY",9,false,false},
            {"nan",3,false,true},{"-nan()",6,true,true},{"NaN(123)tail",8,false,true},{"nan(bad!)",3,false,true},{"NaN(",3,false,true}};
        for (const Special& row:rows) CheckSpecial(row.text,row);
        native_file_record.raw_calls=native_file_record.checks-before;
    }
    void LongDigits() {
        const U before=native_file_record.checks;char text[2048];text[0]='1';text[1]='.';
        for (U i=2;i<2002;i++) text[i]='0';memcpy(text+2002,"7tail",6);
        const ParseCase row={text,2003,0,0,0,0x3f800000,0x3ff0000000000000ULL,0x8000000000000000ULL,0x3fff};
        CheckRow(text,row,false);native_file_record.directory=native_file_record.checks-before;
    }
    void EdgeChecks() {
        const U before=native_file_record.checks;char* edge=(char*)0x40040ff8;memcpy(edge,"0x1.8p1",8);
        const ParseCase three={edge,7,0,0,0,0x40400000,0x4008000000000000ULL,0xc000000000000000ULL,0x4000};CheckRow(edge,three,false);
        edge=(char*)0x40040ffc;memcpy(edge,"inf",4);CheckSpecial(edge,{edge,3,false,false});
        memcpy(edge,"nan",4);CheckSpecial(edge,{edge,3,false,true});
        edge=(char*)0x40040ffe;memcpy(edge,"0",2);const ParseCase zero={edge,1,0,0,0,0,0,0,0};CheckRow(edge,zero,false);
        edge=(char*)0x40040fff;*edge=0;const ParseCase empty={edge,0,0,0,0,0,0,0,0};CheckRow(edge,empty,false);
        native_file_record.bad_calls=native_file_record.checks-before;
    }
    U ControlWord() { unsigned short word;asm volatile("fnstcw %0":"=m"(word));return word; }
    U Mxcsr() { U word;asm volatile("stmxcsr %0":"=m"(word));return word; }
    void SetControls(U cw,U mx) { unsigned short word=(unsigned short)cw;asm volatile("fldcw %0; ldmxcsr %1"::"m"(word),"m"(mx):"memory"); }
    void RoundingChecks() {
        const U before=native_file_record.checks,cw=ControlWord(),mx=Mxcsr();Need(!(cw&0xc00)&&!(mx&0x6000));
        for (U mode=0;mode<4;mode++) for (U negative=0;negative<2;negative++) {
            SetControls((cw&~0xc00U)|(mode<<10),(mx&~0x6000U)|(mode<<13));
            diagnostic_mode=mode;diagnostic_cw=ControlWord();diagnostic_mx=Mxcsr();
            if ((diagnostic_cw&0xc00)!=mode<<10||(diagnostic_mx&0x6000)!=mode<<13) Fail(__LINE__);
            const U codes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};diagnostic_expected_mode=codes[mode];
            diagnostic_backend=(U)__llvm_libc_cr::fputil::quick_get_round();
            if (diagnostic_backend!=diagnostic_expected_mode) Fail(__LINE__);
            const bool high=mode==0||(!negative&&mode==2)||(negative&&mode==1);
            const char* ftext=negative?"-0x1.0000018p0":"0x1.0000018p0";
            char* end=nullptr;errno=1234;diagnostic_expected=(0x3f800000U+(high?1U:0U))|(negative?0x80000000U:0U);
            diagnostic_actual=FBits(parse_float(ftext,&end));diagnostic_cw=ControlWord();diagnostic_mx=Mxcsr();
            if ((diagnostic_cw&0xc00)!=mode<<10||(diagnostic_mx&0x6000)!=mode<<13) Fail(__LINE__);
            Need(diagnostic_actual==diagnostic_expected);
            Need(end==ftext+Length(ftext));Need(errno==1234);
            const char* dtext=negative?"-0x1.0000000000000cp0":"0x1.0000000000000cp0";
            end=nullptr;errno=1234;Need(DBits(parse_double(dtext,&end))==((0x3ff0000000000000ULL+(high?1U:0U))|(negative?0x8000000000000000ULL:0ULL)));
            Need(end==dtext+Length(dtext));Need(errno==1234);
            const char* ltext=negative?"-0x1.00000000000000018p0":"0x1.00000000000000018p0";
            end=nullptr;errno=1234;Need(LongEquals(parse_long(ltext,&end),0x8000000000000000ULL+(high?1U:0U),negative?0xbfff:0x3fff));
            Need(end==ltext+Length(ltext));Need(errno==1234);Need((ControlWord()&0xc00)==mode<<10&&(Mxcsr()&0x6000)==mode<<13);
        }
        SetControls(cw,mx);Need(!(ControlWord()&0xc00)&&!(Mxcsr()&0x6000));native_file_record.foreign=native_file_record.checks-before;
    }
    void NumericChecks() {
        FiniteChecks();SpecialChecks();LongDigits();EdgeChecks();RoundingChecks();
        const U before=native_file_record.checks;RunScanCases();native_file_record.file=native_file_record.checks-before;
        GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==0);
    }
    Q Ticks() {
        GtosClockReadResult result={};GtosClockReadRequest request={1,GTOS_CLOCK_ID_MONOTONIC,0,(U)&result,sizeof(result)};
        if (Raw(GTOS_SYS_CLOCK_READ,(U)&request,sizeof(request))!=0) Fail(__LINE__);return result.delivered_ticks;
    }
    void FpContexts() {
        const U before=native_file_record.checks,cw=ControlWord(),mx=Mxcsr();Need(!(cw&0xc00)&&!(mx&0x6000));
        for (U mode=0;mode<4;mode++) {
            SetControls((cw&~0xc00U)|(mode<<10),(mx&~0x6000U)|(mode<<13));
            const Q begin=Ticks();U spins=0;while (Ticks()-begin<2) if (++spins>1000000) Fail(__LINE__);Need(Ticks()-begin>=2);
            const char text[]="0x1.0000018p0";char* end=nullptr;errno=1234;
            Need(FBits(parse_float(text,&end))==0x3f800000U+(mode==0||mode==2?1U:0U));Need(end==text+sizeof(text)-1);Need(errno==1234);
            Need((ControlWord()&0xc00)==mode<<10&&(Mxcsr()&0x6000)==mode<<13);
        }
        SetControls(cw,mx);Need(!(ControlWord()&0xc00)&&!(Mxcsr()&0x6000));native_file_record.png_bytes=native_file_record.checks-before;
    }
    void HeapRecord() {
        GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16);
        native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.command=1;
    }
}
extern "C" int NativeV8IsolateMain() {
    Need(native_file_record.mode<8);NumericChecks();
    const char marker[]="FLOAT CONVERSION CHECKS PASS\n";Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    const U mode=native_file_record.mode;
    if (!mode||mode==6||mode==7) {
        if (mode==6) {
            U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
            Need(count>0&&count<80);for (U i=0;i<count;i++) free(pressure[i]);NumericChecks();
        }
        if (mode==7) FpContexts();native_file_record.stage=2;return 0;
    }
    native_file_record.stage=3;
    if (mode==1) { char* edge=(char*)0x40040ff0;for (U i=0;i<16;i++) edge[i]='1';(void)parse_float(edge,nullptr);Fail(__LINE__); }
    if (mode==2) { native_file_record.progress=(U)&readonly_end;(void)parse_double("1.5",(char**)&readonly_end);Fail(__LINE__); }
    if (mode==3) { (void)parse_long("1.5",(char**)0x40041000);Fail(__LINE__); }
    char* text=(char*)malloc(4096);Need(text);text[0]='1';text[1]='.';for (U i=2;i<3002;i++) text[i]='0';text[3002]='7';text[3003]=0;HeapRecord();
    if (mode==5) { *(volatile U*)0x100000=1;Fail(__LINE__); }
    for (;;) { if (!LongEquals(parse_long(text,nullptr),0x8000000000000000ULL,0x3fff)) Fail(__LINE__);native_file_record.progress=native_file_record.progress+1; }
}
