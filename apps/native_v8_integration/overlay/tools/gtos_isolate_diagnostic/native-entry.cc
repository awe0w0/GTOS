#include "runtime/sdk/process/abi.h"
extern "C" int NativeV8IsolateMain();
extern "C" void gtos_native_thread_cleanup() noexcept;
extern "C" [[noreturn]] void gtos_native_exit_with_tls(unsigned) noexcept;
typedef void (*Initializer)();
extern "C" Initializer __init_array_start[],__init_array_end[],__fini_array_start[],__fini_array_end[];
extern "C" {
    volatile unsigned native_v8_integration_stage=0;
    volatile unsigned native_v8_integration_result=0xffffffffU;
    void NativeEntry() {
        native_v8_integration_stage=1;
        for (Initializer* p=__init_array_start;p!=__init_array_end;++p) (*p)();
        native_v8_integration_stage=2;
        int result=NativeV8IsolateMain();
        native_v8_integration_result=(unsigned)result;
        native_v8_integration_stage=3;
        gtos_native_thread_cleanup();
        for (Initializer* p=__fini_array_end;p!=__fini_array_start;) (*--p)();
        native_v8_integration_stage=4;
        gtos_native_exit_with_tls((unsigned)result);
    }
}
