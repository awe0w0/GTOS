#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <process/abi.h>
#include "src/base/platform/platform.h"
#include "src/__support/File/file.h"
#include "heap.h"
#include "record.h"
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    using U=unsigned;using Opener=FILE* (*)(const char*,const char*);
    Opener volatile opener=&v8::base::OS::FOpen;
    void* pressure[80];char longPath[513],exclusive[]="/native/fopen-exclusive-00.bin";U sequence;
    const char path[]="/native/fopen-probe.bin",absent[]="/native/fopen-oom-new.bin";
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    FILE* Open(const char* name,const char* mode) { native_file_record.raw_calls=native_file_record.raw_calls+1;return opener(name,mode); }
    void Empty() { GtosVmRegionInfo heap={};Need(__llvm_libc_cr::File::get_first_file()==nullptr&&gtos_native_heap_query(&heap)==0); }
    void Reject(const char* name,const char* mode,int error) { errno=0;Need(Open(name,mode)==nullptr&&errno==error);Empty(); }
    void Read(const char* expected,U bytes) {
        FILE* file=Open(path,"rb");Need(file&&fileno(file)>0);char actual[16];
        Need(fread(actual,1,sizeof(actual),file)==bytes&&feof(file)&&!ferror(file));
        for (U i=0;i<bytes;i++) Need(actual[i]==expected[i]);
        Need(fclose(file)==0);Empty();
    }
    void Flow() {
        const U begin=native_file_record.checks;Empty();
        Reject("/native","r",EISDIR);Reject("/native","w",EISDIR);Reject("/native","a",EISDIR);
        Reject(absent,"r",ENOENT);Reject("","r",EINVAL);
        for (U i=0;i<512;i++) longPath[i]='p';longPath[512]=0;
        Reject(longPath,"r",ENAMETOOLONG);Reject(path,"bad",EINVAL);
        FILE* file=Open(path,"w+b");Need(file&&fileno(file)>0);
        Need(fwrite("seed",1,4,file)==4);Need(fclose(file)==0);Empty();Read("seed",4);
        file=Open(path,"r+b");Need(file&&fileno(file)>0);
        Need(fputc('S',file)=='S'&&fseek(file,0,SEEK_SET)==0);Need(fclose(file)==0);Empty();Read("Seed",4);
        file=Open(path,"ab");Need(file&&fileno(file)>0);
        Need(fwrite("!",1,1,file)==1);Need(fclose(file)==0);Empty();Read("Seed!",5);
        exclusive[24]='0'+native_file_record.mode;exclusive[25]='0'+sequence++;
        file=Open(exclusive,"wx");Need(file&&fileno(file)>0);
        Need(fwrite("x",1,1,file)==1);Need(fclose(file)==0);
        errno=0;Need(Open(exclusive,"wx")==nullptr&&errno==EEXIST);Empty();
        U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
        Need(count>0&&count<80);errno=0;Need(Open(path,"w")==nullptr&&errno==ENOMEM);
        errno=0;Need(Open(absent,"w")==nullptr&&errno==ENOMEM);
        for (U i=0;i<count;i++) free(pressure[i]);Empty();Read("Seed!",5);
        Reject(absent,"r",ENOENT);native_file_record.mask=native_file_record.checks-begin;
    }
}
extern "C" int NativeV8IsolateMain() {
    const U mode=native_file_record.mode;Need(mode<4);const U loops=mode==3?4:1;
    for (U i=0;i<loops;i++) Flow();native_file_record.directory=loops;
    const char marker[]="FOPEN CHECKS PASS\n";Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    if (mode==0||mode==3) { native_file_record.stage=2;return 0; }
    FILE* file=Open(path,"rb");Need(file&&fileno(file)>0);
    U* input=(U*)malloc(4096);Need(input);*input=0xf0a01234;
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16);
    native_file_record.file=fileno(file);native_file_record.foreign=(U)input;
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.stage=3;
    if (mode==2) { *(volatile U*)0x100000=1;Fail(__LINE__); }
    for (;;) native_file_record.progress=native_file_record.progress+1;
}
