#ifndef GTOS_RUNTIME_GAP_CASES_H
#define GTOS_RUNTIME_GAP_CASES_H
#include "pow-reference-v1.h"
    unsigned long long RuntimeBits(double value) {
        union { double value;unsigned long long bits; } result={value};return result.bits;
    }
    double RuntimeDouble(unsigned long long bits) {
        union { unsigned long long bits;double value; } result={bits};return result.value;
    }
    void RuntimeClearFP() {
        unsigned mxcsr;asm volatile("fnclex;stmxcsr %0":"=m"(mxcsr));
        mxcsr&=~63U;asm volatile("ldmxcsr %0"::"m"(mxcsr):"memory");
    }
    unsigned RuntimeRawFlags() {
        unsigned mxcsr;unsigned short status;
        asm volatile("stmxcsr %0;fnstsw %1":"=m"(mxcsr),"=m"(status));return (mxcsr|status)&63U;
    }
    unsigned RuntimeFlags() {
        const unsigned raw=RuntimeRawFlags();
        // The fixed C SDK's public exception codes differ from x87/MXCSR bits.
        return ((raw&1)?FE_INVALID:0U)|((raw&2)?FE_DENORM:0U)|((raw&4)?FE_DIVBYZERO:0U)
            |((raw&8)?FE_OVERFLOW:0U)|((raw&16)?FE_UNDERFLOW:0U)|((raw&32)?FE_INEXACT:0U);
    }
    void RuntimeRound(unsigned mode) {
        unsigned mxcsr;unsigned short control;
        asm volatile("stmxcsr %0;fnstcw %1":"=m"(mxcsr),"=m"(control));
        mxcsr=(mxcsr&~0x6000U)|(mode<<13);control=(unsigned short)((control&~0xc00U)|(mode<<10));
        asm volatile("fnclex;ldmxcsr %0;fldcw %1"::"m"(mxcsr),"m"(control):"memory");
        RuntimeClearFP();
    }
    void RuntimeValues() {
        static_assert(sizeof(long)==4 && sizeof(unsigned long)==4,"actual IA32 integer ABI");
        const int saved=errno;unsigned original_mxcsr;unsigned short original_control;
        unsigned char original_x87[28];
        asm volatile("fnstenv %0;fldenv %0":"=m"(original_x87)::"memory");
        asm volatile("stmxcsr %0;fnstcw %1":"=m"(original_mxcsr),"=m"(original_control));
        double (*const exponent)(double,int)=::ldexp;double (*const power)(double,double)=::pow;
        double (*const truncate)(double)=::trunc;long (*const round_integer)(double)=::lrint;
        const unsigned offset=v8::base::gtos::CurrentProcessInfo().thread_id&3;
        for (unsigned iteration=0;iteration<4;iteration++) {
            const unsigned mode=(iteration+offset)&3;RuntimeRound(mode);
            const long positive[]={2,1,2,1},negative[]={-2,-2,-1,-1};
            errno=EDOM;
            Need(round_integer(1.5)==positive[mode] && round_integer(-1.5)==negative[mode]);
            Need(round_integer(-2147483648.0)==LONG_MIN && errno==EDOM);
            RuntimeClearFP();errno=0;
            const long invalid_integer=round_integer(2147483648.0);const int invalid_errno=errno;
            native_file_record.mask=(unsigned)invalid_integer;native_file_record.foreign=(unsigned)invalid_errno;
            native_file_record.bad_calls=RuntimeRawFlags();
            Need(invalid_integer==LONG_MAX && invalid_errno==EDOM && (RuntimeFlags()&FE_INVALID));
            RuntimeClearFP();errno=EDOM;
            const unsigned long long values[]={0,0x8000000000000000ULL,0x3ff8000000000000ULL,
                0xbff8000000000000ULL,1,0x8000000000000001ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL};
            const unsigned long long integral[]={0,0x8000000000000000ULL,0x3ff0000000000000ULL,
                0xbff0000000000000ULL,0,0x8000000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL};
            for (unsigned i=0;i<8;i++) Need(RuntimeBits(truncate(RuntimeDouble(values[i])))==integral[i]);
            Need(RuntimeBits(exponent(-0.0,INT_MAX))==0x8000000000000000ULL);
            Need(RuntimeBits(exponent(1.0,-1074))==1 && exponent(1.0,-1022)==0x1p-1022);
            Need(exponent(0x1p-1022,1022)==1.0 && exponent(1.5,2)==6.0 && errno==EDOM);
            Need(RuntimeBits(exponent(RuntimeDouble(1),2097))==0x7fe0000000000000ULL && errno==EDOM);
            Need(RuntimeBits(exponent(0x1p1023,-2097))==1 && errno==EDOM);
            Need(power(-2.0,3.0)==-8.0 && power(1.25,3.0)==1.953125 && power(4.0,0.5)==2.0);
            Need(RuntimeBits(power(-0.0,3.0))==0x8000000000000000ULL && power(0.0,0.0)==1.0);
            for (const auto& reference:runtimePowReferences)
                Need(RuntimeBits(power(reference.base,reference.exponent))==reference.expected[mode]);
            const double infinity=RuntimeDouble(0x7ff0000000000000ULL),nan=RuntimeDouble(0x7ff8000000001234ULL);
            Need(power(nan,0.0)==1.0 && power(1.0,nan)==1.0 && power(infinity,-1.0)==0.0);
            Need(RuntimeBits(exponent(infinity,INT_MIN))==0x7ff0000000000000ULL && __builtin_isnan(truncate(nan)));
            RuntimeClearFP();errno=0;
            const double overflow=exponent(1.0,INT_MAX);
            const int overflow_errno=errno;
            native_file_record.mask=(unsigned)(RuntimeBits(overflow)>>32);
            native_file_record.foreign=(unsigned)overflow_errno;native_file_record.bad_calls=RuntimeRawFlags();
            Need(RuntimeBits(overflow)==((mode==1 || mode==3)?0x7fefffffffffffffULL:0x7ff0000000000000ULL));
            Need(overflow_errno==ERANGE && (RuntimeFlags()&(FE_OVERFLOW|FE_INEXACT))==(FE_OVERFLOW|FE_INEXACT));
            RuntimeClearFP();errno=0;
            Need(RuntimeBits(exponent(1.0,INT_MIN))==(mode==2?1ULL:0ULL));
            Need(errno==ERANGE && (RuntimeFlags()&FE_UNDERFLOW));
            RuntimeClearFP();errno=0;
            Need(RuntimeBits(exponent(-1.0,INT_MAX))==((mode==2 || mode==3)?0xffefffffffffffffULL:0xfff0000000000000ULL));
            Need(errno==ERANGE && (RuntimeFlags()&(FE_OVERFLOW|FE_INEXACT))==(FE_OVERFLOW|FE_INEXACT));
            RuntimeClearFP();errno=0;
            Need(RuntimeBits(exponent(-1.0,INT_MIN))==(mode==1?0x8000000000000001ULL:0x8000000000000000ULL));
            Need(errno==ERANGE && (RuntimeFlags()&(FE_UNDERFLOW|FE_INEXACT))==(FE_UNDERFLOW|FE_INEXACT));
            RuntimeClearFP();errno=0;
            Need(RuntimeBits(exponent(1.0,1024))==((mode==1 || mode==3)?0x7fefffffffffffffULL:0x7ff0000000000000ULL));
            Need(errno==ERANGE && (RuntimeFlags()&(FE_OVERFLOW|FE_INEXACT))==(FE_OVERFLOW|FE_INEXACT));
            RuntimeClearFP();errno=0;
            Need(RuntimeBits(exponent(1.0,-1075))==(mode==2?1ULL:0ULL));
            Need(errno==ERANGE && (RuntimeFlags()&(FE_UNDERFLOW|FE_INEXACT))==(FE_UNDERFLOW|FE_INEXACT));
            RuntimeClearFP();errno=0;
            Need(__builtin_isnan(power(-2.0,0.5)) && errno==EDOM && (RuntimeFlags()&FE_INVALID));
            RuntimeClearFP();errno=EDOM;
            v8::base::gtos::Yield();
            unsigned mxcsr;unsigned short control;
            asm volatile("stmxcsr %0;fnstcw %1":"=m"(mxcsr),"=m"(control));
            Need((mxcsr&~63U)==((original_mxcsr&~(63U|0x6000U))|(mode<<13)));
            Need(control==(unsigned short)((original_control&~0xc00U)|(mode<<10)) && errno==EDOM);
        }
        asm volatile("fnclex;ldmxcsr %0;fldcw %1"::"m"(original_mxcsr),"m"(original_control):"memory");
        const unsigned inputs[]={0,0x80000000U,1,0x80000001U,0x7f800000U,0xff800000U,0xffc12345U};
        for (unsigned bits:inputs) {
            union { unsigned bits;float value; } input={bits},output;
            output.value=fabsf(input.value);Need(output.bits==(bits&0x7fffffffU));
        }
        for (int value=-1;value<256;value++) {
            const bool expected=(value>='0' && value<='9') || (value>='A' && value<='Z') || (value>='a' && value<='z');
            Need((isalnum(value)!=0)==expected);
        }
        for (int base=2;base<=36;base++) {
            char* end=nullptr;errno=EDOM;
            Need(strtoul("10!",&end,base)==(unsigned)base && *end=='!' && errno==EDOM);
        }
        struct IntegerCase { const char* text;int base;unsigned long value;unsigned end;int error; };
        const IntegerCase integers[]={
            {"4294967295!",10,ULONG_MAX,10,EDOM},{"4294967296!",10,ULONG_MAX,10,ERANGE},
            {"-1!",10,ULONG_MAX,2,EDOM},{"-4294967295!",10,1,11,EDOM},
            {"-4294967296!",10,ULONG_MAX,11,ERANGE},{"  +0x10!",0,16,7,EDOM},
            {"077!",0,63,3,EDOM},{"08!",0,0,1,EDOM},{"xyz",10,0,0,EDOM},{"  -",10,0,0,EDOM},
        };
        for (const auto& value:integers) {
            char* end=nullptr;errno=EDOM;
            Need(strtoul(value.text,&end,value.base)==value.value && end==value.text+value.end && errno==value.error);
        }
        for (int base:{-1,1,37}) {
            char untouched='Q';char* end=&untouched;errno=0;
            Need(strtoul("10",&end,base)==0 && errno==EINVAL && end==&untouched);
        }
        char bytes[256];for (unsigned i=0;i<255;i++) bytes[i]=(char)(i+1);bytes[255]=0;
        errno=EDOM;char* copy=strdup(bytes);Need(copy && copy!=bytes && errno==EDOM);
        for (unsigned i=0;i<256;i++) Need(copy[i]==bytes[i]);copy[0]='Q';Need(bytes[0]==1);free(copy);
        copy=strdup("");Need(copy && copy[0]==0);free(copy);
        const char embedded[]={'A',0,'B',0};copy=strdup(embedded);Need(copy && copy[0]=='A' && copy[1]==0);free(copy);
        asm volatile("fldenv %0;ldmxcsr %1"::"m"(original_x87),"m"(original_mxcsr):"memory");
        errno=saved;
    }
    void RuntimePagesAndAllocation() {
        const int saved=errno;
        GtosVmReserveResult result_vm={};
        GtosVmReserveRequest reserve={GTOS_VM_ABI_VERSION,8192,4096,0,(unsigned)&result_vm};
        Need(Raw(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
        GtosVmRangeRequest access={GTOS_VM_ABI_VERSION,result_vm.handle,0,4096,GTOS_VM_READ_WRITE};
        Need(Raw(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&access,sizeof(access))==0);
        char* source=(char*)result_vm.base+4093;source[0]='Q';source[1]=(char)0xff;source[2]=0;
        char* copy=strdup(source);Need(copy && copy[0]=='Q' && (unsigned char)copy[1]==255 && copy[2]==0);free(copy);
        GtosVmControlRequest release={GTOS_VM_ABI_VERSION,result_vm.handle,0};
        Need(Raw(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
        void* held[128]={};unsigned count=0;
        for (;count<128;count++) { held[count]=malloc(1024);if (!held[count]) break; }
        Need(count>0 && count<128);
        char large[2049];for (unsigned i=0;i<2048;i++) large[i]='D';large[2048]=0;
        errno=0;copy=strdup(large);Need(!copy && errno==ENOMEM);
        for (unsigned i=0;i<count;i++) free(held[i]);
        copy=strdup(large);Need(copy && strlen(copy)==2048);free(copy);errno=saved;
    }
    void RuntimeOutput() {
        const int saved=errno;
        Need(freopen("/native/runtime-printf.txt","w+",stdout)==stdout);
        char payload[2050];for (unsigned i=0;i<2049;i++) payload[i]=(char)('A'+i%26);payload[2049]=0;
        errno=EDOM;Need(printf("%08x|%+.2f|%c|%s!",42U,1.25,0,payload)==2067 && errno==EDOM);
        Need(fflush(stdout)==0 && fseek(stdout,0,SEEK_SET)==0);
        const unsigned char prefix[]={'0','0','0','0','0','0','2','a','|','+','1','.','2','5','|',0,'|'};
        for (unsigned char byte:prefix) Need(getc(stdout)==byte);
        for (unsigned i=0;i<2049;i++) Need(getc(stdout)==payload[i]);
        Need(getc(stdout)=='!' && getc(stdout)==EOF && feof(stdout));
        Need(freopen("/native/runtime-printf.txt","r",stdout)==stdout);errno=0;
        Need(printf("%s",payload)==-1 && errno==EBADF && ferror(stdout));clearerr(stdout);
        Need(freopen("/native/runtime-console.txt","w",stdout)==stdout);errno=saved;
    }
#endif
