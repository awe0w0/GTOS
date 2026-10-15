#include "runtime/sdk/process/abi.h"
#include <stdlib.h>
extern "C" int NativeV8IsolateMain();
extern "C" int gtos_native_heap_enable_large_allocations() noexcept;
typedef void (*Initializer)();
extern "C" Initializer __init_array_start[],__init_array_end[],__fini_array_start[],__fini_array_end[];
extern "C" {
    volatile unsigned native_v8_integration_stage=0;
    volatile unsigned native_v8_integration_result=0xffffffffU;
    void NativeEntry() {
        native_v8_integration_stage=1;
        if (gtos_native_heap_enable_large_allocations() != 0) __builtin_trap();
        for (Initializer* p=__init_array_start;p!=__init_array_end;++p) (*p)();
        native_v8_integration_stage=2;
        int result=NativeV8IsolateMain();
        native_v8_integration_result=(unsigned)result;
        native_v8_integration_stage=3;
        exit(result);
    }
}
