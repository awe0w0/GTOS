#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <process/abi.h>
#include <process/file_abi.h>
#include <process/vm_abi.h>
#include <process/thread_abi.h>
#include "perfetto/ext/base/thread_checker.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "record.h"
extern "C" {
volatile DirectoryRecord native_file_record __attribute__((section(".data.file_record")))={1};
extern volatile unsigned native_v8_integration_stage;
extern void *__dso_handle;
void gtos_native_thread_cleanup() noexcept;
[[noreturn]] void gtos_native_exit_with_tls(unsigned) noexcept;
int gtos_native_heap_query(GtosVmRegionInfo *) noexcept;
}
namespace {
    int Raw(unsigned operation,unsigned address=0,unsigned bytes=0) {
        asm volatile("int $0x80":"+a"(operation):"b"(address),"c"(bytes):"memory","cc");return (int)operation;
    }
    [[noreturn]] void Failed(unsigned line) {
        native_file_record.failedLine=line;Raw(GTOS_SYS_EXIT,0xCC000000U|line);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool pass,unsigned line) {__atomic_fetch_add(&native_file_record.checks,1U,__ATOMIC_RELAXED);if (!pass) Failed(line);}
#define Need(value) NeedAt((value),__LINE__)
    void Yield() {Need(Raw(GTOS_SYS_YIELD)==0);}
    unsigned Load(volatile unsigned& value) {return __atomic_load_n(&value,__ATOMIC_ACQUIRE);}
    void Store(volatile unsigned& value,unsigned data) {__atomic_store_n(&value,data,__ATOMIC_RELEASE);}
    void Wait(volatile unsigned& value,unsigned expected) {
        unsigned rounds=0;while (Load(value)!=expected) {Need(++rounds<30000);Yield();}
    }
    bool Same(const char* first,const char* second) {while (*first&&*first==*second) {++first;++second;}return *first==*second;}
    int Control(unsigned operation,unsigned handle) {GtosFileControlRequest request={1,handle};return Raw(operation,(unsigned)&request,sizeof(request));}
    int Path(unsigned operation,const char* path) {
        unsigned bytes=1;while (path[bytes-1]) ++bytes;GtosFilePathRequest request={1,(unsigned)path,bytes};return Raw(operation,(unsigned)&request,sizeof(request));
    }
    void Read(unsigned handle,const char* expected,unsigned bytes) {
        char data[16];Need(bytes<=sizeof(data));GtosFileTransferRequest request={1,handle,(unsigned)data,bytes};
        Need(Raw(GTOS_SYS_FILE_READ,(unsigned)&request,sizeof(request))==(int)bytes);
        for (unsigned i=0;i<bytes;++i) Need(data[i]==expected[i]);
    }
    void Write(unsigned handle,const char* data,unsigned bytes) {
        GtosFileTransferRequest request={1,handle,(unsigned)data,bytes};Need(Raw(GTOS_SYS_FILE_WRITE,(unsigned)&request,sizeof(request))==(int)bytes);
    }
    GtosFileInfo DirectoryEntry(unsigned handle) {
        GtosFileInfo info={};GtosFileInfoRequest request={1,handle,(unsigned)&info,sizeof(info)};
        Need(Raw(GTOS_SYS_FILE_DIR_READ,(unsigned)&request,sizeof(request))==1&&info.version==1);return info;
    }
    void SsePattern(unsigned role) {
        unsigned input[4] __attribute__((aligned(16)))={0xC0010000U+role,0xABCD1000U+role,0x56780000U+role,0xA5A50000U+role};
        unsigned output[4] __attribute__((aligned(16)))={};const unsigned mx=0x1F80U|((role&3)<<13);unsigned observedMx=0;
        const unsigned short cw=(unsigned short)(0x037FU|((role&3)<<10));unsigned short observedCw=0;int result;
        asm volatile("ldmxcsr %[mx]; fldcw %[cw]; movdqa %[source],%%xmm0; int $0x80; movdqa %%xmm0,%[destination]; stmxcsr %[mxout]; fnstcw %[cwout]"
            :"=a"(result),[destination]"=m"(output),[mxout]"=m"(observedMx),[cwout]"=m"(observedCw)
            :"0"(GTOS_SYS_YIELD),[mx]"m"(mx),[cw]"m"(cw),[source]"m"(input),"b"(0),"c"(0):"memory","cc","xmm0");
        Need(result==0&&observedMx==mx&&observedCw==cw);for (unsigned i=0;i<4;++i) Need(output[i]==input[i]);
    }
    struct Marker {
        unsigned value=0;
        Marker() {__atomic_fetch_add(&native_file_record.tlsConstructed,1U,__ATOMIC_RELAXED);}
        ~Marker() {__atomic_fetch_add(&native_file_record.tlsDestroyed,1U,__ATOMIC_RELAXED);}
    };
    // This fixture deliberately exercises the real TLS destructor registration
    // and exit cleanup. Limit this warning exception to that one test object.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    thread_local Marker marker;
#pragma clang diagnostic pop
    struct Startup {
        Startup() {Need(native_v8_integration_stage==1);void* p=malloc(32);Need(p!=nullptr);free(p);native_file_record.preinit=1;}
    } startup;
    void FileCases() {
        unsigned before=native_file_record.checks;
        int made=Path(GTOS_SYS_FILE_MKDIR,"/entries");Need(made==0||made==-17);
        made=Path(GTOS_SYS_FILE_MKDIR,"/entries/child");Need(made==0||made==-17);
        const char* paths[]={"/entries/file.bin","/entries/中文"};
        for (const char* path:paths) {
            FILE* file=fopen(path,"w+");Need(file&&fwrite("ABCDEFGH",1,8,file)==8&&fflush(file)==0&&fclose(file)==0);
        }
        errno=0;Need(opendir(nullptr)==nullptr&&errno==EFAULT);
        errno=0;Need(opendir("")==nullptr&&errno==ENOENT);
        char longPath[512];for (char& byte:longPath) byte='x';errno=0;Need(opendir(longPath)==nullptr&&errno==ENAMETOOLONG);
        errno=0;Need(opendir("/absent")==nullptr&&errno==ENOENT);
        errno=0;Need(opendir("/entries/file.bin")==nullptr&&errno==ENOTDIR);
        DIR* first=opendir("/entries");DIR* second=opendir("/entries");Need(first&&second&&first!=second);
        dirent* a=readdir(first);dirent* b=readdir(second);Need(a&&b&&a!=b&&Same(a->d_name,".")&&Same(b->d_name,"."));
        Need(readdir(second)==b&&Same(b->d_name,"..")&&Same(a->d_name,"."));
        unsigned copy=dup(dirfd(first));Need((int)copy>0);rewinddir(first);GtosFileInfo info=DirectoryEntry(copy);
        Need(info.type==GTOS_FILE_TYPE_DIRECTORY&&Same(info.name,"."));a=readdir(first);Need(a&&Same(a->d_name,".."));
        Need(closedir(first)==0&&Control(GTOS_SYS_FILE_DIR_REWIND,copy)==0);info=DirectoryEntry(copy);Need(Same(info.name,"."));Need(close(copy)==0);
        rewinddir(second);unsigned mask=0,count=0;errno=EDOM;
        while ((b=readdir(second))!=nullptr) {
            Need(++count<=5);
            if (Same(b->d_name,".")) {Need(b->d_type==DT_DIR);mask|=1;}
            else if (Same(b->d_name,"..")) {Need(b->d_type==DT_DIR);mask|=2;}
            else if (Same(b->d_name,"child")) {Need(b->d_type==DT_DIR);mask|=4;}
            else if (Same(b->d_name,"file.bin")) {Need(b->d_type==DT_REG);mask|=8;}
            else {Need(Same(b->d_name,"中文")&&b->d_type==DT_REG);mask|=16;}
        }
        Need(mask==31&&count==5&&errno==EDOM);errno=0;Need(readdir(second)==nullptr&&errno==0);
        errno=EDOM;Need(readdir(second)==nullptr&&errno==EDOM);Need(closedir(second)==0);
        first=opendir("/entries");Need(first);int borrowed=dirfd(first);Need(close(borrowed)==0);
        errno=0;Need(readdir(first)==nullptr&&errno==EBADF);Need(closedir(first)==-1&&errno==EBADF);
        FILE* file=fopen("/entries/file.bin","r");Need(file&&setvbuf(file,nullptr,_IONBF,0)==0);copy=dup(fileno(file));Need((int)copy>0);
        Read(copy,"A",1);char value=0;Need(fread(&value,1,1,file)==1&&value=='B');Need(fclose(file)==0);Read(copy,"CD",2);Need(close(copy)==0);
        errno=0;Need(dup(copy)==-1&&errno==EBADF);Need(close(copy)==-1&&errno==EBADF);
        Need(dup(0)==-1&&errno==EBADF&&dup(-1)==-1&&errno==EBADF);
        Need(dup(native_file_record.peerFile)==-1&&errno==EBADF);
        errno=0;Need(fileno(stdout)==-1&&errno==ENOSYS);
        file=fopen("/entries/file.bin","r");Need(file);int aliases[62];for (int& alias:aliases) {alias=dup(fileno(file));Need(alias>0);}
        errno=0;Need(dup(fileno(file))==-1&&errno==EMFILE);Need(opendir("/entries")==nullptr&&errno==EMFILE);
        for (int alias:aliases) Need(close(alias)==0);Need(fclose(file)==0);first=opendir("/entries");Need(first&&closedir(first)==0);
        native_file_record.fileChecks+=native_file_record.checks-before;
    }
    void* allocations[512];
    void CloseErrorCase() {
        FILE* file=fopen("/close-error.bin","w+");Need(file&&fwrite("BASE",1,4,file)==4&&fflush(file)==0);
        int alias=dup(fileno(file));Need(alias>0&&fclose(file)==0);Write(alias,"DIRTY",5);
        native_file_record.ack=alias;Store(native_file_record.command,1);
        errno=0;Need(close(alias)==-1&&errno==EIO);Store(native_file_record.command,0);
        Need(close(alias)==-1&&errno==EBADF);
    }
    void HeapCase() {
        void* anchor=malloc(32);Need(anchor);GtosVmRegionInfo before={},after={};Need(gtos_native_heap_query(&before)==1);
        unsigned count=0;while (count<512) {void* p=malloc(256);if (!p) break;allocations[count++]=p;}
        Need(count>16&&count<512);native_file_record.heapBlocks=count;Store(native_file_record.phase,DirectoryHeapFull);
        errno=0;Need(opendir("/entries")==nullptr&&errno==ENOMEM);Store(native_file_record.phase,DirectoryHeapRecovered);
        for (unsigned i=0;i<count;++i) free(allocations[i]);Need(gtos_native_heap_query(&after)==1&&before.base==after.base&&before.handle==after.handle);
        DIR* stream=opendir("/entries");Need(stream&&closedir(stream)==0);free(anchor);
    }
    perfetto::base::ThreadChecker* ownerChecker;
    perfetto::base::ThreadChecker* copyChecker;
    perfetto::base::ThreadChecker* assignedChecker;
    extern "C" void DirectoryWorker(unsigned role) {
        Need(role<3);auto info=gtos::native::CurrentProcessInfo();Need(info.thread_id!=info.process_id&&info.maximum_parallel_threads==4);
        native_file_record.ids[role]=info.thread_id;Need(errno==0&&marker.value==0);marker.value=0xA0+role;errno=EDOM+role;
        Need(!ownerChecker->CalledOnValidThread());SsePattern(role+1);Store(native_file_record.workerStage[role],1);
        if (native_file_record.mode==2) {for (;;) {SsePattern(role+3);Yield();}}
        if (role==0) {
            Wait(native_file_record.release0,1);Need(ownerChecker->CalledOnValidThread()&&ownerChecker->CalledOnValidThread());Store(native_file_record.workerStage[0],2);
            Wait(native_file_record.release0,2);Need(copyChecker->CalledOnValidThread()&&assignedChecker->CalledOnValidThread());
        } else if (role==1) {Wait(native_file_record.release1,1);Need(!ownerChecker->CalledOnValidThread());}
        else {
            Need(!copyChecker->CalledOnValidThread()&&!assignedChecker->CalledOnValidThread());Wait(native_file_record.release2,1);Need(ownerChecker->CalledOnValidThread());
        }
        DIR* stream=opendir("/entries");Need(stream&&readdir(stream)&&closedir(stream)==0);
        for (unsigned i=0;i<8;++i) {SsePattern(role+5);Need(errno==(int)(EDOM+role)&&marker.value==0xA0+role);}
        __atomic_fetch_or(&native_file_record.workerDone,1U<<role,__ATOMIC_RELEASE);Store(native_file_record.workerStage[role],3);
        gtos_native_exit_with_tls(40+role);
    }
    unsigned Start(unsigned role) {
        GtosVmReserveResult region={};GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&region};Need(Raw(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
        GtosVmRangeRequest commit={1,region.handle,4096,8192,GTOS_VM_READ_WRITE};Need(Raw(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&commit,sizeof(commit))==0);
        unsigned id=0;GtosThreadCreateRequest create={1,0,(unsigned)&DirectoryWorker,role,region.handle,(unsigned)&id};Need(Raw(GTOS_SYS_THREAD_CREATE,(unsigned)&create,sizeof(create))==0&&id);return region.handle;
    }
    void Join(unsigned role,unsigned stack) {
        unsigned code=0;GtosThreadJoinRequest request={1,native_file_record.ids[role],(unsigned)&code};
        for (;;) {int result=Raw(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&request,sizeof(request));if (!result) break;Need(result==GTOS_THREAD_ERR_BUSY);Yield();}
        Need(code==40+role);__atomic_fetch_add(&native_file_record.joined,1U,__ATOMIC_RELAXED);GtosVmControlRequest release={1,stack,0};Need(Raw(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
    }
    void ThreadCases() {
        unsigned before=native_file_record.checks;perfetto::base::ThreadChecker checker;ownerChecker=&checker;Need(checker.CalledOnValidThread());
        marker.value=0xF0;errno=EDOM+7;unsigned first=Start(0),second=Start(1);Wait(native_file_record.workerStage[0],1);Wait(native_file_record.workerStage[1],1);
        Need(errno==EDOM+7&&marker.value==0xF0&&checker.CalledOnValidThread());
        if (native_file_record.mode==2) {Store(native_file_record.phase,DirectoryRetained);*(volatile unsigned*)0xBFFFCFFCU=1;Failed(__LINE__);}
        checker.DetachFromThread();Store(native_file_record.release0,1);Wait(native_file_record.workerStage[0],2);Need(!checker.CalledOnValidThread());
        perfetto::base::ThreadChecker copied(checker),assigned;Need(assigned.CalledOnValidThread());assigned=checker;
        copyChecker=&copied;assignedChecker=&assigned;Need(!copied.CalledOnValidThread()&&!assigned.CalledOnValidThread());
        Store(native_file_record.release0,2);Store(native_file_record.release1,1);Join(0,first);Join(1,second);
        Need(native_file_record.tlsDestroyed==2&&native_file_record.workerDone==3);
        unsigned third=Start(2);Wait(native_file_record.workerStage[2],1);Need(native_file_record.ids[2]!=native_file_record.ids[0]&&native_file_record.ids[2]!=native_file_record.ids[1]);
        checker.DetachFromThread();Store(native_file_record.release2,1);Join(2,third);
        Need(native_file_record.tlsDestroyed==3&&native_file_record.workerDone==7&&!checker.CalledOnValidThread());
        Need(errno==EDOM+7&&marker.value==0xF0);native_file_record.threadChecks+=native_file_record.checks-before;
    }
    void Retain() {
        DIR* first=opendir("/entries");DIR* second=opendir("/entries");Need(first&&second);Need(dup(dirfd(first))>0);
        FILE* file=fopen("/retained.bin","w+");Need(file&&fwrite("KEEP",1,4,file)==4&&fflush(file)==0);
        int a=dup(fileno(file)),b=dup(fileno(file));Need(a>0&&b>0&&fclose(file)==0);Write(b,"DIRTY",5);
        native_file_record.retained=5;
    }
}
extern "C" int NativeV8IsolateMain() {
    Need(native_v8_integration_stage==2&&native_file_record.preinit==1&&native_file_record.mode<=2);
    auto info=gtos::native::CurrentProcessInfo();Need(info.maximum_parallel_threads==4&&info.process_id==info.thread_id);native_file_record.mainId=info.thread_id;
    if (native_file_record.mode==1) {
        FILE* peer=fopen("/peer.bin","w+");Need(peer&&fwrite("peer",1,4,peer)==4&&fflush(peer)==0&&fseek(peer,0,SEEK_SET)==0);
        native_file_record.peerFile=fileno(peer);Store(native_file_record.phase,DirectoryRetained);
        for (;;) {Need(Control(GTOS_SYS_FILE_SIZE,native_file_record.peerFile)==4);__atomic_fetch_add(&native_file_record.peerProgress,1U,__ATOMIC_RELAXED);SsePattern(7);}
    }
    Need(native_file_record.peerFile>0);Store(native_file_record.phase,DirectoryFiles);FileCases();HeapCase();CloseErrorCase();Retain();Store(native_file_record.phase,DirectoryThreads);ThreadCases();
    Need(native_file_record.joined==3&&native_file_record.tlsConstructed==4);Store(native_file_record.phase,DirectoryDone);return 0;
}
