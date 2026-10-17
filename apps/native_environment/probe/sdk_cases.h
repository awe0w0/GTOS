#ifndef SDK_ENVIRONMENT_CASES_H
#define SDK_ENVIRONMENT_CASES_H
extern "C" char* getenv(const char*) noexcept;
namespace perfetto { namespace base { bool StderrSupportsColor(); } }
typedef void (*Initializer)();
extern "C" Initializer __init_array_start[],__init_array_end[];
static unsigned startupReady;
static bool Same(const char* value,const char* expected) {
    if (!value || !expected) return value==expected;
    while (*value && *value==*expected) {++value;++expected;}
    return *value==*expected;
}
static void KnownEnvironment() {
    const unsigned c=cpu_record.environmentCase;CHECK(c<8);
    const char* force[]={"1","","0","1","",0,"1",0};
    const char* no[]={"","1","1","1","","1",0,0};
    const bool color[]={true,false,true,true,false,false,true,false};
    CHECK(!getenv(0) && !getenv("") && !getenv("MISSING") && !getenv("FORCE") && !getenv("FORCE_COLOR_EXTRA"));
    CHECK(Same(getenv("FORCE_COLOR"),force[c]) && Same(getenv("NO_COLOR"),no[c]));
    CHECK((unsigned)getenv("FORCE_COLOR")==cpu_record.environmentPointer);
    CHECK(perfetto::base::StderrSupportsColor()==color[c]);
    CHECK(Same(getenv("A"),c==0?"x=y":0) && !getenv("AB"));
    CHECK(Same(getenv("EMPTY"),c==0?"":0) && Same(getenv("PEER"),c==1?"alone":0));
    if (c==0) {
        const char prefix[]="FORCE_COLOR=1\0NO_COLOR=\0A=x=y\0EMPTY=\0";
        CHECK(Same(getenv("N00"),""));const char* last=getenv("N59");CHECK(last);
        const unsigned length=3824-(sizeof(prefix)-1+59*5+4)-1;
        for (unsigned i=0;i<length;++i) CHECK(last[i]=='Z');CHECK(last[length]==0);
    } else CHECK(!getenv("N00") && !getenv("N59"));
}
static void EnvironmentConstructor() __attribute__((constructor));
static void EnvironmentConstructor() {
    CHECK(startupReady==1 && gtos_native_environment_initialize()==0);
    KnownEnvironment();++cpu_record.constructorCalls;
}
static void SsePattern(unsigned role) {
    unsigned in[4] __attribute__((aligned(16)))={0xC0010000U+role,0xABCD1000U+role,0x56780000U+role,0xA5A50000U+role};
    unsigned out[4] __attribute__((aligned(16)))={};
    const unsigned mx=0x1F80U|((role&3)<<13);unsigned observedMx=0;
    const unsigned short cw=(unsigned short)(0x037FU|((role&3)<<10));unsigned short observedCw=0;
    int status;
    asm volatile("ldmxcsr %[mx]; fldcw %[cw]; movdqa %[source],%%xmm0; int $0x80; movdqa %%xmm0,%[destination]; stmxcsr %[mxout]; fnstcw %[cwout]"
                 : "=a"(status),[cwout]"=m"(observedCw),[destination]"=m"(out),[mxout]"=m"(observedMx)
                 : "0"(GTOS_SYS_YIELD),[cw]"m"(cw),[source]"m"(in),[mx]"m"(mx),"b"(0),"c"(0)
                 : "memory","cc","xmm0");
    CHECK(status==0 && observedMx==mx && observedCw==cw);
    for (unsigned i=0;i<4;++i) CHECK(out[i]==in[i]);
}
static void ValidatorCases() {
    GtosVmReserveResult region={};GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&region};
    CHECK(Call(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
    GtosVmRangeRequest commit={1,region.handle,4096,8192,GTOS_VM_READ_WRITE};
    CHECK(Call(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&commit,sizeof(commit))==0);
    GtosEnvironmentReadResult* original=(GtosEnvironmentReadResult*)(region.base+4096);
    GtosEnvironmentReadResult* bad=(GtosEnvironmentReadResult*)(region.base+8192);
    GtosEnvironmentReadRequest request={1,0,(unsigned)original,4096};
    CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==0 && gtos_native::ValidEnvironment(*original));
    for (unsigned i=0;i<sizeof(*original);++i) ((unsigned char*)bad)[i]=((unsigned char*)original)[i];
    bad->version=2;CHECK(!gtos_native::ValidEnvironment(*bad));bad->version=1;
    bad->reserved=1;CHECK(!gtos_native::ValidEnvironment(*bad));bad->reserved=0;
    const unsigned count=bad->count,bytes=bad->data_bytes,offset=bad->offsets[0];
    bad->count=65;CHECK(!gtos_native::ValidEnvironment(*bad));bad->count=count;
    bad->data_bytes=3825;CHECK(!gtos_native::ValidEnvironment(*bad));bad->data_bytes=bytes;
    bad->offsets[0]=~0U;CHECK(!gtos_native::ValidEnvironment(*bad));bad->offsets[0]=offset;
    bad->data[3823]=1;CHECK(!gtos_native::ValidEnvironment(*bad));bad->data[3823]=0;
    CHECK(gtos_native::ValidEnvironment(*bad));
    for (unsigned i=0;i<sizeof(*bad);++i) ((unsigned char*)bad)[i]=0;
    const char duplicate[]="A=1\0A=2\0";bad->version=1;bad->count=2;bad->data_bytes=sizeof(duplicate)-1;bad->offsets[1]=4;
    for (unsigned i=0;i<sizeof(duplicate)-1;++i) bad->data[i]=duplicate[i];
    CHECK(!gtos_native::ValidEnvironment(*bad));
    for (unsigned i=0;i<sizeof(*bad);++i) ((unsigned char*)bad)[i]=0;
    const char prefix[]="A=1\0AB=2\0";bad->version=1;bad->count=2;bad->data_bytes=sizeof(prefix)-1;bad->offsets[1]=4;
    for (unsigned i=0;i<sizeof(prefix)-1;++i) bad->data[i]=prefix[i];
    CHECK(gtos_native::ValidEnvironment(*bad));
    GtosVmControlRequest release={1,region.handle,0};CHECK(Call(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
}
#endif
