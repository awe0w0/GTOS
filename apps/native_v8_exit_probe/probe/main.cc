#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <type_traits>
#include "heap.h"
#include "record.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/log/internal/conditions.h"
#include "absl/log/internal/log_message.h"
#include "absl/log/internal/nullstream.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/gtos-raw-io.h"

#ifndef GTOS_EXIT_MODE
#define GTOS_EXIT_MODE 0
#endif
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,GTOS_EXIT_MODE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
extern "C" volatile unsigned native_v8_integration_stage,native_v8_integration_result;
extern "C" int __cxa_thread_atexit(void (*)(void*),void*,void*) noexcept;
extern "C" void* __dso_handle;
static_assert(std::is_same<absl::LogEntry::tid_t,uint32_t>::value,"GTOS log ID keeps the full native uint32 ABI");
static_assert(std::is_same<decltype(absl::base_internal::GetCachedTID()),absl::LogEntry::tid_t>::value,"Actual ID producer and log consumer agree");
namespace {
    typedef unsigned U;
    FILE* buffered;
    char buffer[32];
    const char payload[]="buffered-payload";
    void NeedAt(bool value,U line) {
        native_file_record.checks=native_file_record.checks+1;
        if (!value) { native_file_record.error=line;v8::base::gtos::ExitImmediately(91); }
    }
#define Need(value) NeedAt((value),__LINE__)
    struct Automatic {
        ~Automatic() { native_file_record.raw_calls=native_file_record.raw_calls+1; }
    };
    void TlsDestructor(void*) {
        Need(fflush(buffered)==0);
        native_file_record.bad_calls=native_file_record.bad_calls+1;
    }
    __attribute__((constructor)) void Initialize() { native_file_record.mask=native_file_record.mask+1; }
    __attribute__((destructor)) void Finalize() { native_file_record.ack=native_file_record.ack+1; }
    class ActualLogExit:public absl::log_internal::LogMessage {
    public:
        [[noreturn]] static void Terminate() { FailQuietly(); }
    };
}
extern "C" int NativeV8IsolateMain() {
    native_file_record.stage=1;
    Need(native_file_record.mask==1&&native_v8_integration_stage==2);
    char path[]="/native/buffer0";path[14]=char('0'+GTOS_EXIT_MODE);
    buffered=fopen(path,"w");Need(buffered&&fileno(buffered)>0);
    native_file_record.file=static_cast<U>(fileno(buffered));
    Need(setvbuf(buffered,buffer,_IOFBF,sizeof(buffer))==0);
    Need(fwrite(payload,1,sizeof(payload)-1,buffered)==sizeof(payload)-1);
    char durable[]="/native/durable0";durable[15]=char('0'+GTOS_EXIT_MODE);
    FILE* committed=fopen(durable,"w");Need(committed&&fileno(committed)>0);
    native_file_record.directory=static_cast<U>(fileno(committed));
    Need(fwrite("committed",1,9,committed)==9&&fflush(committed)==0);
    Need(__cxa_thread_atexit(TlsDestructor,nullptr,__dso_handle)==0);
    Need(malloc(48)!=nullptr);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.length==65536&&heap.resident_pages==16);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;
    native_file_record.foreign=absl::base_internal::GetCachedTID();
    Need(native_file_record.foreign==v8::base::gtos::CurrentProcessInfo().thread_id);
    native_file_record.command=reinterpret_cast<U>(&native_v8_integration_stage);
    native_file_record.png_bytes=reinterpret_cast<U>(&native_v8_integration_result);
    Automatic automatic;
    native_file_record.stage=3;
#if GTOS_EXIT_MODE == 0
    return 17;
#elif GTOS_EXIT_MODE == 1
    ActualLogExit::Terminate();
#elif GTOS_EXIT_MODE == 2
    absl::log_internal::ExitQuietly();
#elif GTOS_EXIT_MODE == 3
    { absl::log_internal::NullStreamFatal fatal; }
#elif GTOS_EXIT_MODE == 4
    absl::InstallFailureSignalHandler(absl::FailureSignalHandlerOptions{});
#else
    for (;;) { native_file_record.progress=native_file_record.progress+1;v8::base::gtos::Yield(); }
#endif
    return 91;
}
