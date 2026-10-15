#include <stdlib.h>

#include "partition_alloc/partition_alloc_base/log_message.h"
#include "partition_alloc/partition_alloc_base/logging.h"
#include "partition_alloc/partition_alloc_base/process/process_handle.h"
#include "partition_alloc/partition_alloc_base/threading/platform_thread.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace base=partition_alloc::internal::base;
namespace logging=partition_alloc::internal::logging;

static_assert(sizeof(void*)==4&&sizeof(base::ProcessId)==4&&sizeof(base::PlatformThreadId)==4);
static_assert(_LIBCPP_ABI_VERSION==1&&_LIBCPP_HAS_THREADS&&_LIBCPP_HAS_THREAD_API_EXTERNAL);

extern "C" {
    volatile unsigned native_pa_mask=0;
    volatile unsigned native_pa_expected_write_bytes=0;
    volatile unsigned native_pa_process_id=0;
    volatile unsigned native_pa_thread_id=0;
}

extern "C" int GtosPartitionPlatformChecks() {
    const auto info=gtos::native::CurrentProcessInfo();
    if (base::GetCurrentProcId()!=info.process_id||base::PlatformThread::CurrentId()!=info.thread_id) return 191;
    native_pa_process_id=info.process_id;
    native_pa_thread_id=info.thread_id;
    auto* block=static_cast<volatile unsigned char*>(malloc(131072));
    if (!block) return 192;
    for (unsigned i=0;i<131072;++i) block[i]=static_cast<unsigned char>(i%251);
    for (unsigned i=0;i<131072;++i) if (block[i]!=static_cast<unsigned char>(i%251)) return 193;
    logging::SetLogMessageHandler(nullptr);
    logging::SetMinLogLevel(logging::LOGGING_FATAL+1);
    if (logging::GetMinLogLevel()!=logging::LOGGING_FATAL) return 194;
    native_pa_expected_write_bytes=sizeof("[FATAL:gtos_pa_fatal.cc(7)] GTOS_PA_FATAL_EXPECTED\n")-1;
    native_pa_mask=1;
    {
        logging::LogMessage message("gtos_pa_fatal.cc",7,logging::LOGGING_FATAL);
        message.stream()<<"GTOS_PA_FATAL_EXPECTED";
    }
    native_pa_mask=2;
    free(const_cast<unsigned char*>(block));
    return 197;
}
