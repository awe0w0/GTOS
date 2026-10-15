#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "partition_alloc/partition_alloc_base/log_message.h"
#include "partition_alloc/partition_alloc_base/logging.h"
#include "partition_alloc/partition_alloc_base/process/process_handle.h"
#include "partition_alloc/partition_alloc_base/scoped_clear_last_error.h"
#include "partition_alloc/partition_alloc_base/strings/safe_sprintf.h"
#include "partition_alloc/partition_alloc_base/threading/platform_thread.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace base=partition_alloc::internal::base;
namespace logging=partition_alloc::internal::logging;

static_assert(sizeof(void*)==4&&sizeof(base::ProcessId)==4&&sizeof(base::PlatformThreadId)==4);
static_assert(_LIBCPP_ABI_VERSION==1&&_LIBCPP_HAS_THREADS&&_LIBCPP_HAS_THREAD_API_EXTERNAL);
static_assert(EINVAL==22&&EDOM==33&&sizeof(timespec)==16);

extern "C" {
    volatile unsigned native_pa_mask=0;
    volatile unsigned native_pa_expected_write_bytes=0;
    volatile unsigned native_pa_process_id=0;
    volatile unsigned native_pa_thread_id=0;
}

namespace {
unsigned captured_calls;
char captured[1024];
bool Capture(int severity,const char*,int,size_t,const char* message) {
    ++captured_calls;
    if (severity!=logging::LOGGING_ERROR||!message) return false;
    const size_t length=strlen(message);
    if (length>=sizeof(captured)) return false;
    memcpy(captured,message,length+1);
    return true;
}
}

extern "C" int GtosPartitionPlatformChecks() {
    const auto info=gtos::native::CurrentProcessInfo();
    if (base::kNullProcessId!=0||!base::PlatformThreadRef().is_null()||
        !base::PlatformThreadHandle().is_null()) return 101;
    for (unsigned i=0;i<16;++i) {
        if (base::GetCurrentProcId()!=info.process_id||
            base::PlatformThread::CurrentId()!=info.thread_id||
            base::PlatformThread::CurrentRef()!=base::PlatformThreadRef(info.thread_id)||
            base::PlatformThread::CurrentHandle().platform_handle()!=info.thread_id) return 102;
    }
    native_pa_process_id=info.process_id;
    native_pa_thread_id=info.thread_id;
    native_pa_mask=native_pa_mask|1U;

    const int original_errno=errno;
    errno=EDOM;
    {
        base::ScopedClearLastError outer;
        if (errno!=0) return 103;
        errno=EIO;
        {
            base::ScopedClearLastError inner;
            if (errno!=0) return 104;
            errno=EINVAL;
        }
        if (errno!=EIO) return 105;
    }
    if (errno!=EDOM) return 106;
    native_pa_mask=native_pa_mask|2U;

    const int old_level=logging::GetMinLogLevel();
    logging::SetMinLogLevel(logging::LOGGING_ERROR);
    logging::RawLog(logging::LOGGING_INFO,"GTOS_PA_SUPPRESSED_MUST_NOT_APPEAR");
    logging::RawLog(logging::LOGGING_ERROR,nullptr);
    logging::RawLog(logging::LOGGING_ERROR,"");
    logging::SetMinLogLevel(logging::LOGGING_INFO);
    char exact[257];
    for (unsigned i=0;i<255;++i) exact[i]='A'+i%26;
    exact[255]='\n';exact[256]='\0';
    char crossing[258];
    for (unsigned i=0;i<257;++i) crossing[i]='a'+i%26;
    crossing[257]='\0';
    logging::RawLog(logging::LOGGING_INFO,exact);
    logging::RawLog(logging::LOGGING_INFO,crossing);
    logging::SetMinLogLevel(old_level);
    if (errno!=EDOM) return 107;
    native_pa_expected_write_bytes=514;
    native_pa_mask=native_pa_mask|4U;

    const auto old_handler=logging::GetLogMessageHandler();
    logging::SetLogMessageHandler(Capture);
    {
        logging::ErrnoLogMessage message("gtos_pa_consumer.cc",7,logging::LOGGING_ERROR,EINVAL);
        message.stream()<<"errno-probe";
    }
    logging::SetLogMessageHandler(old_handler);
    if (captured_calls!=1||!strstr(captured,"errno-probe: GTOS error (22)")||errno!=EDOM) return 108;
    native_pa_mask=native_pa_mask|8U;

    char short_text[4];
    if (base::strings::SafeSPrintf(short_text,"%s","abcdef")!=6||strcmp(short_text,"abc")) return 109;
    char integer[32];
    if (base::strings::SafeSPrintf(integer,"%d",INT64_MIN)!=20||strcmp(integer,"-9223372036854775808")) return 110;
    native_pa_mask=native_pa_mask|16U;

    timespec before={},after={};
    if (clock_gettime(CLOCK_MONOTONIC,&before)!=0) return 111;
    base::PlatformThread::Sleep(base::Microseconds(-1));
    base::PlatformThread::Sleep(base::Microseconds(0));
    base::PlatformThread::Sleep(base::Microseconds(2000));
    if (clock_gettime(CLOCK_MONOTONIC,&after)!=0) return 112;
    if (after.tv_sec<before.tv_sec||after.tv_sec-before.tv_sec>60) return 113;
    const int64_t elapsed=(after.tv_sec-before.tv_sec)*1000000000+after.tv_nsec-before.tv_nsec;
    if (elapsed<2000000||errno!=EDOM) return 114;
    native_pa_mask=native_pa_mask|32U;
    errno=original_errno;
    return 0;
}
