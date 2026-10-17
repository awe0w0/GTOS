#include <process/abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>
#include "record.h"
#include "md5-vectors.h"
extern "C" {
volatile CryptoMd5Record native_crypto_record __attribute__((section(".data.crypto_record"))) = {1};
unsigned native_crypto_probe_call(const unsigned char*,unsigned,unsigned,unsigned char*);
}
namespace {
int Call(unsigned op,unsigned a=0,unsigned b=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "memory", "cc"); return (int)op;
}
template <class T> int Request(unsigned op,T& request) { return Call(op,(unsigned)&request,sizeof(request)); }
void Need(bool pass,unsigned line) {
    native_crypto_record.checks=native_crypto_record.checks+1;
    if (!pass) { native_crypto_record.error=line; Call(GTOS_SYS_EXIT,line); for (;;) Call(GTOS_SYS_YIELD); }
}
#define NEED(x) Need((x),__LINE__)
void Digest(const unsigned char* input,unsigned length,unsigned stride,
            unsigned char* output,const unsigned char* expected) {
    native_crypto_record.stride=stride;
    for (unsigned i=0;i<16;++i) output[i]=0xa5;
    const unsigned result=native_crypto_probe_call(input,length,stride,output);
    native_crypto_record.actual=result; native_crypto_record.expected=31;
    NEED(result==31);
    for (unsigned i=0;i<16;++i) {
        native_crypto_record.actual=output[i]; native_crypto_record.expected=expected[i];
        NEED(output[i]==expected[i]);
    }
    native_crypto_record.calls=native_crypto_record.calls+1;
    if (stride) native_crypto_record.incremental=native_crypto_record.incremental+1;
    else native_crypto_record.oneShot=native_crypto_record.oneShot+1;
}
void Ordinary() {
    const unsigned strides[]={0,1,7,55,56,63,64,65,127,4096};
    unsigned char output[18];
    for (unsigned row=0;row<7;++row) {
        native_crypto_record.row=row;
        const auto& value=nativeCryptoMd5RfcVectors[row];
        for (unsigned stride : strides) {
            for (auto& byte : output) byte=0xa5;
            Digest((const unsigned char*)value.input,value.length,stride,output+1,value.digest);
            NEED(output[0]==0xa5 && output[17]==0xa5);
        }
    }
    alignas(16) unsigned char input[4144];
    for (unsigned row=0;row<16;++row) {
        native_crypto_record.row=100+row;
        const auto& value=nativeCryptoMd5BinaryVectors[row];
        for (unsigned alignment=0;alignment<16;++alignment) {
            native_crypto_record.alignment=alignment;
            for (auto& byte : input) byte=0xa5;
            unsigned char* data=input+16+alignment;
            for (unsigned i=0;i<value.length;++i) data[i]=(unsigned char)(i*37+11);
            for (unsigned stride : strides) {
                for (auto& byte : output) byte=0xa5;
                Digest(data,value.length,stride,output+1,value.digest);
                NEED(output[0]==0xa5 && output[17]==0xa5);
            }
            for (unsigned i=0;i<value.length;++i) NEED(data[i]==(unsigned char)(i*37+11));
            for (unsigned i=0;i<16+alignment;++i) NEED(input[i]==0xa5);
            for (unsigned i=16+alignment+value.length;i<sizeof(input);++i) NEED(input[i]==0xa5);
            Call(GTOS_SYS_YIELD);
        }
    }
    // No readable input byte is required when the explicit length is zero.
    for (unsigned stride : strides)
        Digest(nullptr,0,stride,output+1,nativeCryptoMd5RfcVectors[0].digest);
}
GtosVmReserveResult Reserve(unsigned pages) {
    GtosVmReserveResult result={};
    GtosVmReserveRequest request={1,pages*4096,4096,0,(unsigned)&result};
    NEED(Request(GTOS_SYS_VM_RESERVE,request)==0 && result.version==1 && result.handle
        && result.length==pages*4096 && result.page_size==4096 && !(result.base&4095));
    return result;
}
void Protect(const GtosVmReserveResult& region,unsigned offset,unsigned length,unsigned access) {
    GtosVmRangeRequest request={1,region.handle,offset,length,access};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,request)==0);
}
void Query(const GtosVmReserveResult& region,unsigned pages) {
    GtosVmRegionInfo result={}; GtosVmQueryRequest request={1,region.handle,(unsigned)&result};
    NEED(Request(GTOS_SYS_VM_QUERY,request)==0 && result.version==1 && result.handle==region.handle
        && result.base==region.base && result.length==region.length && result.resident_pages==pages);
}
void Release(const GtosVmReserveResult& region) {
    GtosVmControlRequest request={1,region.handle,0}; NEED(Request(GTOS_SYS_VM_RELEASE,request)==0);
}
void Boundaries() {
    const auto input=Reserve(4),output=Reserve(3);
    Query(input,0); Query(output,0);
    Protect(input,4096,8192,GTOS_VM_READ_WRITE);
    Protect(output,0,4096,GTOS_VM_READ_WRITE);
    Query(input,2); Query(output,1);
    const unsigned end=input.base+12288,outEnd=output.base+4096;
    native_crypto_record.inputEnd=end; native_crypto_record.outputEnd=outEnd;
    unsigned char* out=(unsigned char*)(outEnd-16);
    const unsigned strides[]={0,1,7,55,56,63,64,65,127,4096};
    for (unsigned row=0;row<16;++row) {
        native_crypto_record.row=200+row; const auto& value=nativeCryptoMd5BinaryVectors[row];
        Protect(input,4096,8192,GTOS_VM_READ_WRITE);
        unsigned char* data=(unsigned char*)(end-value.length);
        for (unsigned i=0;i<value.length;++i) data[i]=(unsigned char)(i*37+11);
        Protect(input,4096,8192,GTOS_VM_READ);
        for (unsigned stride : strides) {
            out[-1]=0xa5;
            Digest(data,value.length,stride,out,value.digest);
            NEED(out[-1]==0xa5);
        }
    }
    if (native_crypto_record.mode) {
        const unsigned mode=native_crypto_record.mode;
        native_crypto_record.expectedFault=mode==1 ? end : mode==2 ? outEnd : (unsigned)out;
        native_crypto_record.faultError=mode==1 ? 4 : mode==2 ? 6 : 7;
        native_crypto_record.stage=10;
        if (mode==3) Protect(output,0,4096,GTOS_VM_READ);
        native_crypto_probe_call((const unsigned char*)(mode==1 ? end-63 : end-127),
            mode==1 ? 64 : 127,0,mode==2 ? out+1 : out);
        NEED(false);
    }
    Release(output); Release(input);
}
void Main() {
    asm volatile("pxor %%xmm0,%%xmm0" : : : "xmm0");
    GtosProcessInfoResult info={}; GtosProcessInfoRequest request={1,0,(unsigned)&info,sizeof(info)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,request)==0 && info.thread_id==info.process_id);
    native_crypto_record.parent=info.process_id;
    NEED(info.scheduler_cpu_count==1 && info.maximum_parallel_threads==1);
    if (!native_crypto_record.mode) Ordinary();
    Boundaries();
    NEED(native_crypto_record.calls==2800 && native_crypto_record.oneShot==280
        && native_crypto_record.incremental==2520);
    native_crypto_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Call(GTOS_SYS_YIELD);
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Call(GTOS_SYS_YIELD); }
