#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "heap.h"
#include <process/abi.h>
#include <process/resource_abi.h>
#include "src/__support/File/file.h"
#include "record.h"
#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"
#include "src/stdio/stderr.h"
#ifndef GTOS_FILE_MODE
#define GTOS_FILE_MODE 0
#endif
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,GTOS_FILE_MODE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    typedef unsigned U;typedef unsigned char B;
    void* pressure[80];char path[513];B comparison[256];
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Exit(U code) { Raw(GTOS_SYS_EXIT,code,0);for (;;) asm volatile("ud2"); }
    void NeedAt(bool value,U line) {
        native_file_record.ack=line;
        native_file_record.checks=native_file_record.checks+1;if (!value) { native_file_record.error=native_file_record.checks;Exit(91); }
    }
#define Need(value) NeedAt((value),__LINE__)
    B Pattern(U i) { return (B)(i*37+i/251+17); }
    void EmptyList() { Need(__llvm_libc_cr::File::get_first_file()==nullptr); }
    void StandardStreams() {
        const U before=native_file_record.checks;
        Need(stdin&&stdout&&stderr&&stdin!=stdout&&stdout!=stderr&&stdin!=stderr);
        Need(stdin==__llvm_libc_cr::stdin&&stdout==__llvm_libc_cr::stdout&&stderr==__llvm_libc_cr::stderr);
        errno=0;Need(fileno(stdin)==-1&&errno==ENOSYS);
        errno=0;Need(fileno(stdout)==-1&&errno==ENOSYS);
        errno=0;Need(fileno(stderr)==-1&&errno==ENOSYS);
        GtosVmRegionInfo empty={};Need(gtos_native_heap_query(&empty)==0);
        Need(fputs("STANDARD CONSOLE STDOUT\n",stdout)>=0);
        Need(fputs("STANDARD CONSOLE STDERR\n",stderr)>=0);
        char output[320];for (U i=0;i<sizeof(output);i++) output[i]='Q';
        Need(fwrite(output,1,sizeof(output),stdout)==sizeof(output));
        Need(fputc('\n',stdout)=='\n');
        errno=0;Need(fwrite((const void*)0x100000,1,16,stdout)==0&&errno==EFAULT&&ferror(stdout));
        clearerr(stdout);Need(!ferror(stdout));
        errno=0;Need(fwrite((const void*)0xfffffff0U,1,32,stdout)==0&&errno==EOVERFLOW&&ferror(stdout));
        clearerr(stdout);
        errno=0;Need(fgetc(stdin)==EOF&&errno==ENOSYS&&ferror(stdin)&&!feof(stdin));
        Need(ungetc('K',stdin)=='K'&&fgetc(stdin)=='K');
        clearerr(stdin);Need(!ferror(stdin)&&!feof(stdin));
        errno=0;Need(fwrite("x",1,1,stdin)==0&&errno==EBADF&&ferror(stdin));
        clearerr(stdin);
        errno=0;Need(fseek(stdout,1,SEEK_SET)==-1&&errno==ESPIPE);
        errno=0;Need(ftello(stderr)==-1&&errno==ESPIPE);
        static char caller[32];Need(setvbuf(stdout,caller,_IOFBF,sizeof(caller))==0);
        Need(fputs("BUFFERED STANDARD STDOUT\n",stdout)>=0&&fflush(nullptr)==0);
        FILE* target=stdout;
        Need(freopen("/native/console.bin","w+",target)==target&&stdout==target);
        Need(fileno(target)>0&&fwrite("redirected\n",1,11,target)==11&&fflush(nullptr)==0);
        Need(fseek(target,0,SEEK_SET)==0);
        char actual[11];Need(fread(actual,1,sizeof(actual),target)==sizeof(actual));
        for (U i=0;i<sizeof(actual);i++) Need(actual[i]=="redirected\n"[i]);
        Need(fclose(target)==0&&stdout==nullptr&&__llvm_libc_cr::stdout==nullptr);
        target=stdin;
        Need(freopen("/native/console.bin","rb",target)==target&&stdin==target);
        Need(fgetc(target)=='r'&&fileno(target)>0);
        Need(fclose(target)==0&&stdin==nullptr&&__llvm_libc_cr::stdin==nullptr);
        Need(setvbuf(stderr,nullptr,_IOFBF,128)==0);
        Need(fputs("ALLOCATED STANDARD STDERR\n",stderr)>=0&&fflush(stderr)==0);
        Need(setvbuf(stderr,nullptr,_IONBF,0)==0);
        Need(fputs("STANDARD STREAM CHECKS PASS\n",stderr)>=0);
        Need(fflush(nullptr)==0);EmptyList();
        native_file_record.directory=native_file_record.checks-before;
    }
    void Persistent(B* buffer) {
        native_file_record.stage=10;
        FILE* f=fopen("/native/stdio.bin","w+b");Need(f!=nullptr&&fileno(f)>0);
        native_file_record.file=fileno(f);
        for (U i=0;i<9001;i++) buffer[i]=Pattern(i);
        Need(fwrite(buffer,1,9001,f)==9001&&ftell(f)==9001&&ftello(f)==9001);
        Need(fflush(f)==0&&fseek(f,0,SEEK_SET)==0&&!feof(f));
        Need(fread(buffer,2,5000,f)==4500&&feof(f)&&!ferror(f));
        for (U i=0;i<9001;i++) Need(buffer[i]==Pattern(i));
        Need(ftello(f)==9001);
        clearerr(f);Need(!feof(f)&&!ferror(f));
        Need(fseeko(f,0,SEEK_SET)==0);
        Need(fgetc(f)==Pattern(0)&&ftell(f)==1);
        Need(ungetc(0x180,f)==0x80&&fgetc(f)==0x80);
        Need(ungetc(-2,f)==0xfe&&fgetc(f)==0xfe);
        Need(ungetc('Q',f)=='Q'&&ftell(f)==0&&fgetc(f)=='Q'&&ftell(f)==1);
        Need(fseek(f,5,SEEK_CUR)==0&&ftell(f)==6&&fgetc(f)==Pattern(6));
        rewind(f);Need(ftell(f)==0&&!feof(f)&&!ferror(f));
        errno=0;Need(fseeko(f,INT64_C(0x100000000),SEEK_SET)==-1&&errno==EOVERFLOW);
        Need(ftell(f)==0);
        Need(fclose(f)==0);EmptyList();
        f=fopen("/native/stdio.bin","a+b");Need(f!=nullptr);
        Need(fseek(f,0,SEEK_SET)==0&&fwrite("END",1,3,f)==3&&fflush(f)==0);
        Need(fseek(f,-3,SEEK_END)==0&&fread(buffer,1,3,f)==3&&buffer[0]=='E'&&buffer[1]=='N'&&buffer[2]=='D');
        Need(fclose(f)==0);EmptyList();
        native_file_record.stage=11;
        errno=0;Need(!fopen("/native/stdio.bin","wx")&&errno==EEXIST);EmptyList();
        errno=0;Need(!fopen("/native/missing","rb")&&errno==ENOENT);EmptyList();
        errno=0;Need(!fopen("/native/missing","bad")&&errno==EINVAL);EmptyList();
        for (U i=0;i<512;i++) path[i]='a';path[512]=0;
        errno=0;Need(!fopen(path,"r")&&errno==ENAMETOOLONG);EmptyList();
        FILE* a=fopen("/native/\xe4\xb8\xad\xe6\x96\x87.txt","w+");
        FILE* b=fopen("/native/second.txt","w+");Need(a&&b);
        Need(fputs("line one\n",a)>=0&&fputc('X',a)=='X'&&fputs("second",b)>=0);
        Need(fflush(nullptr)==0&&ftell(a)==10&&ftell(b)==6);
        rewind(a);Need(fgets((char*)buffer,100,a)==(char*)buffer);
        const char* line="line one\n";for (U i=0;i<10;i++) Need(buffer[i]==(B)line[i]);
        Need(fgetc(a)=='X'&&fgetc(a)==EOF&&feof(a));
        clearerr(a);Need(!feof(a)&&!ferror(a));
        Need(fclose(a)==0&&fclose(b)==0);EmptyList();
        f=fopen("/native/second.txt","r");Need(f);
        errno=0;Need(fwrite("bad",1,3,f)==0&&ferror(f)&&errno==EBADF);
        Need(ungetc('Z',f)=='Z'&&ferror(f));
        clearerr(f);Need(!ferror(f)&&fgetc(f)=='Z'&&fgetc(f)=='s');
        Need(freopen("/native/stdio.bin","r",f)==f&&fgetc(f)==Pattern(0));
        Need(freopen("/native/missing","r",f)==nullptr&&errno==ENOENT);
        errno=0;Need(fgetc(f)==EOF&&ferror(f)&&errno==EBADF);
        errno=0;Need(fclose(f)==EOF&&errno==EBADF);EmptyList();
        f=fopen("/fixtures/image.png","rb");Need(f&&fread(buffer,1,9001,f)==1108&&feof(f));
        for (U offset=0;offset<1108;offset+=256) {
            U n=1108-offset;if (n>256) n=256;
            GtosResourceReadRequest r={1,1,offset,(U)comparison,n};
            Need(Raw(GTOS_SYS_RESOURCE_READ,(U)&r,sizeof(r))==(int)n);
            for (U i=0;i<n;i++) Need(buffer[offset+i]==comparison[i]);
        }
        native_file_record.png_bytes=1108;Need(fclose(f)==0);EmptyList();
    }
    void Exhaustion() {
        native_file_record.stage=12;
        FILE* f=fopen("/native/oom.txt","w");Need(f);
        U count=0;
        while (count<80) { void* p=malloc(1024);if (!p) break;pressure[count++]=p; }
        Need(count>0&&count<80);
        errno=0;Need(setvbuf(f,nullptr,_IOFBF,65536)==ENOMEM&&errno==ENOMEM);
        errno=0;Need(!fopen("/native/oom-must-not-exist","w")&&errno==ENOMEM);
        Need(fwrite("ok",1,2,f)==2&&fclose(f)==0);EmptyList();
        for (U i=0;i<count;i++) free(pressure[i]);
        errno=0;Need(!fopen("/native/oom-must-not-exist","r")&&errno==ENOENT);
        for (U i=0;i<60;i++) {
            f=fopen("/native/oom.txt","r");Need(f&&fgetc(f)=='o'&&fgetc(f)=='k'&&fclose(f)==0);EmptyList();
        }
        f=fopen("/native/line.txt","w");Need(f);
        Need(setvbuf(f,nullptr,_IOLBF,32)==0&&fputs("abc\n",f)>=0&&fputs("def",f)>=0&&fputc(0x180,f)==0x80&&fclose(f)==0);
        EmptyList();
        char external[32];
        f=fopen("/native/external.txt","w");Need(f&&setvbuf(f,external,_IOFBF,sizeof(external))==0);
        errno=0;Need(setvbuf(f,nullptr,_IOFBF,65536)==ENOMEM&&errno==ENOMEM);
        Need(fwrite("external",1,8,f)==8&&fclose(f)==0);EmptyList();
        f=fopen("/native/external.txt","r");Need(f&&fread(external,1,32,f)==8&&external[0]=='e'&&external[7]=='l'&&fclose(f)==0);EmptyList();
    }
    void Errors(B* buffer) {
        native_file_record.stage=13;
        FILE* f=fopen("/native/stdio.bin","rb");Need(f&&setvbuf(f,nullptr,_IONBF,0)==0);
        native_file_record.command=1;errno=0;
        size_t n=fread(buffer,1,9001,f);
        native_file_record.command=0;
        Need(n<9001&&ferror(f)&&errno==EIO);clearerr(f);Need(!ferror(f));
        Need(fclose(f)==0);EmptyList();
        f=fopen("/native/io-error.bin","w");Need(f&&setvbuf(f,nullptr,_IONBF,0)==0);
        native_file_record.command=2;errno=0;n=fwrite(buffer,1,9001,f);
        native_file_record.command=0;Need(n<9001&&ferror(f)&&errno==EIO);
        fclose(f);EmptyList();
        f=fopen("/native/line-error.bin","w");Need(f&&setvbuf(f,nullptr,_IOLBF,32)==0);
        buffer[8192]='\n';native_file_record.command=2;errno=0;n=fwrite(buffer,1,9001,f);
        native_file_record.command=0;Need(n<9001&&ferror(f)&&errno==EIO);
        fclose(f);EmptyList();
        f=fopen("/native/close-error.bin","w");Need(f&&setvbuf(f,nullptr,_IOFBF,8192)==0);
        Need(fwrite(buffer,1,6000,f)==6000);
        native_file_record.command=2;errno=0;int result=fclose(f);
        native_file_record.command=0;Need(result==EOF&&errno==EIO);EmptyList();
        f=fopen("/native/flush-error.bin","w");Need(f&&fwrite(buffer,1,32,f)==32&&fflush(f)==0);
        native_file_record.command=3;errno=0;result=fclose(f);
        native_file_record.command=0;Need(result==EOF&&errno==EIO);EmptyList();
    }
}
extern "C" void NativeEntry() {
    StandardStreams();
    B* buffer=(B*)malloc(10000);Need(buffer);
    Persistent(buffer);Exhaustion();Errors(buffer);free(buffer);
    FILE* f=fopen("/native/retained.txt","w");Need(f&&fwrite("retain",1,6,f)==6&&fflush(f)==0);
    native_file_record.file=fileno(f);
    if (GTOS_FILE_MODE) {
        Need(freopen("/native/console-retained.txt","w",stderr)==stderr);
        Need(setvbuf(stderr,nullptr,_IOFBF,2048)==0&&fputs("pending standard buffer",stderr)>=0);
    } else Need(fclose(stderr)==0&&stderr==nullptr&&__llvm_libc_cr::stderr==nullptr);
    GtosVmRegionInfo info={};Need(gtos_native_heap_query(&info)==1&&info.length==65536&&info.resident_pages==16);
    native_file_record.base=info.base;native_file_record.vm_handle=info.handle;
    native_file_record.foreign=1;
    if (GTOS_FILE_MODE==4) {
        native_file_record.stage=4;
        __llvm_libc_cr::Mutex mutex(false,false,false,false);
        mutex.lock();Need(mutex.try_lock()==__llvm_libc_cr::MutexError::BUSY);
        mutex.lock();Exit(93);
    }
    if (GTOS_FILE_MODE==5) {
        native_file_record.stage=4;
        __llvm_libc_cr::Mutex mutex(true,false,false,false);
        mutex.lock();Exit(94);
    }
    native_file_record.stage=3;
    if (GTOS_FILE_MODE==1) { *(volatile U*)0x100000=1;Exit(92); }
    if (GTOS_FILE_MODE==2||GTOS_FILE_MODE==3) for (;;) native_file_record.progress=native_file_record.progress+1;
    Need(fclose(f)==0);EmptyList();
    Need(gtos_native_heap_query(&info)==0);
    GtosVmQueryRequest released={GTOS_VM_ABI_VERSION,native_file_record.vm_handle,(U)&info};
    Need(Raw(GTOS_SYS_VM_QUERY,(U)&released,sizeof(released))==GTOS_VM_ERR_BAD_STATE);
    native_file_record.foreign=0;
    native_file_record.stage=2;Exit(0);
}
