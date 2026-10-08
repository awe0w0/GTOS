#include "record.h"
#include <process/abi.h>
#include <process/vm_abi.h>
extern "C" {
__attribute__((section(".data.process_info_record"),used))
volatile ProcessInfoProbeRecord native_process_info_record={1,GTOS_PROCESS_INFO_PROBE_MODE,0,0,0,0,0,0,{},{},0,0,0,0,0,0,0,0};
}
static int Call(unsigned operation,unsigned first,unsigned second) {
    int result;
    asm volatile("int $0x80":"=a"(result):"a"(operation),"b"(first),"c"(second):"memory","cc");
    return result;
}
static void Fail(unsigned number) __attribute__((noreturn));
static void Fail(unsigned number) {
    native_process_info_record.error=0x4a010000U|number;
    Call(GTOS_SYS_EXIT,native_process_info_record.error,0);asm volatile("ud2");
    __builtin_unreachable();
}
static void Require(bool value,unsigned number) {
    if(!value)Fail(number);
    native_process_info_record.checks=native_process_info_record.checks+1;
}
static void Copy(volatile void* destination,const volatile void* source,unsigned bytes) {
    volatile unsigned char* out=static_cast<volatile unsigned char*>(destination);
    const volatile unsigned char* in=static_cast<const volatile unsigned char*>(source);
    for(unsigned i=0;i<bytes;++i)out[i]=in[i];
}
static GtosProcessInfoRequest Request(unsigned output) {
    const GtosProcessInfoRequest request={1,0,output,sizeof(GtosProcessInfoResult)};return request;
}
static int Query(unsigned input,unsigned bytes,int expected) {
    const int result=Call(GTOS_SYS_PROCESS_INFO,input,bytes);
    native_process_info_record.raw_query_count=native_process_info_record.raw_query_count+1;
    native_process_info_record.last_result=static_cast<unsigned>(result);
    Require(result==expected,1);
    if(result==0)native_process_info_record.successful_queries=native_process_info_record.successful_queries+1;
    else native_process_info_record.failed_queries=native_process_info_record.failed_queries+1;
    return result;
}
static GtosProcessInfoResult Load(unsigned address) {
    GtosProcessInfoResult result;Copy(&result,reinterpret_cast<const void*>(address),sizeof(result));return result;
}
static void Metadata(const GtosProcessInfoResult& value) {
    unsigned esp;asm volatile("movl %%esp,%0":"=r"(esp));
    Require(value.version==1 && value.process_id && value.thread_id==value.process_id,2);
    Require(value.stack_begin==0xbfffd000U && value.stack_end==0xbffff000U
        && esp>=value.stack_begin && esp<value.stack_end,3);
    Require(value.user_begin==0x40000000U && value.user_end==0xc0000000U
        && value.page_bytes==4096 && value.maximum_pages==256 && value.maximum_regions==32
        && value.maximum_parallel_threads==1 && value.scheduler_cpu_count==1,4);
    if(native_process_info_record.first.version)
        Require(value.process_id==native_process_info_record.first.process_id,5);
}
static void Sample() {
    GtosProcessInfoResult value={};
    GtosProcessInfoRequest request=Request(reinterpret_cast<unsigned>(&value));
    Query(reinterpret_cast<unsigned>(&request),sizeof(request),0);Metadata(value);
    if(!native_process_info_record.first.version)Copy(&native_process_info_record.first,&value,sizeof(value));
    Copy(&native_process_info_record.last,&value,sizeof(value));
}
static void Fill(unsigned base) {
    volatile unsigned char* bytes=reinterpret_cast<volatile unsigned char*>(base);
    for(unsigned i=0;i<8192;++i)bytes[i]=0xa5U;
}
static void Pattern(unsigned base,unsigned requestAddress,const GtosProcessInfoRequest* request,
                    unsigned resultAddress,const GtosProcessInfoResult* result) {
    const volatile unsigned char* bytes=reinterpret_cast<const volatile unsigned char*>(base);
    for(unsigned i=0;i<8192;++i) {
        const unsigned address=base+i;
        unsigned char expected=0xa5U;
        if(request && address>=requestAddress && address-requestAddress<sizeof(*request))
            expected=reinterpret_cast<const unsigned char*>(request)[address-requestAddress];
        if(result && address>=resultAddress && address-resultAddress<sizeof(*result))
            expected=reinterpret_cast<const unsigned char*>(result)[address-resultAddress];
        if(bytes[i]!=expected)Fail(6);
    }
    Require(true,6);
    native_process_info_record.sentinel_checks=native_process_info_record.sentinel_checks+1;
    native_process_info_record.sentinel_bytes=native_process_info_record.sentinel_bytes+8192;
}
static void Permissions(unsigned handle,unsigned protection) {
    GtosVmRangeRequest request={1,handle,4096,4096,protection};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS,reinterpret_cast<unsigned>(&request),sizeof(request))==0,7);
}
static void KeepPages() {
    GtosVmReserveResult result={};
    GtosVmReserveRequest request={1,8192,4096,0,reinterpret_cast<unsigned>(&result)};
    Require(Call(GTOS_SYS_VM_RESERVE,reinterpret_cast<unsigned>(&request),sizeof(request))==0,8);
    Require(result.version==1 && result.handle && result.base==0x80000000U
        && result.length==8192 && result.page_size==4096,9);
    GtosVmRangeRequest range={1,result.handle,0,8192,GTOS_VM_READ_WRITE};
    Require(Call(GTOS_SYS_VM_SET_PERMISSIONS,reinterpret_cast<unsigned>(&range),sizeof(range))==0,10);
    native_process_info_record.keep_base=result.base;
    native_process_info_record.keep_handle=result.handle;native_process_info_record.keep_pages=2;
    Fill(result.base);
}
static void WireTests() {
    const unsigned base=native_process_info_record.keep_base,handle=native_process_info_record.keep_handle;
    const unsigned badSizes[]={0,15,17,0xffffffffU};
    for(unsigned i=0;i<4;++i){Query(0,badSizes[i],-22);Pattern(base,0,0,0,0);}
    const unsigned badInputs[]={0,1,0x3fffffffU,0x4003fff8U,0xbfffcff8U,0xbffffffeU,0xc0000000U,0xfffffff8U};
    for(unsigned i=0;i<8;++i){Query(badInputs[i],16,-14);Pattern(base,0,0,0,0);}
    const unsigned badVersions[]={0,2,0xffffffffU};
    for(unsigned i=0;i<3;++i){
        GtosProcessInfoRequest request={badVersions[i],1,0,0};
        Query(reinterpret_cast<unsigned>(&request),16,-38);Pattern(base,0,0,0,0);
    }
    const unsigned badFlags[]={1,0x80000000U,0xffffffffU};
    for(unsigned i=0;i<3;++i){
        GtosProcessInfoRequest request={1,badFlags[i],0,48};
        Query(reinterpret_cast<unsigned>(&request),16,-22);Pattern(base,0,0,0,0);
    }
    const unsigned badOutputsizes[]={0,47,49,0xffffffffU};
    for(unsigned i=0;i<4;++i){
        GtosProcessInfoRequest request={1,0,0,badOutputsizes[i]};
        Query(reinterpret_cast<unsigned>(&request),16,-22);Pattern(base,0,0,0,0);
    }
    const unsigned badOutputs[]={0,1,0x3fffffffU,0x40000000U,0x40020000U+4096-24,
        base+8192-47,base+8192,0xbfffcff8U,0xbffff000U-24,0xc0000000U-24,0xfffffff8U};
    for(unsigned i=0;i<11;++i){
        GtosProcessInfoRequest request=Request(badOutputs[i]);
        Query(reinterpret_cast<unsigned>(&request),16,-14);Pattern(base,0,0,0,0);
    }
    Permissions(handle,GTOS_VM_READ);
    GtosProcessInfoRequest request=Request(base+4096-24);
    Query(reinterpret_cast<unsigned>(&request),16,-14);Pattern(base,0,0,0,0);
    request=Request(base+4096+128);
    Query(reinterpret_cast<unsigned>(&request),16,-14);Pattern(base,0,0,0,0);
    Permissions(handle,GTOS_VM_NONE);
    request=Request(base+4096-24);Query(reinterpret_cast<unsigned>(&request),16,-14);
    Permissions(handle,GTOS_VM_READ_WRITE);Pattern(base,0,0,0,0);
    // A readable request prefix cannot bypass validation of its absent suffix.
    Fill(base);request=Request(base+256);
    Copy(reinterpret_cast<void*>(base+4096-8),&request,sizeof(request));
    Permissions(handle,GTOS_VM_NONE);Query(base+4096-8,16,-14);
    Permissions(handle,GTOS_VM_READ_WRITE);Pattern(base,base+4096-8,&request,0,0);
    Fill(base);
    const unsigned goodOutputs[]={base+256,base+4096-24,base+8192-48};
    for(unsigned i=0;i<3;++i){
        request=Request(goodOutputs[i]);Query(reinterpret_cast<unsigned>(&request),16,0);
        const GtosProcessInfoResult result=Load(goodOutputs[i]);Metadata(result);
        Pattern(base,0,0,goodOutputs[i],&result);Fill(base);
    }
    // The request itself may be wholly readonly; only its result needs RW.
    static const GtosProcessInfoRequest readonlyRequest={1,0,GTOS_PROCESS_INFO_RECORD_VA+80,48};
    Query(reinterpret_cast<unsigned>(&readonlyRequest),16,0);
    Metadata(Load(GTOS_PROCESS_INFO_RECORD_VA+80));
    // Aliases exercise the kernel snapshot before every result write.
    const unsigned offsets[]={0,1,4,12,16,32};
    for(unsigned i=0;i<6;++i){
        request=Request(base+128+offsets[i]);Copy(reinterpret_cast<void*>(base+128),&request,16);
        Query(base+128,16,0);const GtosProcessInfoResult result=Load(request.result_va);Metadata(result);
        Pattern(base,base+128,&request,request.result_va,&result);Fill(base);
    }
    request=Request(base+333);Copy(reinterpret_cast<void*>(base+1019),&request,16);
    Query(base+1019,16,0);GtosProcessInfoResult result=Load(request.result_va);Metadata(result);
    Pattern(base,base+1019,&request,request.result_va,&result);Fill(base);
    request=Request(base+256);Copy(reinterpret_cast<void*>(base+8192-16),&request,16);
    Query(base+8192-16,16,0);result=Load(request.result_va);Metadata(result);
    Pattern(base,base+8192-16,&request,request.result_va,&result);Fill(base);
    request=Request(base+130);request.version=2;Copy(reinterpret_cast<void*>(base+128),&request,16);
    Query(base+128,16,-38);Pattern(base,base+128,&request,0,0);Fill(base);
    request=Request(base+130);request.flags=1;Copy(reinterpret_cast<void*>(base+128),&request,16);
    Query(base+128,16,-22);Pattern(base,base+128,&request,0,0);Fill(base);
}
extern "C" void NativeEntry() {
    Require(Call(GTOS_SYS_ABI,0,0)==1,11);Sample();KeepPages();
    WireTests();
    Call(GTOS_SYS_YIELD,0,0);Sample();
    native_process_info_record.stage=1;
#if GTOS_PROCESS_INFO_PROBE_MODE==1
    *reinterpret_cast<volatile unsigned*>(0xbfffcffcU)=0x494e464fU;
#elif GTOS_PROCESS_INFO_PROBE_MODE==2
    for(;;)Call(GTOS_SYS_YIELD,0,0);
#elif GTOS_PROCESS_INFO_PROBE_MODE==3
    for(;;){Sample();Call(GTOS_SYS_YIELD,0,0);}
#else
    native_process_info_record.stage=2;
    static const char pass[]="GTOS NATIVE PROCESS INFO PASS V1\n";
    Require(Call(GTOS_SYS_WRITE,reinterpret_cast<unsigned>(pass),sizeof(pass)-1)==sizeof(pass)-1,12);
    Call(GTOS_SYS_EXIT,0,0);
#endif
    asm volatile("ud2");__builtin_unreachable();
}
