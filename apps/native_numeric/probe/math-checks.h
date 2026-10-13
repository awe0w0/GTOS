#ifndef GTOS_NATIVE_MATH_CHECKS_H
#define GTOS_NATIVE_MATH_CHECKS_H
#include "math-record.h"
typedef uint64_t Q;
struct MathFloatCase { U input,ceiling; };
struct MathDoubleCase { Q input,ceiling,floor; };
struct MathRemainderCase { Q input,divisor,remainder; };
static const MathFloatCase mathFloats[]={
    {0x00000000,0x00000000},{0x80000000,0x80000000},
    {0x00000001,0x3f800000},{0x80000001,0x80000000},
    {0x007fffff,0x3f800000},{0x807fffff,0x80000000},
    {0x00800000,0x3f800000},{0x80800000,0x80000000},
    {0x3f7fffff,0x3f800000},{0xbf7fffff,0x80000000},
    {0x3f800000,0x3f800000},{0xbf800000,0xbf800000},
    {0x3f800001,0x40000000},{0xbf800001,0xbf800000},
    {0x3fc00000,0x40000000},{0xbfc00000,0xbf800000},
    {0x40000000,0x40000000},{0xc0000000,0xc0000000},
    {0x40000001,0x40400000},{0xc0000001,0xc0000000},
    {0x4affffff,0x4b000000},{0xcaffffff,0xcafffffe},
    {0x4b000000,0x4b000000},{0x4b000001,0x4b000001},
    {0x7f7fffff,0x7f7fffff},{0xff7fffff,0xff7fffff},
    {0x7f800000,0x7f800000},{0xff800000,0xff800000}
};
static const MathDoubleCase mathDoubles[]={
    {0,0,0},{0x8000000000000000ULL,0x8000000000000000ULL,0x8000000000000000ULL},
    {1,0x3ff0000000000000ULL,0},{0x8000000000000001ULL,0x8000000000000000ULL,0xbff0000000000000ULL},
    {0x000fffffffffffffULL,0x3ff0000000000000ULL,0},{0x800fffffffffffffULL,0x8000000000000000ULL,0xbff0000000000000ULL},
    {0x0010000000000000ULL,0x3ff0000000000000ULL,0},{0x8010000000000000ULL,0x8000000000000000ULL,0xbff0000000000000ULL},
    {0x3fefffffffffffffULL,0x3ff0000000000000ULL,0},{0xbfefffffffffffffULL,0x8000000000000000ULL,0xbff0000000000000ULL},
    {0x3ff0000000000000ULL,0x3ff0000000000000ULL,0x3ff0000000000000ULL},{0xbff0000000000000ULL,0xbff0000000000000ULL,0xbff0000000000000ULL},
    {0x3ff0000000000001ULL,0x4000000000000000ULL,0x3ff0000000000000ULL},{0xbff0000000000001ULL,0xbff0000000000000ULL,0xc000000000000000ULL},
    {0x3ff8000000000000ULL,0x4000000000000000ULL,0x3ff0000000000000ULL},{0xbff8000000000000ULL,0xbff0000000000000ULL,0xc000000000000000ULL},
    {0x4000000000000000ULL,0x4000000000000000ULL,0x4000000000000000ULL},{0xc000000000000000ULL,0xc000000000000000ULL,0xc000000000000000ULL},
    {0x4000000000000001ULL,0x4008000000000000ULL,0x4000000000000000ULL},{0xc000000000000001ULL,0xc000000000000000ULL,0xc008000000000000ULL},
    {0x432fffffffffffffULL,0x4330000000000000ULL,0x432ffffffffffffeULL},{0xc32fffffffffffffULL,0xc32ffffffffffffeULL,0xc330000000000000ULL},
    {0x4330000000000000ULL,0x4330000000000000ULL,0x4330000000000000ULL},{0x4340000000000000ULL,0x4340000000000000ULL,0x4340000000000000ULL},
    {0x4340000000000001ULL,0x4340000000000001ULL,0x4340000000000001ULL},{0xc340000000000001ULL,0xc340000000000001ULL,0xc340000000000001ULL},
    {0x7fefffffffffffffULL,0x7fefffffffffffffULL,0x7fefffffffffffffULL},{0xffefffffffffffffULL,0xffefffffffffffffULL,0xffefffffffffffffULL},
    {0x7ff0000000000000ULL,0x7ff0000000000000ULL,0x7ff0000000000000ULL},{0xfff0000000000000ULL,0xfff0000000000000ULL,0xfff0000000000000ULL}
};
static const MathRemainderCase mathRemainders[]={
    {0,0x3ff0000000000000ULL,0},{0x8000000000000000ULL,0x3ff0000000000000ULL,0x8000000000000000ULL},
    {0x4018000000000000ULL,0x4008000000000000ULL,0},{0xc018000000000000ULL,0x4008000000000000ULL,0x8000000000000000ULL},
    {0x4016000000000000ULL,0x4000000000000000ULL,0x3ff8000000000000ULL},{0xc016000000000000ULL,0x4000000000000000ULL,0xbff8000000000000ULL},
    {0x4016000000000000ULL,0xc000000000000000ULL,0x3ff8000000000000ULL},{0xc016000000000000ULL,0xc000000000000000ULL,0xbff8000000000000ULL},
    {0x3ff4000000000000ULL,0x3fe0000000000000ULL,0x3fd0000000000000ULL},
    {0x3fd0000000000000ULL,0x4000000000000000ULL,0x3fd0000000000000ULL},
    {1,0x7fefffffffffffffULL,1},{3,2,1},{0x8000000000000003ULL,2,0x8000000000000001ULL},
    {0x0010000000000000ULL,3,1},
    {0x7fefffffffffffffULL,0x4008000000000000ULL,0x4000000000000000ULL},
    {0xffefffffffffffffULL,0x4008000000000000ULL,0xc000000000000000ULL},
    {0x7fefffffffffffffULL,1,0},
    {0x7fefffffffffffffULL,0x7ff0000000000000ULL,0x7fefffffffffffffULL},
    {0x3ff0000000000000ULL,0x7ff0000000000000ULL,0x3ff0000000000000ULL},
    {0xbff0000000000000ULL,0xfff0000000000000ULL,0xbff0000000000000ULL}
};
static_assert(sizeof(mathFloats)/sizeof(mathFloats[0])==28,"float vector coverage");
static_assert(sizeof(mathDoubles)/sizeof(mathDoubles[0])==30,"double vector coverage");
static_assert(sizeof(mathRemainders)/sizeof(mathRemainders[0])==20,"remainder vector coverage");
union MathFloat { U bits;float value; };
union MathDouble { Q bits;double value; };
U MathMxcsr() { U value;asm volatile("stmxcsr %0":"=m"(value));return value; }
unsigned short MathControl() { unsigned short value;asm volatile("fnstcw %0":"=m"(value));return value; }
void MathSetMxcsr(U value) { asm volatile("ldmxcsr %0"::"m"(value):"memory"); }
void MathSetControl(unsigned short value) { asm volatile("fldcw %0"::"m"(value):"memory"); }
void MathClear() { asm volatile("fnclex":::"memory");MathSetMxcsr(MathMxcsr()&~63U); }
bool MathNan(Q bits) { return (bits&0x7ff0000000000000ULL)==0x7ff0000000000000ULL&&(bits&0x000fffffffffffffULL); }
U MathChecks() {
    const U before=native_file_record.checks;native_file_record.stage=20;
    const U mxcsr=MathMxcsr();const unsigned short control=MathControl();const int saved_errno=errno;
    Need(math_errhandling==(MATH_ERRNO|MATH_ERREXCEPT));
    Need((mxcsr&0x8040U)==0&&(mxcsr&0x1f80U)==0x1f80U&&(control&63U)==63U);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==0);
    float (*volatile ceilsingle)(float)=ceilf;
    double (*volatile ceildouble)(double)=ceil;
    double (*volatile floordouble)(double)=floor;
    double (*volatile remainder)(double,double)=fmod;
    int (*volatile absolute)(int)=abs;
    for (U mode=0;mode<4;mode++) {
        const U requested=(mxcsr&~0x603fU)|(mode<<13);
        const unsigned short requested_control=(unsigned short)((control&~0x0c00U)|(mode<<10));
        MathSetMxcsr(requested);MathSetControl(requested_control);
        Need(MathMxcsr()==requested&&MathControl()==requested_control);
        for (U i=0;i<28;i++) {
            MathFloat input={mathFloats[i].input},output;errno=123;output.value=ceilsingle(input.value);
            Need(output.bits==mathFloats[i].ceiling);Need(errno==123);
        }
        for (U i=0;i<30;i++) {
            MathDouble input={mathDoubles[i].input},output;errno=123;output.value=ceildouble(input.value);
            Need(output.bits==mathDoubles[i].ceiling);output.value=floordouble(input.value);
            Need(output.bits==mathDoubles[i].floor);Need(errno==123);
        }
        for (U i=0;i<20;i++) {
            MathDouble input={mathRemainders[i].input},divisor={mathRemainders[i].divisor},output;errno=123;
            output.value=remainder(input.value,divisor.value);Need(output.bits==mathRemainders[i].remainder);Need(errno==123);
        }
        const int signed_values[]={0,1,-1,2147483647,-2147483647};
        for (U i=0;i<5;i++) Need(absolute(signed_values[i])==(i>=3?2147483647:i?1:0));
        const Q domain[][3]={
            {0x7ff8000000000001ULL,0x3ff0000000000000ULL,0},
            {0x7ff0000000000001ULL,0x3ff0000000000000ULL,1},
            {0x3ff0000000000000ULL,0x7ff8000000000001ULL,0},
            {0x3ff0000000000000ULL,0x7ff0000000000001ULL,1},
            {0x7ff0000000000000ULL,0x3ff0000000000000ULL,2},
            {0x3ff0000000000000ULL,0,2},
            {0x3ff0000000000000ULL,0x8000000000000000ULL,2},
            {0,0,2}
        };
        for (U i=0;i<8;i++) {
            MathClear();MathDouble input={domain[i][0]},divisor={domain[i][1]},output;errno=123;
            output.value=remainder(input.value,divisor.value);
            Need(MathNan(output.bits)&&(output.bits&0x0008000000000000ULL));
            Need(errno==(domain[i][2]==2?EDOM:123));Need((MathMxcsr()&1U)==(domain[i][2]?1U:0U));
        }
        for (U i=0;i<2;i++) {
            MathFloat input={i?0x7f800001U:0x7fc00001U},output;errno=123;output.value=ceilsingle(input.value);
            Need((output.bits&0x7f800000U)==0x7f800000U&&(output.bits&0x007fffffU));Need(errno==123);
            MathDouble wide={i?0x7ff0000000000001ULL:0x7ff8000000000001ULL},rounded;rounded.value=ceildouble(wide.value);
            Need(MathNan(rounded.bits));rounded.value=floordouble(wide.value);Need(MathNan(rounded.bits));Need(errno==123);
        }
        Need((MathMxcsr()&~63U)==requested&&MathControl()==requested_control);
        Need(Raw(GTOS_SYS_YIELD,0,0)==0);
        Need((MathMxcsr()&~63U)==requested&&MathControl()==requested_control);
    }
    MathClear();MathSetMxcsr(mxcsr);MathSetControl(control);errno=saved_errno;
    Need(MathMxcsr()==mxcsr&&MathControl()==control);Need(gtos_native_heap_query(&heap)==0);
    const U completed=native_file_record.checks-before;Need(completed==MATH_CASE_CHECKS);
    const char marker[]="NATIVE MATH CHECKS PASS\n";
    Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    native_file_record.stage=0;return completed;
}
#endif
