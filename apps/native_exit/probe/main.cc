#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "heap.h"
#include "thread-exit.h"
#include <process/abi.h>
#include "record.h"
extern "C" int __cxa_atexit(void (*)(void*),void*,void*);
extern "C" void __cxa_finalize(void*);
extern "C" void* __dso_handle;
extern "C" volatile unsigned native_v8_integration_stage;
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    typedef unsigned U;
    U trace[256],trace_count,a,b,c,other;
    void* pressure[80];FILE* active;
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;
        Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    void Append(U value) { Need(trace_count<256);trace[trace_count++]=value; }
    void Callback(void* value) { Append((U)value); }
    void CUnit() { Append(90); }
    void Nested(void*) {
        Append(20);Need(__cxa_atexit(Callback,(void*)30,&a)==0);__cxa_finalize(&a);
    }
    __attribute__((constructor(101))) void RegistryChecks() {
        native_file_record.stage=20;const U before=native_file_record.checks;GtosVmRegionInfo heap={};
        Need(gtos_native_heap_query(&heap)==0);__cxa_finalize(nullptr);__cxa_finalize(nullptr);
        Need(trace_count==0);Need(__cxa_atexit(nullptr,nullptr,&a)==-1);Need(atexit(nullptr)!=0);
        for (U i=0;i<32;i++) Need(atexit(CUnit)==0);
        Need(gtos_native_heap_query(&heap)==0);__cxa_finalize(nullptr);Need(trace_count==32);
        for (U i=0;i<32;i++) Need(trace[i]==90);
        __cxa_finalize(nullptr);Need(trace_count==32);trace_count=0;
        for (U i=0;i<67;i++) Need(__cxa_atexit(Callback,(void*)i,i%3==0?&a:i%3==1?&b:&c)==0);
        Need(gtos_native_heap_query(&heap)==1);__cxa_finalize(&other);Need(trace_count==0);
        __cxa_finalize(&b);Need(trace_count==22);U n=0;
        for (int i=66;i>=0;i--) if (i%3==1) Need(trace[n++]==(U)i);
        __cxa_finalize(&b);Need(trace_count==22);
        __cxa_finalize(&c);Need(trace_count==44);
        for (int i=66;i>=0;i--) if (i%3==2) Need(trace[n++]==(U)i);
        __cxa_finalize(&a);Need(trace_count==67);
        for (int i=66;i>=0;i--) if (i%3==0) Need(trace[n++]==(U)i);
        Need(n==67&&gtos_native_heap_query(&heap)==0);__cxa_finalize(nullptr);Need(trace_count==67);trace_count=0;
        Need(atexit(CUnit)==0);Need(__cxa_atexit(Callback,(void*)31,&a)==0);Need(__cxa_atexit(Callback,(void*)32,&b)==0);
        __cxa_finalize(&a);Need(trace_count==1&&trace[0]==31);__cxa_finalize(nullptr);
        Need(trace_count==3&&trace[1]==32&&trace[2]==90);trace_count=0;
        Need(__cxa_atexit(Callback,(void*)11,&a)==0);Need(__cxa_atexit(Nested,nullptr,&a)==0);
        __cxa_finalize(&a);Need(trace_count==3&&trace[0]==20&&trace[1]==30&&trace[2]==11);
        __cxa_finalize(&a);Need(trace_count==3);trace_count=0;
        for (U i=0;i<32;i++) Need(__cxa_atexit(Callback,(void*)i,&a)==0);
        U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
        Need(count>0&&count<80);Need(__cxa_atexit(Callback,(void*)32,&a)==-1);
        __cxa_finalize(&a);Need(trace_count==32);
        for (U i=0;i<32;i++) Need(trace[i]==31-i);
        for (U i=0;i<count;i++) free(pressure[i]);Need(gtos_native_heap_query(&heap)==0);trace_count=0;
        for (U i=0;i<33;i++) Need(__cxa_atexit(Callback,(void*)i,&a)==0);
        __cxa_finalize(&a);Need(trace_count==33);
        for (U i=0;i<33;i++) Need(trace[i]==32-i);
        __cxa_finalize(nullptr);Need(trace_count==33&&gtos_native_heap_query(&heap)==0);
        native_file_record.mask=native_file_record.checks-before;
        const char marker[]="EXIT REGISTRY CHECKS PASS\n";
        Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
        native_file_record.stage=0;
    }
    void Write(char value) { Need(active&&fputc(value,active)==value); }
    void OnTls(void*) {
        Need(native_v8_integration_stage==3&&native_file_record.directory==1&&native_file_record.raw_calls==0);
        Write('T');native_file_record.raw_calls=1;
    }
    void OnC() {
        Need(native_file_record.raw_calls==1);Write('C');native_file_record.raw_calls=3;
        if (native_file_record.mode==2) { native_file_record.stage=6;*(volatile U*)0x100000=1;Fail(__LINE__); }
        if (native_file_record.mode==3) {
            native_file_record.stage=6;for (;;) native_file_record.progress=native_file_record.progress+1;
        }
    }
    struct LifeObject {
        LifeObject() { Need(native_file_record.directory==0);native_file_record.directory=1; }
        ~LifeObject() {
            Need(native_v8_integration_stage==3&&native_file_record.raw_calls==3);
            Write('G');native_file_record.raw_calls=7;native_file_record.directory=2;
        }
    } global_object;
    void Late() {
        Need(native_file_record.raw_calls==31);Write('N');native_file_record.raw_calls=63;
        native_file_record.foreign=0;native_file_record.stage=2;
        const char marker[]="EXIT LIFECYCLE CHECKS PASS\n";
        Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    }
    void FiniA() {
        Need(native_file_record.raw_calls==15);Write('a');native_file_record.raw_calls=31;Need(atexit(Late)==0);
    }
    void FiniB() { Need(native_file_record.raw_calls==7);Write('b');native_file_record.raw_calls=15; }
    using Initializer=void(*)();
    __attribute__((section(".fini_array.01000"),used)) Initializer early=FiniA;
    __attribute__((section(".fini_array.02000"),used)) Initializer later=FiniB;
}
extern "C" int NativeV8IsolateMain() {
    Need(native_v8_integration_stage==2&&native_file_record.directory==1&&native_file_record.mask>400);
    Need(native_file_record.mode<7);GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==0);
    active=fopen("/native/lifecycle.txt","w+");Need(active&&setvbuf(active,nullptr,_IOFBF,32)==0);Write('M');
    Need(freopen("/native/lifecycle-stderr.txt","w",stderr)==stderr);
    Need(setvbuf(stderr,nullptr,_IOFBF,32)==0&&fputc('S',stderr)=='S');
    Need(atexit(OnC)==0&&__cxa_thread_atexit(OnTls,nullptr,&__dso_handle)==0);
    Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.foreign=1;
    if (native_file_record.mode==4) {
        native_file_record.stage=7;for (;;) native_file_record.progress=native_file_record.progress+1;
    }
    if (native_file_record.mode==6) { native_file_record.stage=7;abort(); }
    if (native_file_record.mode==5) native_file_record.command=3;
    if (native_file_record.mode==1) exit(29);
    return native_file_record.mode==5?31:17;
}
